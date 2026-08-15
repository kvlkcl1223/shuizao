#include "app.h"
#include "app_config.h"
#include "logger.h"
#include "motor.h"
#include "pg.h"
#include "protocol.h"
#include "pump.h"
#include "screen.h"
#include "tim.h"
#include <stdbool.h>

/*
 * app.c
 * 主业务状态机实现。
 * 自动流程由体积档位表驱动：分阶段吸取、三段喷淋、可选人工预留 10ml，流程结束后回原点。
 */

#define APP_TIME_MIN_MS 100U
#define APP_TIME_MAX_MS 600000U
#define APP_ALL_PUMP_MASK ((uint8_t)((1U << APP_PUMP_COUNT) - 1U))

static App_State app_state = APP_STATE_IDLE;
static App_Alarm app_alarm = APP_ALARM_NONE;

/* 当前自动任务参数。预留 10ml 只作为流程后的人工确认步骤，不再依赖 10ml 光电位。 */
static uint8_t app_keep10 = 0U;
static uint16_t app_manual_reserved_ml = 0U;
static uint8_t app_pump_speed_percent = APP_DEFAULT_PUMP_SPEED_PERCENT;

/* HMI 可临时修改的工艺时间；不写入 Flash，断电或复位后恢复默认值。 */
static uint32_t app_aspirate_phase_ms = APP_ASPIRATE_PHASE_MS;
static uint32_t app_trim10_ms = APP_TRIM_10ML_MS;

/* START 后按体积档位表生成本次自动流程的吸取和喷淋目标。 */
static PG_ID app_aspirate_sequence[APP_MAX_AUTO_PHASES];
static uint8_t app_aspirate_phase_count = 0U;
static PG_ID app_spray_sequence[APP_SPRAY_STAGE_COUNT];
static uint8_t app_spray_phase_count = 0U;
static uint8_t app_has_trim10 = 0U;
static uint8_t app_spray_active_pump_mask = 0U;

/* 阶段索引用于吸取序列和喷淋序列，切换大阶段时会重新置零。 */
static uint8_t app_phase_index = 0U;
static PG_ID app_target_pg = PG_INVALID;
static uint32_t app_state_start_tick = 0U;
static uint32_t app_last_screen_tick = 0U;

/* START 命令会先回原点，回原点完成后根据此标志继续自动流程。 */
static bool app_auto_after_home = false;

/* RETURN_HOME 可来自自动完成或 STOP 取消，完成路径才会进入人工补加确认。 */
static bool app_return_after_success = false;

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

static uint32_t App_ClampProcessTimeMs(uint32_t time_ms)
{
    if (time_ms < APP_TIME_MIN_MS) {
        return APP_TIME_MIN_MS;
    }
    if (time_ms > APP_TIME_MAX_MS) {
        return APP_TIME_MAX_MS;
    }
    return time_ms;
}

static void App_SetState(App_State state)
{
    /* 所有状态切换统一更新时间戳，便于超时和进度计算。 */
    if (app_state != state) {
        Logger_State((uint8_t)state);
    }
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
           app_state == APP_STATE_TRIM_ASPIRATING ||
           app_state == APP_STATE_MOVE_TO_SPRAY ||
           app_state == APP_STATE_SPRAYING ||
           app_state == APP_STATE_RETURN_HOME ||
           app_state == APP_STATE_WAIT_MANUAL_CUP_CLEAN ||
           app_state == APP_STATE_POWER_ON_RESET;
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
    case APP_STATE_TRIM_ASPIRATING:
        return "TRIM 10ML";
    case APP_STATE_MOVE_TO_SPRAY:
        return "MOVE SPRAY";
    case APP_STATE_SPRAYING:
        /* 三段喷淋共用内置 6 泵补偿时间；每个泵按各自时间停止。 */
        return "SPRAY";
    case APP_STATE_RETURN_HOME:
        return "RETURN";
    case APP_STATE_WAIT_MANUAL_CUP_CLEAN:
        return "ADD 10ML";
    case APP_STATE_DONE:
        return "DONE";
    case APP_STATE_ERROR:
        return "ERROR";
    case APP_STATE_ESTOP:
        return "ESTOP";
    case APP_STATE_MANUAL:
        return "MANUAL";
    case APP_STATE_POWER_ON_RESET:
        return "PWR HOME";
    default:
        return "UNKNOWN";
    }
}

