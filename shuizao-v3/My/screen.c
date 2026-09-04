#include "screen.h"
#include "app_config.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/*
 * screen.c
 * 串口屏输出实现。
 * 所有命令通过 USART3 发送，并自动补齐陶晶驰要求的三个 0xFF 结束符。
 */

static void Screen_SendEnd(void)
{
    /* 陶晶驰/Nextion 指令结束符固定为三个 0xFF。 */
    uint8_t end_bytes[3] = {0xFF, 0xFF, 0xFF};
    HAL_UART_Transmit(&huart3, end_bytes, sizeof(end_bytes), 100U);
}

void Screen_Init(void)
{
    /* 文本状态由 HMI 根据 n_state/n_alarm 自行映射，MCU 不再写文本状态控件。 */
}

void Screen_Command(const char *command)
{
    if (command == 0) {
        return;
    }

    /* 所有屏幕输出走 USART3，与协议接收使用同一个串口。 */
    HAL_UART_Transmit(&huart3, (uint8_t *)command, strlen(command), 100U);
    Screen_SendEnd();
}

void Screen_SetText(const char *object_name, const char *text)
{
    char buffer[96];

    if (object_name == 0 || text == 0) {
        return;
    }

    /* 文本内容目前使用 ASCII 状态词；中文推荐在屏幕工程里根据状态码显示。 */
    sprintf(buffer, "%s.txt=\"%s\"", object_name, text);
    Screen_Command(buffer);
}

void Screen_SetValue(const char *object_name, int32_t value)
{
    char buffer[48];

    if (object_name == 0) {
        return;
    }

    sprintf(buffer, "%s.val=%ld", object_name, (long)value);
    Screen_Command(buffer);
}

void Screen_ShowMessage(const char *text)
{
    /* 保留接口兼容旧业务调用，但不再向文本状态控件发送命令。 */
    (void)text;
}

void Screen_ShowAlarm(uint16_t alarm_code)
{
    Screen_SetValue(APP_SCREEN_ALARM_OBJ, alarm_code);
}

void Screen_ShowWarningPage(void)
{
    char buffer[48];

    sprintf(buffer, "page %s", APP_SCREEN_WARNING_PAGE);
    Screen_Command(buffer);
}

void Screen_ShowLeakWarningPage(void)
{
    char buffer[48];

    sprintf(buffer, "page %s", APP_SCREEN_LEAK_WARNING_PAGE);
    Screen_Command(buffer);
}

void Screen_UpdateSprayTimes(const uint32_t *spray_ms, uint16_t volume_ml)
{
    char volume_text[16];
    static const char *slider_objs[APP_PUMP_COUNT] = {
        APP_SCREEN_SPRAY1_SLIDER_OBJ,
        APP_SCREEN_SPRAY2_SLIDER_OBJ,
        APP_SCREEN_SPRAY3_SLIDER_OBJ,
        APP_SCREEN_SPRAY4_SLIDER_OBJ,
        APP_SCREEN_SPRAY5_SLIDER_OBJ,
        APP_SCREEN_SPRAY6_SLIDER_OBJ
    };
    static const char *value_objs[APP_PUMP_COUNT] = {
        APP_SCREEN_SPRAY1_VALUE_OBJ,
        APP_SCREEN_SPRAY2_VALUE_OBJ,
        APP_SCREEN_SPRAY3_VALUE_OBJ,
        APP_SCREEN_SPRAY4_VALUE_OBJ,
        APP_SCREEN_SPRAY5_VALUE_OBJ,
        APP_SCREEN_SPRAY6_VALUE_OBJ
    };

    if (spray_ms == 0) {
        return;
    }

    sprintf(volume_text, "%uml", volume_ml);
    Screen_SetValue(APP_SCREEN_SPRAY_VOLUME_VALUE_OBJ, volume_ml);
    Screen_SetText(APP_SCREEN_SPRAY_VOLUME_TEXT_OBJ, volume_text);

    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        Screen_SetValue(slider_objs[i], (int32_t)spray_ms[i]);
        Screen_SetValue(value_objs[i], (int32_t)spray_ms[i]);
    }
}

void Screen_UpdateZVirtTimes(const uint32_t *zvirt_ms)
{
    static const char *slider_objs[APP_ZVIRT_COUNT] = {
        APP_SCREEN_Z_DN_HOME_800_SLIDER_OBJ,
        APP_SCREEN_Z_DN_800_300_SLIDER_OBJ,
        APP_SCREEN_Z_UP_300_800_SLIDER_OBJ,
        APP_SCREEN_Z_UP_200_300_SLIDER_OBJ
    };
    static const char *value_objs[APP_ZVIRT_COUNT] = {
        APP_SCREEN_Z_DN_HOME_800_VALUE_OBJ,
        APP_SCREEN_Z_DN_800_300_VALUE_OBJ,
        APP_SCREEN_Z_UP_300_800_VALUE_OBJ,
        APP_SCREEN_Z_UP_200_300_VALUE_OBJ
    };

    if (zvirt_ms == 0) {
        return;
    }

    for (uint8_t i = 0U; i < APP_ZVIRT_COUNT; i++) {
        Screen_SetValue(slider_objs[i], (int32_t)zvirt_ms[i]);
        Screen_SetValue(value_objs[i], (int32_t)zvirt_ms[i]);
    }
}

void Screen_UpdatePumpSpeed(uint8_t speed_percent)
{
    Screen_SetValue(APP_SCREEN_SPEED_OBJ, speed_percent);
    Screen_SetValue(APP_SCREEN_SPEED_SLIDER_OBJ, speed_percent);
}

void Screen_UpdateManualPumpSpeed(uint8_t speed_percent)
{
    Screen_SetValue(APP_SCREEN_MANUAL_SPEED_OBJ, speed_percent);
    Screen_SetValue(APP_SCREEN_MANUAL_SPEED_SLIDER_OBJ, speed_percent);
}

void Screen_UpdateStatus(uint8_t state,
                         uint8_t phase,
                         uint8_t speed_percent,
                         uint16_t pg_mask,
                         uint8_t keep10,
                         uint16_t alarm_code,
                         uint8_t progress_percent)
{
    /* 批量更新屏幕数值控件，控件名集中在 app_config.h 中配置。 */
    Screen_SetValue(APP_SCREEN_STATE_OBJ, state);
    Screen_SetValue(APP_SCREEN_PHASE_OBJ, phase);
    Screen_SetValue(APP_SCREEN_SPEED_OBJ, speed_percent);
    Screen_SetValue(APP_SCREEN_PGMASK_OBJ, pg_mask);
    Screen_SetValue(APP_SCREEN_KEEP10_OBJ, keep10);
    Screen_SetValue(APP_SCREEN_ALARM_OBJ, alarm_code);
    Screen_SetValue(APP_SCREEN_PROGRESS_OBJ, progress_percent);
}
