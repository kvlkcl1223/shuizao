#include "logger.h"
#include "app.h"
#include "app_config.h"
#include "main.h"
#include "motor.h"
#include "pg.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/*
 * logger.c
 * USART2 调试日志实现。
 * 日志正文故意保持 ASCII，避免不支持 UTF-8 的串口助手显示乱码；中文解释写在 README 中。
 */

#define LOGGER_LINE_SIZE 192U

static void Logger_SendLine(const char *line)
{
#if APP_LOG_ENABLE
    uint16_t len;

    if (line == 0) {
        return;
    }

    len = (uint16_t)strlen(line);
    if (len == 0U) {
        return;
    }

    (void)HAL_UART_Transmit(&huart2,
                            (uint8_t *)line,
                            len,
                            APP_LOG_UART_TIMEOUT_MS);
#else
    (void)line;
#endif
}

static uint32_t Logger_Tick(void)
{
    return HAL_GetTick();
}

static void Logger_FormatPrefix(char *line, uint16_t size, const char *tag)
{
    (void)snprintf(line, size, "[%010lums][%s] ",
                   (unsigned long)Logger_Tick(),
                   tag);
}

static const char *Logger_StateName(uint8_t state)
{
    switch ((App_State)state) {
    case APP_STATE_IDLE:
        return "IDLE";
    case APP_STATE_HOMING:
        return "HOMING";
    case APP_STATE_CHECK_Y:
        return "CHECK_Y";
    case APP_STATE_MOVE_TO_ASPIRATE:
        return "MOVE_TO_ASPIRATE";
    case APP_STATE_ASPIRATING:
        return "ASPIRATING";
    case APP_STATE_TRIM_ASPIRATING:
        return "TRIM_ASPIRATING";
    case APP_STATE_MOVE_TO_SPRAY:
        return "MOVE_TO_SPRAY";
    case APP_STATE_SPRAYING:
        return "SPRAYING";
    case APP_STATE_RETURN_HOME:
        return "RETURN_HOME";
    case APP_STATE_WAIT_MANUAL_CUP_CLEAN:
        return "WAIT_MANUAL_CUP_CLEAN";
    case APP_STATE_DONE:
        return "DONE";
    case APP_STATE_ERROR:
        return "ERROR";
    case APP_STATE_ESTOP:
        return "ESTOP";
    case APP_STATE_MANUAL:
        return "MANUAL";
    case APP_STATE_POWER_ON_RESET:
        return "POWER_ON_RESET";
    default:
        return "UNKNOWN";
    }
}

static const char *Logger_AlarmName(uint16_t alarm)
{
    switch ((App_Alarm)alarm) {
    case APP_ALARM_NONE:
        return "NONE";
    case APP_ALARM_BUSY:
        return "BUSY";
    case APP_ALARM_BAD_VOLUME:
        return "BAD_VOLUME";
    case APP_ALARM_Y_NOT_READY:
        return "Y_NOT_READY";
    case APP_ALARM_Z_TIMEOUT:
        return "Z_TIMEOUT";
    case APP_ALARM_BAD_COMMAND:
        return "BAD_COMMAND";
    case APP_ALARM_BAD_CONFIG:
        return "BAD_CONFIG";
    case APP_ALARM_SAVE_FAILED:
        return "SAVE_FAILED";
    default:
        return "UNKNOWN";
    }
}

static const char *Logger_CommandName(Protocol_CommandType type)
{
    switch (type) {
    case PROTOCOL_CMD_START:
        return "START";
    case PROTOCOL_CMD_STOP:
        return "STOP";
    case PROTOCOL_CMD_ESTOP:
        return "ESTOP";
    case PROTOCOL_CMD_HOME:
        return "HOME";
    case PROTOCOL_CMD_OK:
        return "OK";
    case PROTOCOL_CMD_SPEED_SET:
        return "SPD";
    case PROTOCOL_CMD_SET_PARAM:
        return "SET";
    case PROTOCOL_CMD_MANUAL:
        return "MAN";
    case PROTOCOL_CMD_GET_PG:
        return "GET_PG";
    case PROTOCOL_CMD_GET_STATE:
        return "GET_STATE";
    case PROTOCOL_CMD_GET_SPRAY_MS:
        return "GET_SPRAY_MS";
    case PROTOCOL_CMD_GET_ZVIRT_MS:
        return "GET_ZVIRT_MS";
    case PROTOCOL_CMD_SAVE_SPRAY_MS:
        return "SAVE_SPRAY_MS";
    case PROTOCOL_CMD_SAVE_ZVIRT_MS:
        return "SAVE_ZVIRT_MS";
    case PROTOCOL_CMD_SAVE_ALL:
        return "SAVE_ALL";
    case PROTOCOL_CMD_RESET:
        return "RESET";
    default:
        return "NONE";
    }
}