static int8_t App_FindVolumeIndex(uint16_t volume_ml)
{
    for (uint8_t i = 0U; i < APP_VOLUME_POSITION_COUNT; i++) {
        if (APP_VOLUME_POSITIONS[i].volume_ml == volume_ml) {
            return (int8_t)i;
        }
    }

    return -1;
}

static int8_t App_FindBaseVolumeIndex(uint16_t machine_volume_ml, uint16_t *trim_ml)
{
    int8_t best_index = -1;
    uint16_t best_delta = 0xFFFFU;

    /*
     * 预留 10ml 时，机器目标体积可能落在两个传感器之间。
     * 例如用户选 100ml 且预留 10ml，机器先按 100ml 定位，再按时间补吸约 10ml。
     */
    for (uint8_t i = 0U; i < APP_VOLUME_POSITION_COUNT; i++) {
        uint16_t table_volume = APP_VOLUME_POSITIONS[i].volume_ml;
        if (table_volume >= machine_volume_ml) {
            uint16_t delta = (uint16_t)(table_volume - machine_volume_ml);
            if (delta < best_delta) {
                best_delta = delta;
                best_index = (int8_t)i;
            }
        }
    }

    if (best_index < 0) {
        return -1;
    }

    if (best_delta != 0U && best_delta != APP_MANUAL_RESERVED_VOLUME_ML) {
        return -1;
    }

    if (trim_ml != 0) {
        *trim_ml = best_delta;
    }
    return best_index;
}

static bool App_BuildAutoPlan(uint16_t volume_ml, uint8_t keep10)
{
    uint16_t reserved_ml = keep10 ? APP_MANUAL_RESERVED_VOLUME_ML : 0U;
    uint16_t machine_volume_ml;
    uint16_t trim_ml = 0U;
    int8_t requested_index;
    int8_t base_index;
    int8_t spray2_index;
    int8_t spray3_index;

    requested_index = App_FindVolumeIndex(volume_ml);
    if (requested_index < 0 || volume_ml <= reserved_ml) {
        return false;
    }

    machine_volume_ml = (uint16_t)(volume_ml - reserved_ml);
    base_index = App_FindBaseVolumeIndex(machine_volume_ml, &trim_ml);
    spray2_index = App_FindVolumeIndex(APP_SPRAY_FIXED_VOLUME_STAGE2_ML);
    spray3_index = App_FindVolumeIndex(APP_SPRAY_FIXED_VOLUME_STAGE3_ML);
    if (base_index < 0 || spray2_index < 0 || spray3_index < 0) {
        return false;
    }

    app_aspirate_phase_count = 0U;
    for (uint8_t i = 0U; i <= (uint8_t)base_index; i++) {
        PG_ID pg = APP_VOLUME_POSITIONS[i].aspirate_pg;
        if (app_aspirate_phase_count >= APP_MAX_AUTO_PHASES || !PG_IsValid(pg)) {
            return false;
        }
        app_aspirate_sequence[app_aspirate_phase_count++] = pg;
    }

    app_spray_phase_count = APP_SPRAY_STAGE_COUNT;
    app_spray_sequence[0] = APP_VOLUME_POSITIONS[(uint8_t)base_index].first_spray_pg;
    app_spray_sequence[1] = APP_VOLUME_POSITIONS[(uint8_t)spray2_index].aspirate_pg;
    app_spray_sequence[2] = APP_VOLUME_POSITIONS[(uint8_t)spray3_index].aspirate_pg;
    for (uint8_t i = 0U; i < app_spray_phase_count; i++) {
        if (!PG_IsValid(app_spray_sequence[i])) {
            return false;
        }
    }

    app_has_trim10 = trim_ml ? 1U : 0U;
    app_manual_reserved_ml = reserved_ml;
    app_keep10 = keep10 ? 1U : 0U;
    return app_aspirate_phase_count > 0U;
}

