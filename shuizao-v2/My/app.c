#include "app.h"
#include "app_config.h"
#include "motor.h"
#include "pg.h"
#include "protocol.h"
#include "pump.h"
#include "screen.h"
#include <stdbool.h>

/*
 * app.c
 * 主业务状态机实现。
 * 自动流程按配置表移动到指定 PG，执行固定时间吸取/喷淋，并在结束后回原点。
 */

static App_State app_state = APP_STATE_IDLE;
static App_Alarm app_alarm = APP_ALARM_NONE;

/* 当前自动任务参数：体积只用于合法性和屏幕状态，具体阶段由配置表驱动。 */
static uint16_t app_volume_ml = 0U;
static uint8_t app_keep10 = 0U;
static uint8_t app_pump_speed_percent = APP_DEFAULT_PUMP_SPEED_PERCENT;

/* 阶段索引在吸取序列和喷淋序列中复用，每次切换大阶段时会重新置零。 */
static uint8_t app_phase_index = 0U;
static PG_ID app_target_pg = PG_INVALID;
static uint32_t app_state_start_tick = 0U;
static uint32_t app_last_screen_tick = 0U;

/* START 命令会先回原点，回原点完成后根据此标志继续自动流程。 */
static bool app_auto_after_home = false;

/* 手动动作记录，用于到达 PG3/PG14 后自动停止。 */
static Protocol_ManualAction app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
static Protocol_ManualAction app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;

static uint32_t Now(void)
{
    return HAL_GetTick();
}

static uint32_t Elapsed(uint32_t start_tick)
{
    return Now() - start_tick;
}

static void App_SetState(App_State state)
{
    /* 所有状态切换统一更新时间戳，便于超时和进度计算。 */
    app_state = state;
    app_state_start_tick = Now();
}

static bool App_IsAutoRunning(void)
{
    /* 自动流程中的状态，用于拒绝手动命令和判断 STOP 的处理方式。 */
    return app_state == APP_STATE_HOMING ||
           app_state == APP_STATE_CHECK_Y ||
           app_state == APP_STATE_MOVE_TO_ASPIRATE ||
           app_state == APP_STATE_ASPIRATING ||
           app_state == APP_STATE_MOVE_TO_KEEP10 ||
           app_state == APP_STATE_WAIT_MANUAL_CLEAN ||
           app_state == APP_STATE_MOVE_TO_SPRAY ||
           app_state == APP_STATE_SPRAYING ||
           app_state == APP_STATE_RETURN_HOME;
}

static const char *App_StateText(App_State state)
{
    /* MCU 发送英文短状态词，中文显示建议由 HMI 根据 n_state 自行映射。 */
    switch (state) {
    case APP_STATE_IDLE:
        return "READY";
    case APP_STATE_HOMING:
        return "HOMING";
    case APP_STATE_CHECK_Y:
        return "CHECK Y";
    case APP_STATE_MOVE_TO_ASPIRATE:
        return "MOVE ASP";
    case APP_STATE_ASPIRATING:
        return "ASPIRATE";
    case APP_STATE_MOVE_TO_KEEP10:
        return "MOVE 10ML";
    case APP_STATE_WAIT_MANUAL_CLEAN:
        return "WAIT CLEAN";
    case APP_STATE_MOVE_TO_SPRAY:
        return "MOVE SPRAY";
    case APP_STATE_SPRAYING:
        return "SPRAY";
    case APP_STATE_RETURN_HOME:
        return "RETURN";
    case APP_STATE_DONE:
        return "DONE";
    case APP_STATE_ERROR:
        return "ERROR";
    case APP_STATE_ESTOP:
        return "ESTOP";
    case APP_STATE_MANUAL:
        return "MANUAL";
    default:
        return "UNKNOWN";
    }
}

static void App_AllStop(void)
{
    /* 紧急停机共用出口：停全部泵，Z 轴刹车，并清手动动作记录。 */
    Pump_StopAll();
    Motor_Brake(APP_Z_MOTOR_ID);
    app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
    app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
}

static uint16_t App_ZSpeed(void)
{
    return Pump_SpeedPercentToMotorSpeed(APP_Z_SPEED_PERCENT);
}

