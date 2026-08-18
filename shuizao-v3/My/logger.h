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

/* 输出状态切换日志，包含状态码和英文状态名。 */
void Logger_State(uint8_t state);

/* 输出报警日志，包含报警码和英文报警名。 */
void Logger_Alarm(uint16_t alarm);

/* 输出带现场上下文的报警日志，便于定位超时或配置问题。 */
void Logger_AlarmDetail(uint16_t alarm,
                        uint8_t state,
                        uint8_t target_pos,
                        uint16_t pgmask,
                        uint32_t elapsed_ms,
                        uint32_t timeout_ms);

/* 输出已经解析成功并交给应用层处理的屏幕命令。 */
void Logger_Command(const Protocol_Command *command);

/* 输出 Z 轴相邻步进目标、最终目标、方向和 PG 掩码。 */
void Logger_Move(uint8_t step_target_pos,
                 int8_t current_pos,
                 int8_t final_target_pos,
                 uint8_t direction,
                 uint16_t motor_speed,
                 uint32_t limit_ms,
                 uint16_t pgmask);

/* 输出自动流程计划摘要。 */
void Logger_AutoPlan(uint16_t volume_ml,
                     uint16_t machine_volume_ml,
                     uint16_t reserved_ml,
                     uint8_t aspirate_count,
                     uint8_t spray_count,
                     uint8_t trim10,
                     uint8_t speed_percent);

/* 输出吸取、补吸、喷淋等阶段开始信息。 */
void Logger_PhaseStart(const char *tag,
                       uint8_t stage,
                       uint8_t total,
                       uint8_t target_pos,
                       uint32_t duration_ms,
                       uint8_t speed_percent,
                       uint16_t pgmask);

/* 输出吸取、补吸、喷淋等阶段完成信息。 */
void Logger_PhaseDone(const char *tag,
                      uint8_t stage,
                      uint32_t elapsed_ms,
                      uint16_t pgmask);

/* 输出喷淋单泵补偿停止信息。 */
void Logger_SprayPumpStop(uint8_t stage,
                          uint8_t pump,
                          uint32_t duration_ms,
                          uint32_t elapsed_ms,
                          uint8_t active_mask,
                          uint16_t pgmask);

#ifdef __cplusplus
}
#endif

#endif /* __LOGGER_H__ */