static const char *Logger_ParamName(Protocol_ParamTarget target)
{
    switch (target) {
    case PROTOCOL_PARAM_ASPIRATE_MS:
        return "ASP_MS";
    case PROTOCOL_PARAM_TRIM10_MS:
        return "TRIM10_MS";
    case PROTOCOL_PARAM_SPRAY1_MS:
        return "SPRAY1_MS";
    case PROTOCOL_PARAM_SPRAY2_MS:
        return "SPRAY2_MS";
    case PROTOCOL_PARAM_SPRAY3_MS:
        return "SPRAY3_MS";
    case PROTOCOL_PARAM_SPRAY4_MS:
        return "SPRAY4_MS";
    case PROTOCOL_PARAM_SPRAY5_MS:
        return "SPRAY5_MS";
    case PROTOCOL_PARAM_SPRAY6_MS:
        return "SPRAY6_MS";
    case PROTOCOL_PARAM_Z_DN_HOME_800_MS:
        return "Z_DN_HOME_800_MS";
    case PROTOCOL_PARAM_Z_DN_800_300_MS:
        return "Z_DN_800_300_MS";
    case PROTOCOL_PARAM_Z_UP_300_800_MS:
        return "Z_UP_300_800_MS";
    case PROTOCOL_PARAM_Z_UP_200_300_MS:
        return "Z_UP_200_300_MS";
    default:
        return "NONE";
    }
}

static const char *Logger_ManualTargetName(Protocol_ManualTarget target)
{
    switch (target) {
    case PROTOCOL_MANUAL_TARGET_Z:
        return "Z";
    case PROTOCOL_MANUAL_TARGET_PUMP:
        return "PUMP";
    default:
        return "NONE";
    }
}

static const char *Logger_ManualActionName(Protocol_ManualAction action)
{
    switch (action) {
    case PROTOCOL_MANUAL_ACTION_UP:
        return "UP";
    case PROTOCOL_MANUAL_ACTION_DOWN:
        return "DOWN";
    case PROTOCOL_MANUAL_ACTION_IN:
        return "IN";
    case PROTOCOL_MANUAL_ACTION_OUT:
        return "OUT";
    case PROTOCOL_MANUAL_ACTION_STOP:
        return "STOP";
    default:
        return "NONE";
    }
}

static const char *Logger_DirectionName(uint8_t direction)
{
    switch (direction) {
    case MOTOR_FORWARD:
        return "FORWARD";
    case MOTOR_REVERSE:
        return "REVERSE";
    case MOTOR_STOP:
        return "STOP";
    default:
        return "UNKNOWN";
    }
}

static const char *Logger_ZPosName(uint8_t pos)
{
    switch ((App_ZPosition)pos) {
    case APP_Z_POS_HOME:
        return "HOME";
    case APP_Z_POS_800ML:
        return "800ml";
    case APP_Z_POS_300ML:
        return "300ml";
    case APP_Z_POS_200ML:
        return "200ml";
    case APP_Z_POS_150ML:
        return "150ml";
    case APP_Z_POS_100ML:
        return "100ml";
    case APP_Z_POS_50ML:
        return "50ml";
    default:
        return "UNKNOWN";
    }
}

static uint8_t Logger_ZPosSensorPG(uint8_t pos)
{
    switch ((App_ZPosition)pos) {
    case APP_Z_POS_HOME:
        return PG_ToNumber(APP_Z_HOME_PG);
    case APP_Z_POS_200ML:
        return PG_ToNumber(APP_Z_200ML_PG);
    case APP_Z_POS_150ML:
        return PG_ToNumber(APP_Z_150ML_PG);
    case APP_Z_POS_100ML:
        return PG_ToNumber(APP_Z_100ML_PG);
    case APP_Z_POS_50ML:
        return PG_ToNumber(APP_Z_50ML_PG);
    default:
        return 0U;
    }
}