static void App_StartZMoveTo(PG_ID target)
{
    int8_t current_index;
    int8_t target_index;
    uint8_t direction;

    app_target_pg = target;

    /* 如果目标 PG 已经有效，说明已经到位，直接刹车即可。 */
    if (PG_IsActive(target)) {
        Motor_Brake(APP_Z_MOTOR_ID);
        return;
    }

    /* 根据当前 Z 轴位置和目标在 APP_Z_ORDER 中的相对顺序判断运动方向。 */
    current_index = PG_GetCurrentZIndex();
    target_index = PG_FindIndexInList(target, APP_Z_ORDER, APP_Z_ORDER_COUNT);

    if (target == APP_Z_HOME_PG) {
        direction = APP_Z_UP_DIRECTION;
    } else if (current_index >= 0 && target_index >= 0 && target_index < current_index) {
        direction = APP_Z_UP_DIRECTION;
    } else {
        direction = APP_Z_DOWN_DIRECTION;
    }

    Motor_Run(APP_Z_MOTOR_ID, direction, App_ZSpeed());
}

static void App_EnterMoveState(App_State state, PG_ID target)
{
    App_SetState(state);
    App_StartZMoveTo(target);
}

static bool App_TargetReached(void)
{
    /* 目标 PG 低电平有效，到位后立即刹车。 */
    if (PG_IsActive(app_target_pg)) {
        Motor_Brake(APP_Z_MOTOR_ID);
        return true;
    }

    return false;
}

static bool App_MoveTimedOut(void)
{
    uint32_t timeout = APP_Z_MOVE_TIMEOUT_MS;

    /* 回原点通常距离可能更长，使用单独超时时间。 */
    if (app_target_pg == APP_Z_HOME_PG) {
        timeout = APP_HOME_TIMEOUT_MS;
    }

    return Elapsed(app_state_start_tick) > timeout;
}

static void App_Fail(App_Alarm alarm)
{
    /* 故障时立即停止所有执行机构，并把报警码同步到屏幕。 */
    app_alarm = alarm;
    app_auto_after_home = false;
    App_AllStop();
    Screen_ShowAlarm((uint16_t)alarm);
    Screen_ShowMessage("ERROR");
    App_SetState(APP_STATE_ERROR);
}

static uint8_t App_ProgressPercent(void)
{
    uint32_t duration = 0U;
    uint32_t elapsed;

    /* 只有吸取和喷淋阶段有固定时长进度，其它移动阶段显示 0。 */
    if (app_state == APP_STATE_ASPIRATING) {
        duration = APP_ASPIRATE_PHASE_MS;
    } else if (app_state == APP_STATE_SPRAYING) {
        duration = APP_SPRAY_PHASE_MS;
    }

    if (duration == 0U) {
        return 0U;
    }

    elapsed = Elapsed(app_state_start_tick);
    if (elapsed >= duration) {
        return 100U;
    }

    return (uint8_t)((elapsed * 100U) / duration);
}

static void App_ReportStatus(bool force)
{
    uint32_t now = Now();

    /* 非强制刷新时按固定周期更新屏幕，避免串口被状态刷屏占满。 */
    if (!force && (now - app_last_screen_tick) < APP_SCREEN_UPDATE_MS) {
        return;
    }

    app_last_screen_tick = now;
    Screen_ShowMessage(App_StateText(app_state));
    Screen_UpdateStatus((uint8_t)app_state,
                        (uint8_t)(app_phase_index + 1U),
                        app_pump_speed_percent,
                        PG_ReadMask(),
                        app_keep10,
                        (uint16_t)app_alarm,
                        App_ProgressPercent());
}

static void App_StartAuto(uint16_t volume_ml, uint8_t keep10)
{
    /* 自动流程只允许从空闲、完成或错误状态重新启动。 */
    if (!(app_state == APP_STATE_IDLE ||
          app_state == APP_STATE_DONE ||
          app_state == APP_STATE_ERROR)) {
        app_alarm = APP_ALARM_BUSY;
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    /* 当前版本只接受 50ml 和 100ml，后续扩展体积时在这里放开。 */
    if (volume_ml != 50U && volume_ml != 100U) {
        app_alarm = APP_ALARM_BAD_VOLUME;
        Screen_ShowMessage("BAD VOL");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    /* 每次启动都先回原点，再检查 Y 轴并进入吸取阶段。 */
    app_alarm = APP_ALARM_NONE;
    app_volume_ml = volume_ml;
    app_keep10 = keep10 ? 1U : 0U;
    app_phase_index = 0U;
    app_auto_after_home = true;
    App_AllStop();
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_HOME_PG);
    App_ReportStatus(true);
}

static void App_StartHome(bool continue_auto)
{
    /* HOME 可单独执行，也可作为 START 的前置动作。 */
    app_alarm = APP_ALARM_NONE;
    app_auto_after_home = continue_auto;
    app_phase_index = 0U;
    Pump_StopAll();
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_HOME_PG);
}

