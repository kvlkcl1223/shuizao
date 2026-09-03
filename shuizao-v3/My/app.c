#include "app.h"
#include "app_config.h"
#include "logger.h"
#include "motor.h"
#include "pg.h"
#include "protocol.h"
#include "pump.h"
#include "screen.h"
#include "settings.h"
#include "tim.h"
#include <stdbool.h>

/*
 * app.c
 * 主业务状态机实现。
 * 自动流程由体积档位表驱动：分阶段吸取、三段喷淋、可选人工预留 10ml，流程结束后回原点。
 */

#define APP_ALL_PUMP_MASK ((uint8_t)((1U << APP_PUMP_COUNT) - 1U))

typedef enum {
    APP_POWER_RESET_PHASE_NONE = 0,
    APP_POWER_RESET_PHASE_DOWN,
    APP_POWER_RESET_PHASE_UP,
    APP_POWER_RESET_PHASE_WAIT_USER_FIX
} App_PowerResetPhase;

typedef enum {
    APP_POWER_RESET_WAIT_NONE = 0,
    APP_POWER_RESET_WAIT_Y_READY,
    APP_POWER_RESET_WAIT_Z_HOME
} App_PowerResetWaitReason;

static App_State app_state = APP_STATE_IDLE;
static App_Alarm app_alarm = APP_ALARM_NONE;

/* 当前自动任务参数。预留 10ml 只作为流程后的人工确认步骤，不再依赖 10ml 光电位。 */
static uint8_t app_keep10 = 0U;
static uint16_t app_manual_reserved_ml = 0U;
static uint8_t app_aspirate_speed_percent = APP_DEFAULT_PUMP_SPEED_PERCENT;
static uint8_t app_manual_pump_speed_percent = APP_DEFAULT_PUMP_SPEED_PERCENT;

/* HMI 可临时修改的工艺时间；吸取和 10ml 补吸不写入 Flash，断电或复位后恢复默认值。 */
static uint32_t app_aspirate_phase_ms = APP_ASPIRATE_PHASE_MS;
static uint32_t app_trim10_ms = APP_TRIM_10ML_MS;

/* 4 个目标体积各自保存第一段 6 个泵的喷淋补偿时间，下标：档位 0~3，泵 0~5。 */
static uint32_t app_spray_profile_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT];
static uint8_t app_spray_active_profile = 0U;

/* 4 个 Z 轴虚拟位置时间：HOME->800、800->300、300->800、200->300。 */
static uint32_t app_zvirt_ms[APP_ZVIRT_COUNT];

/* START 后按体积档位表生成本次自动流程的吸取和喷淋目标。 */
static App_ZPosition app_aspirate_sequence[APP_MAX_AUTO_PHASES];
static uint32_t app_aspirate_dwell_sequence[APP_MAX_AUTO_PHASES];
static uint8_t app_aspirate_move_pump_sequence[APP_MAX_AUTO_PHASES];
static uint8_t app_aspirate_dwell_pump_sequence[APP_MAX_AUTO_PHASES];
static uint8_t app_aspirate_phase_count = 0U;
static App_ZPosition app_spray_sequence[APP_SPRAY_STAGE_COUNT];
static uint8_t app_spray_phase_count = 0U;
static uint8_t app_has_trim10 = 0U;
static uint8_t app_spray_active_pump_mask = 0U;

/* 阶段索引用于吸取序列和喷淋序列，切换大阶段时会重新置零。 */
static uint8_t app_phase_index = 0U;
static App_ZPosition app_current_pos = APP_Z_POS_INVALID;
static App_ZPosition app_target_pos = APP_Z_POS_INVALID;
static App_ZPosition app_step_target_pos = APP_Z_POS_INVALID;
static uint8_t app_step_direction = MOTOR_STOP;
static uint32_t app_step_start_tick = 0U;
static uint32_t app_step_duration_ms = 0U;
static uint32_t app_state_start_tick = 0U;
static uint32_t app_last_screen_tick = 0U;
static uint32_t app_last_leak_check_tick = 0U;
static uint32_t app_last_leak_log_tick = 0U;
static uint8_t app_leak_abnormal_count = 0U;
static bool app_leak_last_normal = true;
static bool app_leak_fault_latched = false;

/* 上电复位分为“先下行”和“再上找 PG3”两个子阶段。 */
static App_PowerResetPhase app_power_reset_phase = APP_POWER_RESET_PHASE_NONE;
static App_PowerResetWaitReason app_power_reset_wait_reason = APP_POWER_RESET_WAIT_NONE;
static uint32_t app_power_reset_start_tick = 0U;

/*
 * Z 轴反向保护。
 * 记录上一次真实启动方向；当下一次命令方向相反时，先空档停顿，再启动新方向。
 */
static uint8_t app_z_last_direction = MOTOR_STOP;
static uint8_t app_z_pending_direction = MOTOR_STOP;
static uint16_t app_z_pending_speed = 0U;
static bool app_z_reverse_deadtime_active = false;
static uint32_t app_z_reverse_deadtime_start_tick = 0U;

/* START 命令会先回原点，回原点完成后根据此标志继续自动流程。 */
static bool app_auto_after_home = false;

/* RETURN_HOME 可来自自动完成或 STOP 取消，完成路径才会进入人工补加确认。 */
static bool app_return_after_success = false;

/* 手动动作记录，用于到达 PG3/PG7 后自动停止。 */
static Protocol_ManualAction app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
static Protocol_ManualAction app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;

static uint32_t Now(void)
{
    return HAL_GetTick();
}

static void App_Fail(App_Alarm alarm);
static void App_EnterAspirateMove(uint8_t phase_index);
static void App_TaskLeakDetect(void);
static void App_PausePowerResetForUser(App_PowerResetWaitReason reason);
static void App_StartPowerResetDown(void);
static void App_ReportStatus(bool force);

static uint32_t Elapsed(uint32_t start_tick)
{
    return Now() - start_tick;
}

static bool App_ZPosIsValid(App_ZPosition pos)
{
    return pos < APP_Z_POSITION_COUNT;
}

static uint16_t App_ZPosVolumeMl(App_ZPosition pos)
{
    switch (pos) {
    case APP_Z_POS_800ML:
        return 800U;
    case APP_Z_POS_700ML:
        return 700U;
    case APP_Z_POS_600ML:
        return 600U;
    case APP_Z_POS_500ML:
        return 500U;
    case APP_Z_POS_400ML:
        return 400U;
    case APP_Z_POS_300ML:
        return 300U;
    case APP_Z_POS_200ML:
        return 200U;
    case APP_Z_POS_150ML:
        return 150U;
    case APP_Z_POS_100ML:
        return 100U;
    case APP_Z_POS_50ML:
        return 50U;
    default:
        return 0U;
    }
}

static PG_ID App_ZPosSensorPG(App_ZPosition pos)
{
    switch (pos) {
    case APP_Z_POS_HOME:
        return APP_Z_HOME_PG;
    case APP_Z_POS_200ML:
        return APP_Z_200ML_PG;
    case APP_Z_POS_150ML:
        return APP_Z_150ML_PG;
    case APP_Z_POS_100ML:
        return APP_Z_100ML_PG;
    case APP_Z_POS_50ML:
        return APP_Z_50ML_PG;
    default:
        return PG_INVALID;
    }
}

static bool App_ZPosHasSensor(App_ZPosition pos)
{
    return PG_IsValid(App_ZPosSensorPG(pos));
}

static bool App_ZPosSensorActive(App_ZPosition pos)
{
    PG_ID pg = App_ZPosSensorPG(pos);

    if (!PG_IsValid(pg)) {
        return false;
    }

    return PG_IsActive(pg);
}