void Logger_Init(void)
{
    Logger_Info("BOOT", "logger_ready uart=USART2 baud=115200 format=ASCII");
}

void Logger_Info(const char *tag, const char *message)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0 || message == 0) {
        return;
    }

    Logger_FormatPrefix(line, sizeof(line), tag);
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "%s\r\n", message);
    Logger_SendLine(line);
}

void Logger_Value(const char *tag, const char *name, uint32_t value)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0 || name == 0) {
        return;
    }

    Logger_FormatPrefix(line, sizeof(line), tag);
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "%s=%lu\r\n",
                   name,
                   (unsigned long)value);
    Logger_SendLine(line);
}

void Logger_State(uint8_t state)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "STATE");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "code=%u name=%s pgmask=0x%04X\r\n",
                   state,
                   Logger_StateName(state),
                   PG_ReadMask());
    Logger_SendLine(line);
}

void Logger_Alarm(uint16_t alarm)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "ALARM");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "code=%u name=%s\r\n",
                   alarm,
                   Logger_AlarmName(alarm));
    Logger_SendLine(line);
}

void Logger_AlarmDetail(uint16_t alarm,
                        uint8_t state,
                        uint8_t target_pos,
                        uint16_t pgmask,
                        uint32_t elapsed_ms,
                        uint32_t timeout_ms)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "ALARM");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "code=%u name=%s state=%u/%s target_pos=%s target_index=%u sensor_pg=PG%u pgmask=0x%04X elapsed_ms=%lu timeout_ms=%lu\r\n",
                   alarm,
                   Logger_AlarmName(alarm),
                   state,
                   Logger_StateName(state),
                   Logger_ZPosName(target_pos),
                   target_pos,
                   Logger_ZPosSensorPG(target_pos),
                   pgmask,
                   (unsigned long)elapsed_ms,
                   (unsigned long)timeout_ms);
    Logger_SendLine(line);
}

void Logger_Command(const Protocol_Command *command)
{
    char line[LOGGER_LINE_SIZE];
    uint16_t pgmask;

    if (command == 0) {
        return;
    }

    pgmask = PG_ReadMask();
    Logger_FormatPrefix(line, sizeof(line), "CMD");

    switch (command->type) {
    case PROTOCOL_CMD_START:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=START volume_ml=%u keep10=%u pgmask=0x%04X\r\n",
                       command->volume_ml,
                       command->keep10,
                       pgmask);
        break;

    case PROTOCOL_CMD_SPEED_SET:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=SPD percent=%u pgmask=0x%04X\r\n",
                       command->speed_percent,
                       pgmask);
        break;

    case PROTOCOL_CMD_SET_PARAM:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=SET param=%s value_ms=%lu spray_volume_ml=%u pgmask=0x%04X\r\n",
                       Logger_ParamName(command->param_target),
                       (unsigned long)command->param_value,
                       command->spray_volume_ml,
                       pgmask);
        break;

    case PROTOCOL_CMD_GET_SPRAY_MS:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=GET_SPRAY_MS spray_volume_ml=%u pgmask=0x%04X\r\n",
                       command->spray_volume_ml,
                       pgmask);
        break;

    case PROTOCOL_CMD_MANUAL:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=MAN target=%s action=%s speed=%u pgmask=0x%04X\r\n",
                       Logger_ManualTargetName(command->manual_target),
                       Logger_ManualActionName(command->manual_action),
                       command->speed_percent,
                       pgmask);
        break;

    default:
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "rx=%s pgmask=0x%04X\r\n",
                       Logger_CommandName(command->type),
                       pgmask);
        break;
    }

    Logger_SendLine(line);
}

void Logger_ScreenRx(const uint8_t *data, uint16_t len)
{
#if APP_LOG_ENABLE
    char line[LOGGER_LINE_SIZE];
    char ascii[17];
    uint16_t offset = 0U;

    if (data == 0 || len == 0U) {
        return;
    }

    while (offset < len) {
        uint16_t chunk = (uint16_t)(len - offset);
        if (chunk > 16U) {
            chunk = 16U;
        }

        Logger_FormatPrefix(line, sizeof(line), "RX3");
        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       "len=%u off=%u hex=",
                       len,
                       offset);

        for (uint16_t i = 0U; i < chunk; i++) {
            uint8_t byte = data[offset + i];
            (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                           "%02X",
                           byte);
            if (i + 1U < chunk) {
                (void)snprintf(line + strlen(line), sizeof(line) - strlen(line), " ");
            }
            ascii[i] = (byte >= 0x20U && byte <= 0x7EU) ? (char)byte : '.';
        }
        ascii[chunk] = '\0';

        (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                       " ascii=\"%s\"\r\n",
                       ascii);
        Logger_SendLine(line);
        offset = (uint16_t)(offset + chunk);
    }