static void App_HandleManual(const Protocol_Command *command)
{
    uint8_t speed = command->speed_percent;

    /* 自动流程运行中禁止手动动作，防止状态机和手动控制抢执行机构。 */
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    /* 手动命令未带速度时使用当前全局泵速。 */
    if (speed == 0U) {
        speed = app_pump_speed_percent;
    }
    speed = Pump_ClampSpeedPercent(speed);

    if (command->manual_target == PROTOCOL_MANUAL_TARGET_Z) {
        if (command->manual_action == PROTOCOL_MANUAL_ACTION_UP) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_UP;
            Motor_Run(APP_Z_MOTOR_ID, APP_Z_UP_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_DOWN) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_DOWN;
            Motor_Run(APP_Z_MOTOR_ID, APP_Z_DOWN_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            if (app_manual_pump_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    } else if (command->manual_target == PROTOCOL_MANUAL_TARGET_PUMP) {
        if (command->manual_action == PROTOCOL_MANUAL_ACTION_IN) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_IN;
            Pump_RunAll(PUMP_DIR_IN, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_OUT) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_OUT;
            Pump_RunAll(PUMP_DIR_OUT, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    }

    App_ReportStatus(true);
}

static void App_HandleCommand(const Protocol_Command *command)
{
    /* 协议层只负责解析，这里才真正执行命令对应的业务动作。 */
    switch (command->type) {
    case PROTOCOL_CMD_START:
        App_StartAuto(command->volume_ml, command->keep10);
        break;

    case PROTOCOL_CMD_STOP:
        App_AllStop();
        /* STOP 在自动流程中按“停止后回原点”处理。 */
        if (App_IsAutoRunning()) {
            app_auto_after_home = false;
            App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_HOME_PG);
        } else {
            App_SetState(APP_STATE_IDLE);
        }
        break;

    case PROTOCOL_CMD_ESTOP:
        /* ESTOP 立即停机，不自动回原点。 */
        App_AllStop();
        app_auto_after_home = false;
        app_alarm = APP_ALARM_NONE;
        App_SetState(APP_STATE_ESTOP);
        Screen_ShowMessage("ESTOP");
        break;

    case PROTOCOL_CMD_HOME:
        App_StartHome(false);
        break;

    case PROTOCOL_CMD_OK:
        /* 人工清洗确认后，从喷淋序列第一个阶段继续。 */
        if (app_state == APP_STATE_WAIT_MANUAL_CLEAN) {
            app_phase_index = 0U;
            App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY,
                               APP_SPRAY_PG_SEQUENCE[app_phase_index]);
        }
        break;

    case PROTOCOL_CMD_SPEED_SET:
        app_pump_speed_percent = Pump_ClampSpeedPercent(command->speed_percent);
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_MANUAL:
        App_HandleManual(command);
        break;

    case PROTOCOL_CMD_GET_PG:
    case PROTOCOL_CMD_GET_STATE:
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_RESET:
        __set_FAULTMASK(1);
        HAL_NVIC_SystemReset();
        break;

    default:
        app_alarm = APP_ALARM_BAD_COMMAND;
        Screen_ShowMessage("BAD CMD");
        Screen_ShowAlarm((uint16_t)app_alarm);
        break;
    }
}

static void App_ProcessCommands(void)
{
    Protocol_Command command;

    /* 一次主循环尽量处理完队列里的所有命令。 */
    while (Protocol_PopCommand(&command)) {
        App_HandleCommand(&command);
    }
}

static void App_TaskManual(void)
{
    /* 手动上升/下降分别用 PG3/PG14 做软限位。 */
    if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_UP &&
        PG_IsActive(APP_Z_HOME_PG)) {
        Motor_Brake(APP_Z_MOTOR_ID);
        app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
        Screen_ShowMessage("Z HOME");
    } else if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_DOWN &&
               PG_IsActive(APP_Z_BOTTOM_PG)) {
        Motor_Brake(APP_Z_MOTOR_ID);
        app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
        Screen_ShowMessage("Z BOTTOM");
    }

    if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_NONE &&
        app_manual_pump_action == PROTOCOL_MANUAL_ACTION_NONE) {
        App_SetState(APP_STATE_IDLE);
    }
}