static uint32_t App_ZStepDurationMs(App_ZPosition from, App_ZPosition to)
{
    uint8_t index;

    if (!App_ZPosIsValid(from) || !App_ZPosIsValid(to)) {
        return APP_Z_MOVE_TIMEOUT_MS;
    }

    if ((uint8_t)to == ((uint8_t)from + 1U)) {
        index = (uint8_t)from;
        if (from == APP_Z_POS_HOME && to == APP_Z_POS_800ML) {
            return app_zvirt_ms[0];
        }
        if (from == APP_Z_POS_800ML && to == APP_Z_POS_300ML) {
            return app_zvirt_ms[1];
        }
        if (index < APP_Z_STEP_COUNT) {
            return APP_Z_STEP_DOWN_MS[index];
        }
    }

    if ((uint8_t)from == ((uint8_t)to + 1U)) {
        index = (uint8_t)to;
        if (from == APP_Z_POS_300ML && to == APP_Z_POS_800ML) {
            return app_zvirt_ms[2];
        }
        if (from == APP_Z_POS_200ML && to == APP_Z_POS_300ML) {
            return app_zvirt_ms[3];
        }
        if (index < APP_Z_STEP_COUNT) {
            return APP_Z_STEP_UP_MS[index];
        }
    }

    return APP_Z_MOVE_TIMEOUT_MS;
}

static uint32_t App_ClampProcessTimeMs(uint32_t time_ms)
{
#if APP_TIME_MIN_MS > 0U
    if (time_ms < APP_TIME_MIN_MS) {
        return APP_TIME_MIN_MS;
    }
#endif
    if (time_ms > APP_TIME_MAX_MS) {
        return APP_TIME_MAX_MS;
    }
    return time_ms;
}

static uint32_t App_ScaleAspirateDwellMs(uint32_t full_speed_ms)
{
    uint32_t speed_percent = Pump_ClampSpeedPercent(app_aspirate_speed_percent);
    uint64_t scaled_ms;

    if (full_speed_ms == 0U) {
        return 0U;
    }

    /*
     * APP_ASP_DWELL_xxx_MS 按 100% 满速标定。
     * 低速时用近似线性关系补偿：时间 = 满速时间 * 100 / 当前速度百分比。
     */
    scaled_ms = ((uint64_t)full_speed_ms * 100ULL + (uint64_t)speed_percent - 1ULL) /
                (uint64_t)speed_percent;

    if (scaled_ms > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFU;
    }

    return (uint32_t)scaled_ms;
}

static uint32_t App_ClampZVirtTimeMs(uint32_t time_ms)
{
    if (time_ms < APP_ZVIRT_TIME_MIN_MS) {
        return APP_ZVIRT_TIME_MIN_MS;
    }
    if (time_ms > APP_ZVIRT_TIME_MAX_MS) {
        return APP_ZVIRT_TIME_MAX_MS;
    }
    return time_ms;
}

static bool App_ZMoveDirectionBetween(App_ZPosition from, App_ZPosition to, uint8_t *direction)
{
    if (!App_ZPosIsValid(from) || !App_ZPosIsValid(to) || direction == 0) {
        return false;
    }

    if ((uint8_t)to > (uint8_t)from) {
        *direction = APP_Z_DOWN_DIRECTION;
        return true;
    }

    if ((uint8_t)to < (uint8_t)from) {
        *direction = APP_Z_UP_DIRECTION;
        return true;
    }

    return false;
}

static void App_LoadDefaultSprayTimes(void)
{
    for (uint8_t profile = 0U; profile < APP_SPRAY_PROFILE_COUNT; profile++) {
        for (uint8_t pump = 0U; pump < APP_PUMP_COUNT; pump++) {
            app_spray_profile_ms[profile][pump] = App_ClampProcessTimeMs(APP_SPRAY_PUMP_MS[pump]);
        }
    }
}

static void App_LoadDefaultZVirtTimes(void)
{
    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        app_zvirt_ms[i] = APP_ZVIRT_TIME_DEFAULT_MS;
    }
}

static int8_t App_FindSprayProfileIndex(uint16_t volume_ml)
{
    for (uint8_t i = 0U; i < APP_SPRAY_PROFILE_COUNT; i++) {
        if (APP_SPRAY_PROFILE_VOLUMES[i] == volume_ml) {
            return (int8_t)i;
        }
    }

    return -1;
}

static uint16_t App_SprayProfileVolume(uint8_t profile)
{
    if (profile >= APP_SPRAY_PROFILE_COUNT) {
        return 0U;
    }

    return APP_SPRAY_PROFILE_VOLUMES[profile];
}

static uint32_t *App_SprayMsForProfile(uint8_t profile)
{
    if (profile >= APP_SPRAY_PROFILE_COUNT) {
        return 0;
    }

    return app_spray_profile_ms[profile];
}

static uint32_t *App_ActiveSprayMs(void)
{
    return app_spray_profile_ms[app_spray_active_profile];
}

static const uint32_t *App_CurrentSprayMs(void)
{
    if (app_spray_active_profile >= APP_SPRAY_PROFILE_COUNT) {
        return App_ActiveSprayMs();
    }

    if (app_phase_index == 1U) {
        return APP_SPRAY_STAGE2_PUMP_MS[app_spray_active_profile];
    }

    if (app_phase_index == 2U) {
        return APP_SPRAY_STAGE3_PUMP_MS[app_spray_active_profile];
    }

    return App_ActiveSprayMs();
}

static int8_t App_ZVirtParamIndex(Protocol_ParamTarget target)
{
    switch (target) {
    case PROTOCOL_PARAM_Z_DN_HOME_800_MS:
        return 0;
    case PROTOCOL_PARAM_Z_DN_800_300_MS:
        return 1;
    case PROTOCOL_PARAM_Z_UP_300_800_MS:
        return 2;
    case PROTOCOL_PARAM_Z_UP_200_300_MS:
        return 3;
    default:
        return -1;
    }
}

static int8_t App_SprayParamIndex(Protocol_ParamTarget target)
{
    switch (target) {
    case PROTOCOL_PARAM_SPRAY1_MS:
        return 0;
    case PROTOCOL_PARAM_SPRAY2_MS:
        return 1;
    case PROTOCOL_PARAM_SPRAY3_MS:
        return 2;
    case PROTOCOL_PARAM_SPRAY4_MS:
        return 3;
    case PROTOCOL_PARAM_SPRAY5_MS:
        return 4;
    case PROTOCOL_PARAM_SPRAY6_MS:
        return 5;
    default:
        return -1;
    }
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
        /* 当前喷淋段使用对应的 6 泵补偿时间；每个泵按各自时间停止。 */
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
    int8_t spray_profile_index;
    uint16_t target_stage_volume_ml;

    requested_index = App_FindVolumeIndex(volume_ml);
    if (requested_index < 0 || volume_ml <= reserved_ml) {
        return false;
    }

    spray_profile_index = App_FindSprayProfileIndex(volume_ml);
    if (spray_profile_index < 0) {
        return false;
    }

    machine_volume_ml = (uint16_t)(volume_ml - reserved_ml);
    base_index = App_FindBaseVolumeIndex(machine_volume_ml, &trim_ml);
    if (base_index < 0) {
        return false;
    }

    app_aspirate_phase_count = 0U;
    target_stage_volume_ml = APP_VOLUME_POSITIONS[(uint8_t)base_index].volume_ml;
    for (uint8_t i = 0U; i < APP_ASPIRATE_STAGE_COUNT; i++) {
        const App_AspirateStage *stage = &APP_ASPIRATE_STAGES[i];

        if (stage->volume_ml < target_stage_volume_ml) {
            continue;
        }

        if (app_aspirate_phase_count >= APP_MAX_AUTO_PHASES || !App_ZPosIsValid(stage->pos)) {
            return false;
        }

        app_aspirate_sequence[app_aspirate_phase_count] = stage->pos;
        app_aspirate_dwell_sequence[app_aspirate_phase_count] =
            App_ClampProcessTimeMs(App_ScaleAspirateDwellMs(stage->dwell_ms));
        app_aspirate_move_pump_sequence[app_aspirate_phase_count] = stage->pump_during_move ? 1U : 0U;
        app_aspirate_dwell_pump_sequence[app_aspirate_phase_count] = stage->pump_during_dwell ? 1U : 0U;
        app_aspirate_phase_count++;
    }

    app_spray_phase_count = APP_SPRAY_STAGE_COUNT;
    app_spray_sequence[0] = APP_VOLUME_POSITIONS[(uint8_t)base_index].first_spray_pos;
    app_spray_sequence[1] = APP_SPRAY_FIXED_STAGE2_POS;
    app_spray_sequence[2] = APP_SPRAY_FIXED_STAGE3_POS;
    for (uint8_t i = 0U; i < app_spray_phase_count; i++) {
        if (!App_ZPosIsValid(app_spray_sequence[i])) {
            return false;
        }
    }

    app_has_trim10 = trim_ml ? 1U : 0U;
    app_manual_reserved_ml = reserved_ml;
    app_keep10 = keep10 ? 1U : 0U;
    app_spray_active_profile = (uint8_t)spray_profile_index;
    return app_aspirate_phase_count > 0U;
}

