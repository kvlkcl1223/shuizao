#ifndef __SETTINGS_H__
#define __SETTINGS_H__

/*
 * settings.h
 * 运行参数保存接口。
 * 当前只保存 6 个蠕动泵喷淋补偿时间，保存位置为 MCU 片内 Flash 最后一页。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_config.h"
#include <stdbool.h>
#include <stdint.h>

/* 从 Flash 读取喷淋补偿时间。校验失败时返回 false，调用方继续使用默认值。 */
bool Settings_LoadSprayMs(uint32_t spray_ms[APP_PUMP_COUNT]);

/* 将当前 RAM 中的喷淋补偿时间写入 Flash。成功返回 true。 */
bool Settings_SaveSprayMs(const uint32_t spray_ms[APP_PUMP_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* __SETTINGS_H__ */