static void App_TaskAuto(void)
{
    /* 自动流程状态机：所有等待都用 HAL_GetTick 计时，避免长阻塞延时。 */
    switch (app_state) {
    case APP_STATE_HOMING:
        if (App_TargetReached()) {
            if (app_auto_after_home) {
                App_SetState(APP_STATE_CHECK_Y);
            } else {
                App_SetState(APP_STATE_IDLE);
            }
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_CHECK_Y:
        /* 默认 PG1 为 Y 轴允许工作位置，低电平有效。 */
        if (PG_IsActive(APP_Y_READY_PG)) {
            app_phase_index = 0U;
            App_EnterMoveState(APP_STATE_MOVE_TO_ASPIRATE,
                               APP_ASPIRATE_PG_SEQUENCE[app_phase_index]);
        } else {
            App_Fail(APP_ALARM_Y_NOT_READY);
        }
        break;

    case APP_STATE_MOVE_TO_ASPIRATE:
        if (App_TargetReached()) {
            Pump_RunAll(PUMP_DIR_IN, app_pump_speed_percent);
            App_SetState(APP_STATE_ASPIRATING);
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_ASPIRATING:
        /* 每个吸取阶段使用同一个固定时间和同一个全局泵速。 */
        if (Elapsed(app_state_start_tick) >= APP_ASPIRATE_PHASE_MS) {
            Pump_StopAll();
            app_phase_index++;
            if (app_phase_index < APP_ASPIRATE_PHASE_COUNT) {
                App_EnterMoveState(APP_STATE_MOVE_TO_ASPIRATE,
                                   APP_ASPIRATE_PG_SEQUENCE[app_phase_index]);
            } else if (app_keep10) {
                /* 预留 10ml 时移动到配置的 10ml PG，并等待屏幕 #OK;。 */
                App_EnterMoveState(APP_STATE_MOVE_TO_KEEP10, APP_Z_KEEP10_PG);
            } else {
                app_phase_index = 0U;
                App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY,
                                   APP_SPRAY_PG_SEQUENCE[app_phase_index]);
            }
        }
        break;

    case APP_STATE_MOVE_TO_KEEP10:
        if (App_TargetReached()) {
            Screen_ShowMessage("CLEAN CUP");
            App_SetState(APP_STATE_WAIT_MANUAL_CLEAN);
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_WAIT_MANUAL_CLEAN:
        break;

    case APP_STATE_MOVE_TO_SPRAY:
        if (App_TargetReached()) {
            Pump_RunAll(PUMP_DIR_OUT, app_pump_speed_percent);
            App_SetState(APP_STATE_SPRAYING);
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_SPRAYING:
        /* 每个喷淋阶段使用同一个固定时间和同一个全局泵速。 */
        if (Elapsed(app_state_start_tick) >= APP_SPRAY_PHASE_MS) {
            Pump_StopAll();
            app_phase_index++;
            if (app_phase_index < APP_SPRAY_PHASE_COUNT) {
                App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY,
                                   APP_SPRAY_PG_SEQUENCE[app_phase_index]);
            } else {
                /* 自动流程结束后必须回原点。 */
                App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_HOME_PG);
            }
        }
        break;

    case APP_STATE_RETURN_HOME:
        if (App_TargetReached()) {
            App_AllStop();
            app_auto_after_home = false;
            App_SetState(APP_STATE_DONE);
            Screen_ShowMessage("DONE");
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    default:
        break;
    }
}

void App_Init(void)
{
    /* 初始化顺序：先启动 PWM，再停泵，再打开串口协议，最后刷新屏幕。 */
    Motor_Init();
    Pump_Init();
    Protocol_Init();
    Screen_Init();

    app_state = APP_STATE_IDLE;
    app_alarm = APP_ALARM_NONE;
    app_pump_speed_percent = Pump_ClampSpeedPercent(APP_DEFAULT_PUMP_SPEED_PERCENT);
    app_last_screen_tick = 0U;
    App_AllStop();
    App_ReportStatus(true);
}

void App_Task(void)
{
    /* 主循环任务入口，保持非阻塞，确保串口命令和安全状态能及时响应。 */
    Protocol_Process();
    App_ProcessCommands();

    if (app_state == APP_STATE_MANUAL) {
        App_TaskManual();
    } else {
        App_TaskAuto();
    }

    App_ReportStatus(false);
}

App_State App_GetState(void)
{
    return app_state;
}

uint8_t App_GetPumpSpeedPercent(void)
{
    return app_pump_speed_percent;
}

uint16_t App_GetPGMask(void)
{
    return PG_ReadMask();
}
