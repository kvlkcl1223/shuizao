#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

/*
 * protocol.h
 * 屏幕到 MCU 的控制协议接口。
 * 负责把 "#START,100,1;" 这类文本帧解析成结构化命令。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "usart.h"
#include <stdbool.h>
#include <stdint.h>

/* 屏幕到 MCU 的命令类型，对应 docs/serial_protocol.md 中的 #命令;。 */
typedef enum {
    PROTOCOL_CMD_NONE = 0,
    PROTOCOL_CMD_START,
    PROTOCOL_CMD_STOP,
    PROTOCOL_CMD_ESTOP,
    PROTOCOL_CMD_HOME,
    PROTOCOL_CMD_OK,
    PROTOCOL_CMD_SPEED_SET,
    PROTOCOL_CMD_MANUAL,
    PROTOCOL_CMD_GET_PG,
    PROTOCOL_CMD_GET_STATE,
    PROTOCOL_CMD_RESET
} Protocol_CommandType;

/* 手动控制目标：Z 轴或全部蠕动泵。 */
typedef enum {
    PROTOCOL_MANUAL_TARGET_NONE = 0,
    PROTOCOL_MANUAL_TARGET_Z,
    PROTOCOL_MANUAL_TARGET_PUMP
} Protocol_ManualTarget;

/* 手动控制动作。Z 轴使用 UP/DOWN/STOP，泵使用 IN/OUT/STOP。 */
typedef enum {
    PROTOCOL_MANUAL_ACTION_NONE = 0,
    PROTOCOL_MANUAL_ACTION_UP,
    PROTOCOL_MANUAL_ACTION_DOWN,
    PROTOCOL_MANUAL_ACTION_IN,
    PROTOCOL_MANUAL_ACTION_OUT,
    PROTOCOL_MANUAL_ACTION_STOP
} Protocol_ManualAction;

/* 协议解析后的统一命令结构，应用层只读取这个结构，不直接解析字符串。 */
typedef struct {
    Protocol_CommandType type;
    uint16_t volume_ml;
    uint8_t keep10;
    uint8_t speed_percent;
    Protocol_ManualTarget manual_target;
    Protocol_ManualAction manual_action;
} Protocol_Command;

/* 初始化 USART3 DMA 接收和 IDLE 中断。 */
void Protocol_Init(void);

/* USART3 中断回调入口，只搬运接收数据，不执行业务动作。 */
void Protocol_UART_IRQHandler(UART_HandleTypeDef *huart);

/* 在主循环中调用，解析 DMA 收到的数据帧。 */
void Protocol_Process(void);

/* 从命令队列取出一条已解析命令。 */
bool Protocol_PopCommand(Protocol_Command *command);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H__ */