static bool App_ZDirectionIsRun(uint8_t direction)
{
    return direction == APP_Z_UP_DIRECTION || direction == APP_Z_DOWN_DIRECTION;
}

static bool App_ZDirectionIsReverse(uint8_t first, uint8_t second)
{
    return (first == APP_Z_UP_DIRECTION && second == APP_Z_DOWN_DIRECTION) ||
           (first == APP_Z_DOWN_DIRECTION && second == APP_Z_UP_DIRECTION);
}

static bool App_IsYReadyForZMotion(void)
{
    return PG_IsActive(APP_Y_READY_PG);
}

static bool App_CheckYReadyForZMotion(void)
{
    if (App_IsYReadyForZMotion()) {
        return true;
    }

    Logger_Info("SAFE", "y_not_ready_during_z_motion");
    if (app_state == APP_STATE_POWER_ON_RESET) {
        App_PausePowerResetForUser(APP_POWER_RESET_WAIT_Y_READY);
        return false;
    }

    App_Fail(APP_ALARM_Y_NOT_READY);
    Screen_ShowWarningPage();
    return false;
}

static void App_ZCancelPendingDrive(void)
{
    app_z_reverse_deadtime_active = false;
    app_z_pending_direction = MOTOR_STOP;
    app_z_pending_speed = 0U;
}

static void App_ZBrake(void)
{
    App_ZCancelPendingDrive();
    Motor_Brake(APP_Z_MOTOR_ID);
}

static bool App_ZStartDrive(uint8_t direction, uint16_t speed)
{
    if (!App_ZDirectionIsRun(direction) || speed == 0U) {
        App_ZBrake();
        return false;
    }

    if (!App_CheckYReadyForZMotion()) {
        return false;
    }

    if (app_z_reverse_deadtime_active) {
        app_z_pending_direction = direction;
        app_z_pending_speed = speed;
        return false;
    }

    if (App_ZDirectionIsReverse(app_z_last_direction, direction)) {
        Motor_Coast(APP_Z_MOTOR_ID);
        app_z_pending_direction = direction;
        app_z_pending_speed = speed;
        app_z_reverse_deadtime_active = true;
        app_z_reverse_deadtime_start_tick = Now();
        Logger_Value("MOVE", "reverse_deadtime_ms", APP_Z_REVERSE_DEADTIME_MS);
        return false;
    }

    Motor_Run(APP_Z_MOTOR_ID, direction, speed);
    app_z_last_direction = direction;
    return true;
}

static bool App_ZDeadtimeTask(void)
{
    if (!app_z_reverse_deadtime_active) {
        return false;
    }

    if (Elapsed(app_z_reverse_deadtime_start_tick) < APP_Z_REVERSE_DEADTIME_MS) {
        return false;
    }

    if (!App_ZDirectionIsRun(app_z_pending_direction) || app_z_pending_speed == 0U) {
        App_ZCancelPendingDrive();
        return false;
    }

    if (!App_CheckYReadyForZMotion()) {
        return false;
    }

    Motor_Run(APP_Z_MOTOR_ID, app_z_pending_direction, app_z_pending_speed);
    app_z_last_direction = app_z_pending_direction;
    App_ZCancelPendingDrive();
    Logger_Info("MOVE", "reverse_deadtime_done");
    return true;
}

static void App_AllStop(void)
{
    /* 紧急停机共用出口：停全部泵，Z 轴刹车，并清手动动作记录。 */
    Pump_StopAll();
    App_ZBrake();
    app_spray_active_pump_mask = 0U;
    app_step_target_pos = APP_Z_POS_INVALID;
    app_step_direction = MOTOR_STOP;
    app_step_start_tick = 0U;
    app_step_duration_ms = 0U;
    app_power_reset_phase = APP_POWER_RESET_PHASE_NONE;
    app_power_reset_wait_reason = APP_POWER_RESET_WAIT_NONE;
    app_power_reset_start_tick = 0U;
    app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
    app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
}

static void App_PausePowerResetForUser(App_PowerResetWaitReason reason)
{
    App_Alarm alarm = APP_ALARM_Z_TIMEOUT;

    if (app_power_reset_phase == APP_POWER_RESET_PHASE_WAIT_USER_FIX) {
        return;
    }

    if (reason == APP_POWER_RESET_WAIT_Y_READY) {
        alarm = APP_ALARM_Y_NOT_READY;
    }

    App_AllStop();
    app_alarm = alarm;
    app_power_reset_phase = APP_POWER_RESET_PHASE_WAIT_USER_FIX;
    app_power_reset_wait_reason = reason;
    app_auto_after_home = false;
    app_return_after_success = false;
    App_SetState(APP_STATE_POWER_ON_RESET);
    Logger_Value("BOOT", "power_reset_wait_reason", (uint32_t)reason);
    Screen_ShowAlarm((uint16_t)alarm);
    Screen_ShowMessage("PWR WARN");
    Screen_ShowWarningPage();
    App_ReportStatus(true);
}

static void App_ResumePowerResetAfterUserFix(void)
{
    if (app_state != APP_STATE_POWER_ON_RESET ||
        app_power_reset_phase != APP_POWER_RESET_PHASE_WAIT_USER_FIX) {
        return;
    }

    if (app_power_reset_wait_reason == APP_POWER_RESET_WAIT_Y_READY &&
        !App_IsYReadyForZMotion()) {
        Logger_Info("BOOT", "power_reset_resume_rejected reason=Y_NOT_READY");
        Screen_ShowAlarm((uint16_t)APP_ALARM_Y_NOT_READY);
        Screen_ShowMessage("PWR WARN");
        Screen_ShowWarningPage();
        App_ReportStatus(true);
        return;
    }

    Logger_Value("BOOT", "power_reset_resume", (uint32_t)app_power_reset_wait_reason);
    App_StartPowerResetDown();
}

static uint16_t App_ZSpeed(void)
{
    return Pump_SpeedPercentToMotorSpeed(APP_Z_SPEED_PERCENT);
}

static uint32_t App_CurrentStepTimeoutMs(void)
{
    if (app_step_target_pos == APP_Z_POS_HOME) {
        return APP_HOME_TIMEOUT_MS;
    }

    if (App_ZPosHasSensor(app_step_target_pos)) {
        return APP_Z_MOVE_TIMEOUT_MS;
    }

    return app_step_duration_ms + 500U;
}

static void App_CompleteZStep(void)
{
    App_ZBrake();
    app_current_pos = app_step_target_pos;
    app_step_target_pos = APP_Z_POS_INVALID;
    app_step_direction = MOTOR_STOP;
    app_step_start_tick = 0U;
    app_step_duration_ms = 0U;
}