static void App_AllStop(void)
{
    /* 紧急停机共用出口：停全部泵，Z 轴刹车，并清手动动作记录。 */
    Pump_StopAll();
    Motor_Brake(APP_Z_MOTOR_ID);
    app_spray_active_pump_mask = 0U;
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
    Logger_Value("MOVE", "target_pg", PG_ToNumber(target));
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
    app_return_after_success = false;
    App_AllStop();
    Logger_Alarm((uint16_t)alarm);
    Screen_ShowAlarm((uint16_t)alarm);
    Screen_ShowMessage("ERROR");
    App_SetState(APP_STATE_ERROR);
}

static uint8_t App_DisplayPhase(void)
{
    if (app_state == APP_STATE_MOVE_TO_ASPIRATE ||
        app_state == APP_STATE_ASPIRATING ||
        app_state == APP_STATE_MOVE_TO_SPRAY ||
        app_state == APP_STATE_SPRAYING) {
        return (uint8_t)(app_phase_index + 1U);
    }

    if (app_state == APP_STATE_TRIM_ASPIRATING) {
        return (uint8_t)(app_aspirate_phase_count + 1U);
    }

    return 0U;
}

static uint32_t App_GetSprayMaxMs(void)
{
    uint32_t max_ms = 0U;

    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        if (APP_SPRAY_PUMP_MS[i] > max_ms) {
            max_ms = APP_SPRAY_PUMP_MS[i];
        }
    }

    return max_ms;
}

