#ifndef __LOGGER_H__
#define __LOGGER_H__

/*
 * logger.h
 * USART2 调试日志接口。
 * 日志只用于真机调试观察，不参与 HMI 通信和业务控制。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "protocol.h"
#include <stdint.h>

/* 初始化日志模块，当前实现无额外硬件初始化，USART2 由 CubeMX 初始化。 */
void Logger_Init(void);

/* 输出普通文本日志，格式为：[TAG] message。 */
void Logger_Info(const char *tag, const char *message);

/* 输出一个无符号数值，格式为：[TAG] name=value。 */
void Logger_Value(const char *tag, const char *name, uint32_t value);

/* 输出状态切换日志。 */
void Logger_State(uint8_t state);

/* 输出报警日志。 */
void Logger_Alarm(uint16_t alarm);

/* 输出已经解析成功并交给应用层处理的屏幕命令。 */
void Logger_Command(const Protocol_Command *command);

#ifdef __cplusplus
}
#endif

#endif /* __LOGGER_H__ */