static bool App_TryPassThroughZeroDwellAspirateTarget(void)
{
    App_ZPosition reached_pos;
    App_ZPosition next_pos;
    uint8_t next_direction;

    if (app_state != APP_STATE_MOVE_TO_ASPIRATE) {
        return false;
    }

    if (app_phase_index >= app_aspirate_phase_count ||
        (uint8_t)(app_phase_index + 1U) >= app_aspirate_phase_count) {
        return false;
    }

    if (app_step_target_pos != app_target_pos ||
        app_step_target_pos != app_aspirate_sequence[app_phase_index]) {
        return false;
    }

    if (app_aspirate_dwell_sequence[app_phase_index] != 0U) {
        return false;
    }

    reached_pos = app_step_target_pos;
    next_pos = app_aspirate_sequence[app_phase_index + 1U];
    if (!App_ZMoveDirectionBetween(reached_pos, next_pos, &next_direction) ||
        next_direction != app_step_direction) {
        return false;
    }

    app_current_pos = reached_pos;
    app_step_target_pos = APP_Z_POS_INVALID;
    app_step_start_tick = 0U;
    app_step_duration_ms = 0U;

    Logger_Info("ASP", "pass_zero_dwell");
    app_phase_index++;
    App_EnterAspirateMove(app_phase_index);
    return true;
}

static void App_StartNextZStep(void)
{
    App_ZPosition from;
    App_ZPosition to;
    uint8_t direction;
    uint16_t speed;

    if (!App_ZPosIsValid(app_target_pos)) {
        App_ZBrake();
        app_step_direction = MOTOR_STOP;
        return;
    }

    if (App_ZPosSensorActive(app_target_pos)) {
        app_current_pos = app_target_pos;
        app_step_target_pos = APP_Z_POS_INVALID;
        app_step_direction = MOTOR_STOP;
        App_ZBrake();
        return;
    }

    /*
     * 如果当前位置未知，先向上寻找 HOME。自动流程每次 START 也会先 HOME，
     * 这样定时位置不会在未知基准上累积误差。
     */
    if (!App_ZPosIsValid(app_current_pos)) {
        from = APP_Z_POS_INVALID;
        to = APP_Z_POS_HOME;
        direction = APP_Z_UP_DIRECTION;
        app_step_duration_ms = APP_HOME_TIMEOUT_MS;
    } else if (app_current_pos == app_target_pos) {
        app_step_target_pos = APP_Z_POS_INVALID;
        app_step_direction = MOTOR_STOP;
        App_ZBrake();
        return;
    } else if ((uint8_t)app_target_pos > (uint8_t)app_current_pos) {
        from = app_current_pos;
        to = (App_ZPosition)((uint8_t)app_current_pos + 1U);
        direction = APP_Z_DOWN_DIRECTION;
        app_step_duration_ms = App_ZStepDurationMs(from, to);
    } else {
        from = app_current_pos;
        to = (App_ZPosition)((uint8_t)app_current_pos - 1U);
        direction = APP_Z_UP_DIRECTION;
        app_step_duration_ms = App_ZStepDurationMs(from, to);
    }

    app_step_target_pos = to;
    app_step_direction = direction;
    app_step_start_tick = 0U;
    speed = App_ZSpeed();

    Logger_Move((uint8_t)to,
                (int8_t)app_current_pos,
                (int8_t)app_target_pos,
                direction,
                speed,
                App_CurrentStepTimeoutMs(),
                PG_ReadMask());
    if (App_ZStartDrive(direction, speed)) {
        app_step_start_tick = Now();
    }
}

static void App_StartZMoveTo(App_ZPosition target)
{
    app_target_pos = target;
    app_step_target_pos = APP_Z_POS_INVALID;
    app_step_duration_ms = 0U;
    App_StartNextZStep();
}

static void App_EnterMoveState(App_State state, App_ZPosition target)
{
    App_SetState(state);
    App_StartZMoveTo(target);
}

static void App_EnterAspirateMove(uint8_t phase_index)
{
    if (phase_index >= app_aspirate_phase_count) {
        return;
    }

    if (app_aspirate_move_pump_sequence[phase_index]) {
        Pump_RunAll(PUMP_DIR_IN, app_aspirate_speed_percent);
    } else {
        Pump_StopAll();
    }

    App_EnterMoveState(APP_STATE_MOVE_TO_ASPIRATE,
                       app_aspirate_sequence[phase_index]);
}

static void App_StartPowerResetHomeSeek(void)
{
    app_power_reset_phase = APP_POWER_RESET_PHASE_UP;
    app_power_reset_wait_reason = APP_POWER_RESET_WAIT_NONE;
    app_power_reset_start_tick = 0U;
    app_current_pos = APP_Z_POS_INVALID;
    Logger_Info("BOOT", "power_reset_up_seek_home");
    App_SetState(APP_STATE_POWER_ON_RESET);
    App_StartZMoveTo(APP_Z_POS_HOME);
}

static void App_StartPowerResetDown(void)
{
    app_alarm = APP_ALARM_NONE;
    app_auto_after_home = false;
    app_return_after_success = false;
    app_phase_index = 0U;
    app_target_pos = APP_Z_POS_HOME;
    app_step_target_pos = APP_Z_POS_INVALID;
    app_step_start_tick = 0U;
    app_step_duration_ms = 0U;
    app_current_pos = APP_Z_POS_INVALID;
    app_power_reset_wait_reason = APP_POWER_RESET_WAIT_NONE;
    App_SetState(APP_STATE_POWER_ON_RESET);

    if (PG_IsActive(APP_Z_BOTTOM_PG)) {
        app_current_pos = APP_Z_POS_50ML;
        Logger_Info("BOOT", "power_reset_bottom_already_active");
        App_StartPowerResetHomeSeek();
        return;
    }

    app_power_reset_phase = APP_POWER_RESET_PHASE_DOWN;
    app_power_reset_start_tick = 0U;
    Logger_Value("BOOT", "power_reset_down_ms", APP_POWER_ON_RESET_DOWN_MS);
    if (App_ZStartDrive(APP_Z_DOWN_DIRECTION, App_ZSpeed())) {
        app_power_reset_start_tick = Now();
    }
}

static bool App_TargetReached(void)
{
    if (App_ZPosIsValid(app_current_pos) && app_current_pos == app_target_pos) {
        App_ZBrake();
        return true;
    }

    if (!App_ZPosIsValid(app_step_target_pos)) {
        App_StartNextZStep();
        return false;
    }

    if (!App_CheckYReadyForZMotion()) {
        return false;
    }

    if (App_ZDeadtimeTask()) {
        app_step_start_tick = Now();
    }

    if (app_step_start_tick == 0U) {
        return false;
    }

    if (App_ZPosHasSensor(app_step_target_pos)) {
        if (App_ZPosSensorActive(app_step_target_pos)) {
            if (App_TryPassThroughZeroDwellAspirateTarget()) {
                return false;
            }

            App_CompleteZStep();
            if (app_current_pos == app_target_pos) {
                return true;
            }
            App_StartNextZStep();
        }
        return false;
    }

    if (Elapsed(app_step_start_tick) >= app_step_duration_ms) {
        if (App_TryPassThroughZeroDwellAspirateTarget()) {
            return false;
        }

        App_CompleteZStep();
        if (app_current_pos == app_target_pos) {
            return true;
        }
        App_StartNextZStep();
    }

    return false;
}

static bool App_MoveTimedOut(void)
{
    if (!App_ZPosIsValid(app_step_target_pos)) {
        return false;
    }

    if (app_step_start_tick == 0U) {
        return false;
    }

    return Elapsed(app_step_start_tick) > App_CurrentStepTimeoutMs();
}

static uint32_t App_MoveTimeoutLimitMs(void)
{
    return App_CurrentStepTimeoutMs();
}