static uint8_t App_ProgressPercent(void)
{
    uint32_t duration = 0U;
    uint32_t elapsed;

    if (app_state == APP_STATE_ASPIRATING) {
        duration = app_aspirate_phase_ms;
    } else if (app_state == APP_STATE_TRIM_ASPIRATING) {
        duration = app_trim10_ms;
    } else if (app_state == APP_STATE_SPRAYING && app_phase_index < app_spray_phase_count) {
        duration = App_GetSprayMaxMs();
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
                        App_DisplayPhase(),
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
        Logger_Info("AUTO", "start rejected busy");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (App_FindVolumeIndex(volume_ml) < 0) {
        app_alarm = APP_ALARM_BAD_VOLUME;
        Logger_Info("AUTO", "start rejected bad volume");
        Screen_ShowMessage("BAD VOL");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (!App_BuildAutoPlan(volume_ml, keep10)) {
        app_alarm = APP_ALARM_BAD_CONFIG;
        Logger_Info("AUTO", "start rejected bad config");
        Screen_ShowMessage("BAD CFG");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    /* 每次启动都先回原点，再检查 Y 轴并进入吸取阶段。 */
    app_alarm = APP_ALARM_NONE;
    app_phase_index = 0U;
    app_auto_after_home = true;
    app_return_after_success = false;
    App_AllStop();
    Logger_Value("AUTO", "volume", volume_ml);
    Logger_Value("AUTO", "keep10", app_keep10);
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_HOME_PG);
    App_ReportStatus(true);
}

static void App_StartHome(bool continue_auto)
{
    /* HOME 可单独执行，也可作为 START 的前置动作。 */
    app_alarm = APP_ALARM_NONE;
    app_auto_after_home = continue_auto;
    app_return_after_success = false;
    app_phase_index = 0U;
    Pump_StopAll();
    Logger_Value("HOME", "continue_auto", continue_auto ? 1U : 0U);
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_HOME_PG);
}

static void App_HandleManual(const Protocol_Command *command)
{
    uint8_t speed = command->speed_percent;

    /* 自动流程运行中禁止手动动作，防止状态机和手动控制抢执行机构。 */
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("MAN", "rejected busy");
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
            Logger_Value("MAN", "z_up_speed", speed);
            Motor_Run(APP_Z_MOTOR_ID, APP_Z_UP_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_DOWN) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_DOWN;
            Logger_Value("MAN", "z_down_speed", speed);
            Motor_Run(APP_Z_MOTOR_ID, APP_Z_DOWN_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            Logger_Info("MAN", "z stop");
            if (app_manual_pump_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    } else if (command->manual_target == PROTOCOL_MANUAL_TARGET_PUMP) {
        if (command->manual_action == PROTOCOL_MANUAL_ACTION_IN) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_IN;
            Logger_Value("MAN", "pump_in_speed", speed);
            Pump_RunAll(PUMP_DIR_IN, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_OUT) {
            Motor_Brake(APP_Z_MOTOR_ID);
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_OUT;
            Logger_Value("MAN", "pump_out_speed", speed);
            Pump_RunAll(PUMP_DIR_OUT, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            Logger_Info("MAN", "pump stop");
            if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    }

    App_ReportStatus(true);
}

static void App_HandleSetParam(const Protocol_Command *command)
{
    uint32_t value;

    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SET", "rejected busy");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    value = App_ClampProcessTimeMs(command->param_value);

    switch (command->param_target) {
    case PROTOCOL_PARAM_ASPIRATE_MS:
        app_aspirate_phase_ms = value;
        Logger_Value("SET", "asp_ms", value);
        break;
    case PROTOCOL_PARAM_TRIM10_MS:
        app_trim10_ms = value;
        Logger_Value("SET", "trim10_ms", value);
        break;
    default:
        app_alarm = APP_ALARM_BAD_COMMAND;
        Screen_ShowMessage("BAD CMD");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    app_alarm = APP_ALARM_NONE;
    Screen_ShowMessage("SET OK");
    App_ReportStatus(true);
}

static void App_HandleCommand(const Protocol_Command *command)
{
    /* 协议层只负责解析，这里才真正执行命令对应的业务动作。 */
    Logger_Command(command);

    switch (command->type) {
    case PROTOCOL_CMD_START:
        App_StartAuto(command->volume_ml, command->keep10);
        break;

    case PROTOCOL_CMD_STOP:
        App_AllStop();
        app_auto_after_home = false;
        app_return_after_success = false;
        Logger_Info("CMD", "stop handled");
        /* STOP 在自动流程中按“停止后回原点”处理；人工补加等待中则直接取消。 */
        if (app_state == APP_STATE_POWER_ON_RESET) {
            App_EnterMoveState(APP_STATE_POWER_ON_RESET, APP_Z_HOME_PG);
        } else if (app_state == APP_STATE_WAIT_MANUAL_CUP_CLEAN) {
            app_manual_reserved_ml = 0U;
            App_SetState(APP_STATE_IDLE);
        } else if (App_IsAutoRunning()) {
            App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_HOME_PG);
        } else {
            App_SetState(APP_STATE_IDLE);
        }
        break;

    case PROTOCOL_CMD_ESTOP:
        /* ESTOP 立即停机，不自动回原点。 */
        App_AllStop();
        app_auto_after_home = false;
        app_return_after_success = false;
        app_alarm = APP_ALARM_NONE;
        Logger_Info("CMD", "estop handled");
        App_SetState(APP_STATE_ESTOP);
        Screen_ShowMessage("ESTOP");
        break;

    case PROTOCOL_CMD_HOME:
        App_StartHome(false);
        break;

    case PROTOCOL_CMD_OK:
        /* 人工用 10ml 清洗接液烧杯并补加完成后，屏幕发送 #OK; 确认整套流程完成。 */
        if (app_state == APP_STATE_WAIT_MANUAL_CUP_CLEAN) {
            app_manual_reserved_ml = 0U;
            App_SetState(APP_STATE_DONE);
            Screen_ShowMessage("DONE");
            Logger_Info("AUTO", "manual 10ml confirmed");
            App_ReportStatus(true);
        }
        break;

    case PROTOCOL_CMD_SPEED_SET:
        app_pump_speed_percent = Pump_ClampSpeedPercent(command->speed_percent);
        app_alarm = APP_ALARM_NONE;
        Logger_Value("SET", "speed", app_pump_speed_percent);
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_SET_PARAM:
        App_HandleSetParam(command);
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

static void App_StartSprayPhaseZero(void)
{
    app_phase_index = 0U;
    Logger_Info("SPRAY", "start sequence");
    App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY, app_spray_sequence[app_phase_index]);
}

static void App_StartSprayPumps(void)
{
    App_SetState(APP_STATE_SPRAYING);
    app_spray_active_pump_mask = 0U;
    Logger_Value("SPRAY", "stage", (uint32_t)app_phase_index + 1U);
    Logger_Value("SPRAY", "target_pg", PG_ToNumber(app_spray_sequence[app_phase_index]));

    /* 三段喷淋共用同一套 6 泵补偿时间：同开，按各自时间分别停止。 */
    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        if (APP_SPRAY_PUMP_MS[i] > 0U) {
            Pump_RunOne(i, PUMP_DIR_OUT, app_pump_speed_percent);
            app_spray_active_pump_mask |= (uint8_t)(1U << i);
        } else {
            Pump_StopOne(i);
        }
    }

    app_spray_active_pump_mask &= APP_ALL_PUMP_MASK;
}

static void App_AdvanceSprayPhase(void)
{
    Pump_StopAll();
    app_spray_active_pump_mask = 0U;
    Logger_Value("SPRAY", "stage_done", (uint32_t)app_phase_index + 1U);
    app_phase_index++;

    if (app_phase_index < app_spray_phase_count) {
        App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY,
                           app_spray_sequence[app_phase_index]);
    } else {
        /* 自动喷淋结束后必须回原点。 */
        app_return_after_success = true;
        Logger_Info("SPRAY", "sequence done");
        App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_HOME_PG);
    }
}

static void App_TaskSpraying(void)
{
    uint32_t elapsed = Elapsed(app_state_start_tick);

    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        uint8_t pump_bit = (uint8_t)(1U << i);
        if ((app_spray_active_pump_mask & pump_bit) != 0U &&
            elapsed >= APP_SPRAY_PUMP_MS[i]) {
            Pump_StopOne(i);
            app_spray_active_pump_mask &= (uint8_t)(~pump_bit);
            Logger_Value("SPRAY", "pump_stop", (uint32_t)i + 1U);
        }
    }

    if (app_spray_active_pump_mask == 0U) {
        App_AdvanceSprayPhase();
    }
}

static void App_TaskAuto(void)
{
    /* 自动流程状态机：所有等待都用 HAL_GetTick 计时，避免长阻塞延时。 */
    switch (app_state) {
    case APP_STATE_HOMING:
        if (App_TargetReached()) {
            Logger_Info("HOME", "reached");
            if (app_auto_after_home) {
                App_SetState(APP_STATE_CHECK_Y);
            } else {
                App_SetState(APP_STATE_IDLE);
            }
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_POWER_ON_RESET:
        if (App_TargetReached()) {
            app_auto_after_home = false;
            app_return_after_success = false;
            App_AllStop();
            Logger_Info("BOOT", "power home reached");
            App_SetState(APP_STATE_IDLE);
            Screen_ShowMessage("READY");
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_CHECK_Y:
        /* 默认 PG1 为 Y 轴允许工作位置，低电平有效。 */
        if (PG_IsActive(APP_Y_READY_PG)) {
            app_phase_index = 0U;
            Logger_Info("AUTO", "y ready");
            App_EnterMoveState(APP_STATE_MOVE_TO_ASPIRATE,
                               app_aspirate_sequence[app_phase_index]);
        } else {
            Logger_Info("AUTO", "y not ready");
            App_Fail(APP_ALARM_Y_NOT_READY);
        }
        break;

    case APP_STATE_MOVE_TO_ASPIRATE:
        if (App_TargetReached()) {
            Logger_Value("ASP", "stage", (uint32_t)app_phase_index + 1U);
            Logger_Value("ASP", "target_pg", PG_ToNumber(app_aspirate_sequence[app_phase_index]));
            Pump_RunAll(PUMP_DIR_IN, app_pump_speed_percent);
            App_SetState(APP_STATE_ASPIRATING);
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_ASPIRATING:
        /* 每个吸取阶段使用同一固定时间、同一全局泵速。 */
        if (Elapsed(app_state_start_tick) >= app_aspirate_phase_ms) {
            Pump_StopAll();
            Logger_Value("ASP", "stage_done", (uint32_t)app_phase_index + 1U);
            app_phase_index++;
            if (app_phase_index < app_aspirate_phase_count) {
                App_EnterMoveState(APP_STATE_MOVE_TO_ASPIRATE,
                                   app_aspirate_sequence[app_phase_index]);
            } else if (app_has_trim10) {
                /* 目标体积减 10ml 时没有独立 PG，当前以定时补吸作为占位实现。 */
                Logger_Info("ASP", "trim10 start");
                Pump_RunAll(PUMP_DIR_IN, app_pump_speed_percent);
                App_SetState(APP_STATE_TRIM_ASPIRATING);
            } else {
                App_StartSprayPhaseZero();
            }
        }
        break;

    case APP_STATE_TRIM_ASPIRATING:
        if (Elapsed(app_state_start_tick) >= app_trim10_ms) {
            Pump_StopAll();
            Logger_Info("ASP", "trim10 done");
            App_StartSprayPhaseZero();
        }
        break;

    case APP_STATE_MOVE_TO_SPRAY:
        if (App_TargetReached()) {
            App_StartSprayPumps();
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_SPRAYING:
        /* 三段喷淋共用内置 6 泵补偿时间。 */
        App_TaskSpraying();
        break;

    case APP_STATE_RETURN_HOME:
        if (App_TargetReached()) {
            App_AllStop();
            app_auto_after_home = false;
            Logger_Info("HOME", "return reached");
            if (app_return_after_success && app_manual_reserved_ml > 0U) {
                app_return_after_success = false;
                Screen_ShowMessage("ADD 10ML");
                Logger_Info("AUTO", "wait manual 10ml");
                App_SetState(APP_STATE_WAIT_MANUAL_CUP_CLEAN);
            } else if (app_return_after_success) {
                app_return_after_success = false;
                App_SetState(APP_STATE_DONE);
                Screen_ShowMessage("DONE");
                Logger_Info("AUTO", "done");
            } else {
                App_SetState(APP_STATE_IDLE);
            }
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_WAIT_MANUAL_CUP_CLEAN:
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
    Logger_Init();

    if (HAL_TIM_Base_Start_IT(&htim5) == HAL_OK) {
        Logger_Info("BOOT", "tim5 heartbeat start");
    } else {
        Logger_Info("BOOT", "tim5 heartbeat start failed");
    }

    app_state = APP_STATE_IDLE;
    app_alarm = APP_ALARM_NONE;
    app_pump_speed_percent = Pump_ClampSpeedPercent(APP_DEFAULT_PUMP_SPEED_PERCENT);
    app_aspirate_phase_ms = App_ClampProcessTimeMs(APP_ASPIRATE_PHASE_MS);
    app_trim10_ms = App_ClampProcessTimeMs(APP_TRIM_10ML_MS);
    app_last_screen_tick = 0U;
    app_auto_after_home = false;
    app_return_after_success = false;
    app_phase_index = 0U;
    App_AllStop();

#if APP_POWER_ON_RESET_ENABLE
    /* 上电后先复位到最高点。最高点由 APP_Z_HOME_PG 配置，当前默认 PG3。 */
    Logger_Info("BOOT", "power home start");
    App_EnterMoveState(APP_STATE_POWER_ON_RESET, APP_Z_HOME_PG);
#else
    App_SetState(APP_STATE_IDLE);
#endif

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
