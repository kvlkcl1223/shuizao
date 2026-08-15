#include "logger.h"
#include "app_config.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/*
 * logger.c
 * USART2 调试日志实现。
 * 注意：不要在中断中调用本模块，避免阻塞中断；业务日志统一在主循环状态机中输出。
 */

#define LOGGER_LINE_SIZE 128U

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

void Logger_Init(void)
{
    Logger_Info("BOOT", "logger ready");
}

void Logger_Info(const char *tag, const char *message)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0 || message == 0) {
        return;
    }

    (void)snprintf(line, sizeof(line), "[%s] %s\r\n", tag, message);
    Logger_SendLine(line);
}

void Logger_Value(const char *tag, const char *name, uint32_t value)
{
    char line[LOGGER_LINE_SIZE];

    if (tag == 0 || name == 0) {
        return;
    }

    (void)snprintf(line, sizeof(line), "[%s] %s=%lu\r\n",
                   tag,
                   name,
                   (unsigned long)value);
    Logger_SendLine(line);
}

void Logger_State(uint8_t state)
{
    char line[LOGGER_LINE_SIZE];

    (void)snprintf(line, sizeof(line), "[STATE] code=%u\r\n", state);
    Logger_SendLine(line);
}

void Logger_Alarm(uint16_t alarm)
{
    char line[LOGGER_LINE_SIZE];

    (void)snprintf(line, sizeof(line), "[ALARM] code=%u\r\n", alarm);
    Logger_SendLine(line);
}

void Logger_Command(const Protocol_Command *command)
{
    char line[LOGGER_LINE_SIZE];

    if (command == 0) {
        return;
    }

    switch (command->type) {
    case PROTOCOL_CMD_START:
        (void)snprintf(line, sizeof(line),
                       "[CMD] START volume=%u keep10=%u\r\n",
                       command->volume_ml,
                       command->keep10);
        break;

    case PROTOCOL_CMD_SPEED_SET:
        (void)snprintf(line, sizeof(line),
                       "[CMD] SPD percent=%u\r\n",
                       command->speed_percent);
        break;

    case PROTOCOL_CMD_SET_PARAM:
        (void)snprintf(line, sizeof(line),
                       "[CMD] SET param=%s value=%lu\r\n",
                       Logger_ParamName(command->param_target),
                       (unsigned long)command->param_value);
        break;

    case PROTOCOL_CMD_MANUAL:
        (void)snprintf(line, sizeof(line),
                       "[CMD] MAN target=%s action=%s speed=%u\r\n",
                       Logger_ManualTargetName(command->manual_target),
                       Logger_ManualActionName(command->manual_action),
                       command->speed_percent);
        break;

    default:
        (void)snprintf(line, sizeof(line),
                       "[CMD] %s\r\n",
                       Logger_CommandName(command->type));
        break;
    }

    Logger_SendLine(line);
}
