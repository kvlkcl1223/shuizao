#ifndef __SCREEN_H__
#define __SCREEN_H__

/*
 * screen.h
 * 陶晶驰串口屏发送接口。
 * MCU 向屏幕发送原生命令，屏幕到 MCU 的控制命令由 protocol 模块接收。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 串口屏通信层。所有发送都走 USART3，并自动追加 0xFF 0xFF 0xFF。 */
void Screen_Init(void);

/* 发送一条陶晶驰原生命令，例如 page work 或 n0.val=10。 */
void Screen_Command(const char *command);

/* 设置文本控件内容。text 建议使用 ASCII，中文显示可交给 HMI 内部文本资源。 */
void Screen_SetText(const char *object_name, const char *text);

/* 设置数值控件内容。 */
void Screen_SetValue(const char *object_name, int32_t value);

/* 更新默认状态文本控件。 */
void Screen_ShowMessage(const char *text);

/* 更新默认报警码控件。 */
void Screen_ShowAlarm(uint16_t alarm_code);

/* 跳转到警告页面。页面名由 APP_SCREEN_WARNING_PAGE 配置。 */
void Screen_ShowWarningPage(void);

/* 同步当前体积档位和该档位 6 个喷淋补偿时间到 HMI。 */
void Screen_UpdateSprayTimes(const uint32_t *spray_ms, uint16_t volume_ml);

/* 同步 4 个 Z 轴虚拟位置时间到 HMI 的滑轴和数值控件。 */
void Screen_UpdateZVirtTimes(const uint32_t *zvirt_ms);

/* 同步当前吸取/手动泵速到 HMI。 */
void Screen_UpdatePumpSpeed(uint8_t speed_percent);

/* 批量刷新状态、阶段、速度、PG 掩码、预留 10ml 标志、报警和进度。 */
void Screen_UpdateStatus(uint8_t state,
                         uint8_t phase,
                         uint8_t speed_percent,
                         uint16_t pg_mask,
                         uint8_t keep10,
                         uint16_t alarm_code,
                         uint8_t progress_percent);

#ifdef __cplusplus
}
#endif

#endif /* __SCREEN_H__ */
