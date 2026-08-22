#ifndef __SETTINGS_H__
#define __SETTINGS_H__

/*
 * settings.h
 * 运行参数保存接口。
 * 当前保存 4 个体积档位各 6 个第一段喷淋补偿时间、4 个 Z 轴虚拟位置时间、
 * 以及吸取/手动泵速。
 * 保存位置为 MCU 片内 Flash 最后一页。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include <stdbool.h>
#include <stdint.h>

/* 从 Flash 读取喷淋补偿时间。校验失败时返回 false，调用方继续使用默认值。 */
bool Settings_LoadSprayMs(uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT]);

/* 将当前 RAM 中的所有档位喷淋补偿时间写入 Flash。成功返回 true。 */
bool Settings_SaveSprayMs(const uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT]);

/* 从 Flash 读取全部可保存参数。校验失败时返回 false，调用方继续使用默认值。 */
bool Settings_LoadAll(uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT],
                      uint32_t zvirt_ms[APP_ZVIRT_COUNT],
                      uint8_t *pump_speed_percent);

/* 将全部可保存参数写入 Flash。成功返回 true。 */
bool Settings_SaveAll(const uint32_t spray_ms[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT],
                      const uint32_t zvirt_ms[APP_ZVIRT_COUNT],
                      uint8_t pump_speed_percent);

#ifdef __cplusplus
}
#endif

#endif /* __SETTINGS_H__ */