#else
    (void)data;
    (void)len;
#endif
}

void Logger_Move(uint8_t step_target_pos,
                 int8_t current_pos,
                 int8_t final_target_pos,
                 uint8_t direction,
                 uint16_t motor_speed,
                 uint32_t limit_ms,
                 uint16_t pgmask)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "MOVE");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "step_target=%s step_index=%u final_target=%s final_index=%d current_index=%d mode=%s sensor_pg=PG%u dir=%s motor_speed=%u limit_ms=%lu pgmask=0x%04X\r\n",
                   Logger_ZPosName(step_target_pos),
                   step_target_pos,
                   Logger_ZPosName((uint8_t)final_target_pos),
                   final_target_pos,
                   current_pos,
                   Logger_ZPosSensorPG(step_target_pos) ? "SENSOR" : "TIME",
                   Logger_ZPosSensorPG(step_target_pos),
                   Logger_DirectionName(direction),
                   motor_speed,
                   (unsigned long)limit_ms,
                   pgmask);
    Logger_SendLine(line);
}

void Logger_AutoPlan(uint16_t volume_ml,
                     uint16_t machine_volume_ml,
                     uint16_t reserved_ml,
                     uint8_t aspirate_count,
                     uint8_t spray_count,
                     uint8_t trim10,
                     uint8_t speed_percent)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "AUTO");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "plan volume_ml=%u machine_ml=%u reserved_ml=%u asp_count=%u spray_count=%u trim10=%u pump_speed=%u%%\r\n",
                   volume_ml,
                   machine_volume_ml,
                   reserved_ml,
                   aspirate_count,
                   spray_count,
                   trim10,
                   speed_percent);
    Logger_SendLine(line);
}

void Logger_PhaseStart(const char *tag,
                       uint8_t stage,
                       uint8_t total,
                       uint8_t target_pos,
                       uint32_t duration_ms,
                       uint8_t speed_percent,
                       uint16_t pgmask)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0) {
        return;
    }

    Logger_FormatPrefix(line, sizeof(line), tag);
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "start stage=%u/%u target_pos=%s target_index=%u sensor_pg=PG%u duration_ms=%lu pump_speed=%u%% pgmask=0x%04X\r\n",
                   stage,
                   total,
                   Logger_ZPosName(target_pos),
                   target_pos,
                   Logger_ZPosSensorPG(target_pos),
                   (unsigned long)duration_ms,
                   speed_percent,
                   pgmask);
    Logger_SendLine(line);
}

void Logger_PhaseDone(const char *tag,
                      uint8_t stage,
                      uint32_t elapsed_ms,
                      uint16_t pgmask)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0) {
        return;
    }

    Logger_FormatPrefix(line, sizeof(line), tag);
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "done stage=%u elapsed_ms=%lu pgmask=0x%04X\r\n",
                   stage,
                   (unsigned long)elapsed_ms,
                   pgmask);
    Logger_SendLine(line);
}

void Logger_SprayPumpStop(uint8_t stage,
                          uint8_t pump,
                          uint32_t duration_ms,
                          uint32_t elapsed_ms,
                          uint8_t active_mask,
                          uint16_t pgmask)
{
    char line[LOGGER_LINE_SIZE];

    Logger_FormatPrefix(line, sizeof(line), "SPRAY");
    (void)snprintf(line + strlen(line), sizeof(line) - strlen(line),
                   "pump_stop stage=%u pump=%u duration_ms=%lu elapsed_ms=%lu active_mask=0x%02X pgmask=0x%04X\r\n",
                   stage,
                   pump,
                   (unsigned long)duration_ms,
                   (unsigned long)elapsed_ms,
                   active_mask,
                   pgmask);
    Logger_SendLine(line);
}