static void App_TaskPowerOnReset(void)
{
    if (app_power_reset_phase == APP_POWER_RESET_PHASE_WAIT_USER_FIX) {
        return;
    }

    if (app_power_reset_phase == APP_POWER_RESET_PHASE_DOWN) {
        if (!App_CheckYReadyForZMotion()) {
            return;
        }

        if (App_ZDeadtimeTask()) {
            app_power_reset_start_tick = Now();
        }

        if (app_power_reset_start_tick == 0U) {
            return;
        }

        if (PG_IsActive(APP_Z_BOTTOM_PG)) {
            App_ZBrake();
            app_current_pos = APP_Z_POS_50ML;
            Logger_Info("BOOT", "power_reset_down_stop reason=BOTTOM_PG");
            App_StartPowerResetHomeSeek();
            return;
        }

        if (Elapsed(app_power_reset_start_tick) >= APP_POWER_ON_RESET_DOWN_MS) {
            App_ZBrake();
            Logger_Info("BOOT", "power_reset_down_stop reason=TIME_DONE");
            App_StartPowerResetHomeSeek();
        }
        return;
    }

    if (App_TargetReached()) {
        app_power_reset_phase = APP_POWER_RESET_PHASE_NONE;
        app_auto_after_home = false;
        app_return_after_success = false;
        App_AllStop();
        Logger_Info("BOOT", "power_home_reached");
        App_SetState(APP_STATE_IDLE);
        Screen_ShowMessage("READY");
    } else if (App_MoveTimedOut()) {
        Logger_Info("BOOT", "power_reset_wait_user_fix reason=Z_HOME_TIMEOUT");
        App_PausePowerResetForUser(APP_POWER_RESET_WAIT_Z_HOME);
    }
}

static void App_Fail(App_Alarm alarm)
{
    App_ZPosition alarm_target_pos = app_target_pos;
    uint32_t alarm_elapsed_ms = Elapsed(app_state_start_tick);
    uint32_t alarm_timeout_ms = App_MoveTimeoutLimitMs();

    if (App_ZPosIsValid(app_step_target_pos)) {
        alarm_target_pos = app_step_target_pos;
        alarm_elapsed_ms = Elapsed(app_step_start_tick);
    }

    /* 故障时立即停止所有执行机构，并把报警码同步到屏幕。 */
    app_alarm = alarm;
    app_auto_after_home = false;
    app_return_after_success = false;
    App_AllStop();
    Logger_AlarmDetail((uint16_t)alarm,
                       (uint8_t)app_state,
                       (uint8_t)alarm_target_pos,
                       PG_ReadMask(),
                       alarm_elapsed_ms,
                       alarm_timeout_ms);
    Screen_ShowAlarm((uint16_t)alarm);
    Screen_ShowMessage("ERROR");
    App_SetState(APP_STATE_ERROR);
}

static bool App_LeakIsNormal(void)
{
#if APP_LEAK_DETECT_ENABLE
    if (!PG_IsValid(APP_LEAK_PG)) {
        return true;
    }

    /* 漏水检测使用原始电平：高电平为正常，低电平为异常。 */
    return PG_ReadRaw(APP_LEAK_PG) == GPIO_PIN_SET;
#else
    return true;
#endif
}

static void App_LogLeakStatus(bool normal, const char *mode_text)
{
    if (normal) {
        if (mode_text == 0) {
            Logger_Info("LEAK", "state=normal raw=HIGH");
        } else if (mode_text[0] == 't') {
            Logger_Info("LEAK", "mode=test state=normal raw=HIGH");
        } else {
            Logger_Info("LEAK", "mode=formal state=normal raw=HIGH");
        }
    } else {
        if (mode_text == 0) {
            Logger_Info("LEAK", "state=abnormal raw=LOW");
        } else if (mode_text[0] == 't') {
            Logger_Info("LEAK", "mode=test state=abnormal raw=LOW");
        } else {
            Logger_Info("LEAK", "mode=formal state=abnormal raw=LOW");
        }
    }
}

static void App_TriggerLeakAlarm(void)
{
    if (app_leak_fault_latched) {
        return;
    }

    app_leak_fault_latched = true;
    Logger_Info("LEAK", "fault=detected action=all_stop page=leak_warn");

    if (app_state == APP_STATE_ESTOP) {
        app_alarm = APP_ALARM_LEAK_DETECTED;
        App_AllStop();
        Logger_Alarm((uint16_t)APP_ALARM_LEAK_DETECTED);
        Screen_ShowAlarm((uint16_t)APP_ALARM_LEAK_DETECTED);
    } else {
        App_Fail(APP_ALARM_LEAK_DETECTED);
    }

    Screen_ShowMessage("LEAK");
    Screen_ShowLeakWarningPage();
}

static void App_TaskLeakDetect(void)
{
#if APP_LEAK_DETECT_ENABLE
    bool normal;

    if (Elapsed(app_last_leak_check_tick) < APP_LEAK_CHECK_INTERVAL_MS) {
        return;
    }
    app_last_leak_check_tick = Now();

    normal = App_LeakIsNormal();

#if APP_LEAK_DETECT_TEST_ONLY
    if (app_leak_last_normal != normal ||
        Elapsed(app_last_leak_log_tick) >= APP_LEAK_TEST_LOG_INTERVAL_MS) {
        App_LogLeakStatus(normal, "test");
        app_last_leak_log_tick = Now();
    }
    app_leak_last_normal = normal;
#else
    if (normal) {
        if (!app_leak_last_normal) {
            App_LogLeakStatus(true, "formal");
        }
        app_leak_abnormal_count = 0U;
        app_leak_last_normal = true;
        return;
    }

    if (app_leak_abnormal_count < APP_LEAK_DEBOUNCE_COUNT) {
        app_leak_abnormal_count++;
    }

    if (app_leak_last_normal || app_leak_abnormal_count == 1U) {
        App_LogLeakStatus(false, "formal");
    }
    app_leak_last_normal = false;

    if (app_leak_abnormal_count >= APP_LEAK_DEBOUNCE_COUNT) {
        App_TriggerLeakAlarm();
    }
#endif
#endif
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
    const uint32_t *spray_ms = App_CurrentSprayMs();

    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        if (spray_ms[i] > max_ms) {
            max_ms = spray_ms[i];
        }
    }

    return max_ms;
}

static uint8_t App_ProgressPercent(void)
{
    uint32_t duration = 0U;
    uint32_t elapsed;

    if (app_state == APP_STATE_ASPIRATING && app_phase_index < app_aspirate_phase_count) {
        duration = app_aspirate_dwell_sequence[app_phase_index];
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
                        app_aspirate_speed_percent,
                        PG_ReadMask(),
                        app_keep10,
                        (uint16_t)app_alarm,
                        App_ProgressPercent());
}

static void App_StartAuto(uint16_t volume_ml, uint8_t keep10)
{
    uint16_t reserved_ml = keep10 ? APP_MANUAL_RESERVED_VOLUME_ML : 0U;

    /* 自动流程只允许从空闲、完成或错误状态重新启动。 */
    if (!(app_state == APP_STATE_IDLE ||
          app_state == APP_STATE_DONE ||
          app_state == APP_STATE_ERROR)) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("AUTO", "start_rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (App_FindVolumeIndex(volume_ml) < 0) {
        app_alarm = APP_ALARM_BAD_VOLUME;
        Logger_Info("AUTO", "start_rejected reason=BAD_VOLUME");
        Screen_ShowMessage("BAD VOL");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (!App_BuildAutoPlan(volume_ml, keep10)) {
        app_alarm = APP_ALARM_BAD_CONFIG;
        Logger_Info("AUTO", "start_rejected reason=BAD_CONFIG");
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
    app_current_pos = APP_Z_POS_INVALID;
    Logger_AutoPlan(volume_ml,
                    (uint16_t)(volume_ml - reserved_ml),
                    reserved_ml,
                    app_aspirate_phase_count,
                    app_spray_phase_count,
                    app_has_trim10,
                    app_aspirate_speed_percent);
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_POS_HOME);
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
    app_current_pos = APP_Z_POS_INVALID;
    Logger_Value("HOME", "continue_auto", continue_auto ? 1U : 0U);
    App_EnterMoveState(APP_STATE_HOMING, APP_Z_POS_HOME);
}

static void App_HandleManual(const Protocol_Command *command)
{
    uint8_t speed = command->speed_percent;

    /* 自动流程运行中禁止手动动作，防止状态机和手动控制抢执行机构。 */
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("MAN", "rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    /* 手动泵命令未带速度时使用手动泵速；手动 Z 轴保留旧逻辑，使用自动吸取速度。 */
    if (speed == 0U) {
        if (command->manual_target == PROTOCOL_MANUAL_TARGET_PUMP) {
            speed = app_manual_pump_speed_percent;
        } else {
            speed = app_aspirate_speed_percent;
        }
    }
    speed = Pump_ClampSpeedPercent(speed);

    if (command->manual_target == PROTOCOL_MANUAL_TARGET_Z) {
        if (command->manual_action == PROTOCOL_MANUAL_ACTION_UP) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_UP;
            app_current_pos = APP_Z_POS_INVALID;
            Logger_Value("MAN", "z_up_speed", speed);
            (void)App_ZStartDrive(APP_Z_UP_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            if (app_state != APP_STATE_ERROR) {
                App_SetState(APP_STATE_MANUAL);
            }
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_DOWN) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_DOWN;
            app_current_pos = APP_Z_POS_INVALID;
            Logger_Value("MAN", "z_down_speed", speed);
            (void)App_ZStartDrive(APP_Z_DOWN_DIRECTION, Pump_SpeedPercentToMotorSpeed(speed));
            if (app_state != APP_STATE_ERROR) {
                App_SetState(APP_STATE_MANUAL);
            }
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            App_ZBrake();
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            Logger_Info("MAN", "z_stop");
            if (app_manual_pump_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    } else if (command->manual_target == PROTOCOL_MANUAL_TARGET_PUMP) {
        if (command->manual_action == PROTOCOL_MANUAL_ACTION_IN) {
            App_ZBrake();
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_IN;
            Logger_Value("MAN", "pump_in_speed", speed);
            Pump_RunAll(PUMP_DIR_IN, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_OUT) {
            App_ZBrake();
            app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_OUT;
            Logger_Value("MAN", "pump_out_speed", speed);
            Pump_RunAll(PUMP_DIR_OUT, speed);
            App_SetState(APP_STATE_MANUAL);
        } else if (command->manual_action == PROTOCOL_MANUAL_ACTION_STOP) {
            Pump_StopAll();
            app_manual_pump_action = PROTOCOL_MANUAL_ACTION_NONE;
            Logger_Info("MAN", "pump_stop");
            if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_NONE) {
                App_SetState(APP_STATE_IDLE);
            }
        }
    }

    App_ReportStatus(true);
}

static int8_t App_RequireSprayProfile(uint16_t volume_ml)
{
    int8_t profile_index = App_FindSprayProfileIndex(volume_ml);

    if (profile_index < 0) {
        app_alarm = APP_ALARM_BAD_COMMAND;
        Screen_ShowMessage("BAD VOL");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return -1;
    }

    return profile_index;
}

static void App_HandleSetParam(const Protocol_Command *command)
{
    uint32_t value;
    int8_t spray_index;
    int8_t zvirt_index;
    int8_t profile_index;
    uint32_t *spray_ms = 0;
    bool spray_time_changed = false;
    bool zvirt_time_changed = false;

    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SET", "rejected reason=BUSY");
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
        zvirt_index = App_ZVirtParamIndex(command->param_target);
        if (zvirt_index >= 0 && (uint8_t)zvirt_index < APP_ZVIRT_COUNT) {
            value = App_ClampZVirtTimeMs(command->param_value);
            app_zvirt_ms[(uint8_t)zvirt_index] = value;
            Logger_Value("SET", "zvirt_ms", value);
            zvirt_time_changed = true;
            break;
        }

        spray_index = App_SprayParamIndex(command->param_target);
        if (spray_index < 0 || (uint8_t)spray_index >= APP_PUMP_COUNT) {
            app_alarm = APP_ALARM_BAD_COMMAND;
            Screen_ShowMessage("BAD CMD");
            Screen_ShowAlarm((uint16_t)app_alarm);
            return;
        }
        profile_index = App_RequireSprayProfile(command->spray_volume_ml);
        if (profile_index < 0) {
            return;
        }
        spray_ms = App_SprayMsForProfile((uint8_t)profile_index);
        if (spray_ms == 0) {
            return;
        }
        spray_ms[(uint8_t)spray_index] = value;
        Logger_Value("SET", "spray_pump_ms", value);
        spray_time_changed = true;
        break;
    }

    app_alarm = APP_ALARM_NONE;
    Screen_ShowMessage("SET OK");
    if (spray_time_changed) {
        Screen_UpdateSprayTimes(spray_ms, command->spray_volume_ml);
    }
    if (zvirt_time_changed) {
        Screen_UpdateZVirtTimes(app_zvirt_ms);
    }
    App_ReportStatus(true);
}

static void App_HandleSaveSprayTimes(void)
{
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SAVE", "rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (Settings_SaveAll(app_spray_profile_ms,
                         app_zvirt_ms,
                         app_aspirate_speed_percent,
                         app_manual_pump_speed_percent)) {
        app_alarm = APP_ALARM_NONE;
        Logger_Info("SAVE", "spray_ms result=ok");
        Screen_ShowMessage("SAVE OK");
        Screen_ShowAlarm((uint16_t)app_alarm);
    } else {
        app_alarm = APP_ALARM_SAVE_FAILED;
        Logger_Info("SAVE", "spray_ms result=failed");
        Screen_ShowMessage("SAVE ERR");
        Screen_ShowAlarm((uint16_t)app_alarm);
    }

    App_ReportStatus(true);
}

static void App_HandleSaveZVirtTimes(void)
{
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SAVE", "rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (Settings_SaveAll(app_spray_profile_ms,
                         app_zvirt_ms,
                         app_aspirate_speed_percent,
                         app_manual_pump_speed_percent)) {
        app_alarm = APP_ALARM_NONE;
        Logger_Info("SAVE", "zvirt_ms result=ok");
        Screen_ShowMessage("SAVE OK");
        Screen_ShowAlarm((uint16_t)app_alarm);
    } else {
        app_alarm = APP_ALARM_SAVE_FAILED;
        Logger_Info("SAVE", "zvirt_ms result=failed");
        Screen_ShowMessage("SAVE ERR");
        Screen_ShowAlarm((uint16_t)app_alarm);
    }

    Screen_UpdateZVirtTimes(app_zvirt_ms);
    App_ReportStatus(true);
}

static void App_HandleSavePumpSpeed(void)
{
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SAVE", "rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (Settings_SaveAll(app_spray_profile_ms,
                         app_zvirt_ms,
                         app_aspirate_speed_percent,
                         app_manual_pump_speed_percent)) {
        app_alarm = APP_ALARM_NONE;
        Logger_Info("SAVE", "speed result=ok");
        Screen_ShowMessage("SAVE OK");
        Screen_ShowAlarm((uint16_t)app_alarm);
    } else {
        app_alarm = APP_ALARM_SAVE_FAILED;
        Logger_Info("SAVE", "speed result=failed");
        Screen_ShowMessage("SAVE ERR");
        Screen_ShowAlarm((uint16_t)app_alarm);
    }

    Screen_UpdatePumpSpeed(app_aspirate_speed_percent);
    App_ReportStatus(true);
}

static void App_HandleSaveManualPumpSpeed(void)
{
    if (App_IsAutoRunning()) {
        app_alarm = APP_ALARM_BUSY;
        Logger_Info("SAVE", "rejected reason=BUSY");
        Screen_ShowMessage("BUSY");
        Screen_ShowAlarm((uint16_t)app_alarm);
        return;
    }

    if (Settings_SaveAll(app_spray_profile_ms,
                         app_zvirt_ms,
                         app_aspirate_speed_percent,
                         app_manual_pump_speed_percent)) {
        app_alarm = APP_ALARM_NONE;
        Logger_Info("SAVE", "manual_speed result=ok");
        Screen_ShowMessage("SAVE OK");
        Screen_ShowAlarm((uint16_t)app_alarm);
    } else {
        app_alarm = APP_ALARM_SAVE_FAILED;
        Logger_Info("SAVE", "manual_speed result=failed");
        Screen_ShowMessage("SAVE ERR");
        Screen_ShowAlarm((uint16_t)app_alarm);
    }

    Screen_UpdateManualPumpSpeed(app_manual_pump_speed_percent);
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
        if (app_state == APP_STATE_POWER_ON_RESET &&
            app_power_reset_phase == APP_POWER_RESET_PHASE_WAIT_USER_FIX) {
            Logger_Info("CMD", "stop_ignored reason=POWER_RESET_WAIT_USER_FIX");
            Screen_ShowMessage("PWR WARN");
            Screen_ShowWarningPage();
            App_ReportStatus(true);
            break;
        }
        App_AllStop();
        app_auto_after_home = false;
        app_return_after_success = false;
        app_current_pos = APP_Z_POS_INVALID;
        Logger_Info("CMD", "stop_handled");
        /* STOP 在自动流程中按“停止后回原点”处理；人工补加等待中则直接取消。 */
        if (app_state == APP_STATE_POWER_ON_RESET) {
            App_StartPowerResetHomeSeek();
        } else if (app_state == APP_STATE_WAIT_MANUAL_CUP_CLEAN) {
            app_manual_reserved_ml = 0U;
            App_SetState(APP_STATE_IDLE);
        } else if (App_IsAutoRunning()) {
            App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_POS_HOME);
        } else {
            App_SetState(APP_STATE_IDLE);
        }
        break;

    case PROTOCOL_CMD_ESTOP:
        /* ESTOP 立即停机，不自动回原点。 */
        App_AllStop();
        app_auto_after_home = false;
        app_return_after_success = false;
        app_current_pos = APP_Z_POS_INVALID;
        app_alarm = APP_ALARM_NONE;
        Logger_Info("CMD", "estop_handled");
        App_SetState(APP_STATE_ESTOP);
        Screen_ShowMessage("ESTOP");
        break;

    case PROTOCOL_CMD_HOME:
        App_StartHome(false);
        break;

    case PROTOCOL_CMD_OK:
        /* 上电复位暂停等待人工修正时，屏幕发送 #OK; 后重新执行复位。 */
        if (app_state == APP_STATE_POWER_ON_RESET &&
            app_power_reset_phase == APP_POWER_RESET_PHASE_WAIT_USER_FIX) {
            App_ResumePowerResetAfterUserFix();
        } else if (app_state == APP_STATE_WAIT_MANUAL_CUP_CLEAN) {
            /* 人工用 10ml 清洗接液烧杯并补加完成后，屏幕发送 #OK; 确认整套流程完成。 */
            app_manual_reserved_ml = 0U;
            App_SetState(APP_STATE_DONE);
            Screen_ShowMessage("DONE");
            Logger_Info("AUTO", "manual_10ml_confirmed");
            App_ReportStatus(true);
        }
        break;

    case PROTOCOL_CMD_SPEED_SET:
        app_aspirate_speed_percent = Pump_ClampSpeedPercent(command->speed_percent);
        app_alarm = APP_ALARM_NONE;
        Logger_Value("SET", "aspirate_speed", app_aspirate_speed_percent);
        Screen_UpdatePumpSpeed(app_aspirate_speed_percent);
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_MANUAL_SPEED_SET:
        app_manual_pump_speed_percent = Pump_ClampSpeedPercent(command->speed_percent);
        app_alarm = APP_ALARM_NONE;
        Logger_Value("SET", "manual_pump_speed", app_manual_pump_speed_percent);
        Screen_UpdateManualPumpSpeed(app_manual_pump_speed_percent);
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

    case PROTOCOL_CMD_GET_SPRAY_MS:
        if (!App_IsAutoRunning()) {
            int8_t profile_index = App_RequireSprayProfile(command->spray_volume_ml);
            if (profile_index >= 0) {
                Screen_UpdateSprayTimes(App_SprayMsForProfile((uint8_t)profile_index),
                                       command->spray_volume_ml);
                app_alarm = APP_ALARM_NONE;
                App_ReportStatus(true);
            }
        } else {
            app_alarm = APP_ALARM_BUSY;
            Logger_Info("GET", "spray_ms_rejected reason=BUSY");
            Screen_ShowMessage("BUSY");
            Screen_ShowAlarm((uint16_t)app_alarm);
        }
        break;

    case PROTOCOL_CMD_GET_ZVIRT_MS:
        if (!App_IsAutoRunning()) {
            Screen_UpdateZVirtTimes(app_zvirt_ms);
            app_alarm = APP_ALARM_NONE;
            App_ReportStatus(true);
        } else {
            app_alarm = APP_ALARM_BUSY;
            Logger_Info("GET", "zvirt_ms_rejected reason=BUSY");
            Screen_ShowMessage("BUSY");
            Screen_ShowAlarm((uint16_t)app_alarm);
        }
        break;

    case PROTOCOL_CMD_GET_SPEED:
        Screen_UpdatePumpSpeed(app_aspirate_speed_percent);
        app_alarm = APP_ALARM_NONE;
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_GET_MANUAL_SPEED:
        Screen_UpdateManualPumpSpeed(app_manual_pump_speed_percent);
        app_alarm = APP_ALARM_NONE;
        App_ReportStatus(true);
        break;

    case PROTOCOL_CMD_SAVE_SPRAY_MS:
        App_HandleSaveSprayTimes();
        break;

    case PROTOCOL_CMD_SAVE_ZVIRT_MS:
        App_HandleSaveZVirtTimes();
        break;

    case PROTOCOL_CMD_SAVE_SPEED:
        App_HandleSavePumpSpeed();
        break;

    case PROTOCOL_CMD_SAVE_MANUAL_SPEED:
        App_HandleSaveManualPumpSpeed();
        break;

    case PROTOCOL_CMD_SAVE_ALL:
        App_HandleSaveManualPumpSpeed();
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
    if (app_manual_z_action != PROTOCOL_MANUAL_ACTION_NONE) {
        if (!App_CheckYReadyForZMotion()) {
            return;
        }

        (void)App_ZDeadtimeTask();
    }

    /* 手动上升/下降分别用 PG3/PG7 做软限位，PG7 同时是 50ml 和下限位。 */
    if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_UP &&
        PG_IsActive(APP_Z_HOME_PG)) {
        App_ZBrake();
        app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
        app_current_pos = APP_Z_POS_HOME;
        Screen_ShowMessage("Z HOME");
    } else if (app_manual_z_action == PROTOCOL_MANUAL_ACTION_DOWN &&
               PG_IsActive(APP_Z_BOTTOM_PG)) {
        App_ZBrake();
        app_manual_z_action = PROTOCOL_MANUAL_ACTION_NONE;
        app_current_pos = APP_Z_POS_50ML;
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
    Logger_Info("SPRAY", "sequence_start");
    App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY, app_spray_sequence[app_phase_index]);
}

static void App_StartSprayPumps(void)
{
    const uint32_t *spray_ms = App_CurrentSprayMs();

    App_SetState(APP_STATE_SPRAYING);
    app_spray_active_pump_mask = 0U;
    Logger_PhaseStart("SPRAY",
                      (uint8_t)(app_phase_index + 1U),
                      app_spray_phase_count,
                      (uint8_t)app_spray_sequence[app_phase_index],
                      App_GetSprayMaxMs(),
                      Pump_ClampSpeedPercent(APP_SPRAY_SPEED_PERCENT),
                      PG_ReadMask());

    /* 第一段使用屏幕可调时间；第二段 300ml、第三段 800ml 使用程序固定表。 */
    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        if (spray_ms[i] > 0U) {
            Pump_RunOne(i, PUMP_DIR_OUT, Pump_ClampSpeedPercent(APP_SPRAY_SPEED_PERCENT));
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
    Logger_PhaseDone("SPRAY",
                     (uint8_t)(app_phase_index + 1U),
                     Elapsed(app_state_start_tick),
                     PG_ReadMask());
    app_phase_index++;

    if (app_phase_index < app_spray_phase_count) {
        App_EnterMoveState(APP_STATE_MOVE_TO_SPRAY,
                           app_spray_sequence[app_phase_index]);
    } else {
        /* 自动喷淋结束后必须回原点。 */
        app_return_after_success = true;
        Logger_Info("SPRAY", "sequence_done");
        App_EnterMoveState(APP_STATE_RETURN_HOME, APP_Z_POS_HOME);
    }
}

static void App_TaskSpraying(void)
{
    uint32_t elapsed = Elapsed(app_state_start_tick);
    const uint32_t *spray_ms = App_CurrentSprayMs();

    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        uint8_t pump_bit = (uint8_t)(1U << i);
        if ((app_spray_active_pump_mask & pump_bit) != 0U &&
            elapsed >= spray_ms[i]) {
            Pump_StopOne(i);
            app_spray_active_pump_mask &= (uint8_t)(~pump_bit);
            Logger_SprayPumpStop((uint8_t)(app_phase_index + 1U),
                                 (uint8_t)(i + 1U),
                                 spray_ms[i],
                                 elapsed,
                                 app_spray_active_pump_mask,
                                 PG_ReadMask());
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
        App_TaskPowerOnReset();
        break;

    case APP_STATE_CHECK_Y:
        /* 默认 PG1 为 Y 轴允许工作位置，低电平有效。 */
        if (PG_IsActive(APP_Y_READY_PG)) {
            app_phase_index = 0U;
            Logger_Info("AUTO", "y_ready");
            App_EnterAspirateMove(app_phase_index);
        } else {
            Logger_Info("AUTO", "y_not_ready");
            App_Fail(APP_ALARM_Y_NOT_READY);
            Screen_ShowWarningPage();
        }
        break;

    case APP_STATE_MOVE_TO_ASPIRATE:
        if (App_TargetReached()) {
            Logger_PhaseStart("ASP",
                              (uint8_t)(app_phase_index + 1U),
                              app_aspirate_phase_count,
                              (uint8_t)app_aspirate_sequence[app_phase_index],
                              app_aspirate_dwell_sequence[app_phase_index],
                              app_aspirate_speed_percent,
                              PG_ReadMask());
            if (app_aspirate_dwell_pump_sequence[app_phase_index]) {
                Pump_RunAll(PUMP_DIR_IN, app_aspirate_speed_percent);
            } else {
                Pump_StopAll();
            }
            App_SetState(APP_STATE_ASPIRATING);
        } else if (App_MoveTimedOut()) {
            App_Fail(APP_ALARM_Z_TIMEOUT);
        }
        break;

    case APP_STATE_ASPIRATING:
        if (Elapsed(app_state_start_tick) >= app_aspirate_dwell_sequence[app_phase_index]) {
            Logger_PhaseDone("ASP",
                             (uint8_t)(app_phase_index + 1U),
                             Elapsed(app_state_start_tick),
                             PG_ReadMask());
            app_phase_index++;
            if (app_phase_index < app_aspirate_phase_count) {
                App_EnterAspirateMove(app_phase_index);
            } else if (app_has_trim10) {
                /* 目标体积减 10ml 时没有独立 PG，当前以定时补吸作为占位实现。 */
                Logger_PhaseStart("TRIM10",
                                  (uint8_t)(app_aspirate_phase_count + 1U),
                                  (uint8_t)(app_aspirate_phase_count + 1U),
                                  (uint8_t)app_target_pos,
                                  app_trim10_ms,
                                  app_aspirate_speed_percent,
                                  PG_ReadMask());
                Pump_RunAll(PUMP_DIR_IN, app_aspirate_speed_percent);
                App_SetState(APP_STATE_TRIM_ASPIRATING);
            } else {
                Pump_StopAll();
                App_StartSprayPhaseZero();
            }
        }
        break;

    case APP_STATE_TRIM_ASPIRATING:
        if (Elapsed(app_state_start_tick) >= app_trim10_ms) {
            Pump_StopAll();
            Logger_PhaseDone("TRIM10",
                             (uint8_t)(app_aspirate_phase_count + 1U),
                             Elapsed(app_state_start_tick),
                             PG_ReadMask());
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
        /* 第一段使用屏幕可调时间，第二段和第三段使用程序固定时间。 */
        App_TaskSpraying();
        break;

    case APP_STATE_RETURN_HOME:
        if (App_TargetReached()) {
            App_AllStop();
            app_auto_after_home = false;
            Logger_Info("HOME", "return_reached");
            if (app_return_after_success && app_manual_reserved_ml > 0U) {
                app_return_after_success = false;
                Screen_ShowMessage("ADD 10ML");
                Logger_Info("AUTO", "wait_manual_10ml");
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

#if APP_LEAK_DETECT_ENABLE
    Logger_Value("LEAK", "pg", PG_ToNumber(APP_LEAK_PG));
#if APP_LEAK_DETECT_TEST_ONLY
    Logger_Info("LEAK", "mode=test high=normal action=log_only");
#else
    Logger_Info("LEAK", "mode=formal high=normal action=all_stop");
#endif
#endif

    if (HAL_TIM_Base_Start_IT(&htim5) == HAL_OK) {
        Logger_Info("BOOT", "tim5_heartbeat_start result=ok");
    } else {
        Logger_Info("BOOT", "tim5_heartbeat_start result=failed");
    }

    app_state = APP_STATE_IDLE;
    app_alarm = APP_ALARM_NONE;
    app_aspirate_speed_percent = Pump_ClampSpeedPercent(APP_DEFAULT_PUMP_SPEED_PERCENT);
    app_manual_pump_speed_percent = Pump_ClampSpeedPercent(APP_DEFAULT_PUMP_SPEED_PERCENT);
    app_aspirate_phase_ms = App_ClampProcessTimeMs(APP_ASPIRATE_PHASE_MS);
    app_trim10_ms = App_ClampProcessTimeMs(APP_TRIM_10ML_MS);
    app_last_leak_check_tick = 0U;
    app_last_leak_log_tick = 0U;
    app_leak_abnormal_count = 0U;
    app_leak_last_normal = true;
    app_leak_fault_latched = false;
    App_LoadDefaultSprayTimes();
    App_LoadDefaultZVirtTimes();
    app_spray_active_profile = 0U;
    if (Settings_LoadAll(app_spray_profile_ms,
                         app_zvirt_ms,
                         &app_aspirate_speed_percent,
                         &app_manual_pump_speed_percent)) {
        Logger_Info("BOOT", "settings_load source=flash");
    } else {
        Logger_Info("BOOT", "settings_load source=default");
    }
    app_last_screen_tick = 0U;
    app_auto_after_home = false;
    app_return_after_success = false;
    app_phase_index = 0U;
    App_AllStop();

#if APP_POWER_ON_RESET_ENABLE
    /* 上电后先短时下行，再上行寻找最高点。最高点由 APP_Z_HOME_PG 配置，当前默认 PG3。 */
    Logger_Info("BOOT", "power_reset_start");
    App_StartPowerResetDown();
#else
    App_SetState(APP_STATE_IDLE);
#endif

    App_ReportStatus(true);
    Screen_UpdatePumpSpeed(app_aspirate_speed_percent);
    Screen_UpdateManualPumpSpeed(app_manual_pump_speed_percent);
    Screen_UpdateSprayTimes(App_SprayMsForProfile(0U), App_SprayProfileVolume(0U));
    Screen_UpdateZVirtTimes(app_zvirt_ms);
}

void App_Task(void)
{
    /* 主循环任务入口，保持非阻塞，确保串口命令和安全状态能及时响应。 */
    Protocol_Process();
    App_ProcessCommands();
    App_TaskLeakDetect();

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
    return app_aspirate_speed_percent;
}

uint16_t App_GetPGMask(void)
{
    return PG_ReadMask();
}
