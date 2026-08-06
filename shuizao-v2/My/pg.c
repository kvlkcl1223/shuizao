#include "pg.h"
#include "app_config.h"

/*
 * pg.c
 * 光电传感器底层映射和读取实现。
 * 后续若只改 Z 轴物理顺序，应优先修改 app_config.c，不需要改这里。
 */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} PG_PinDef;

/* PG1~PG16 的 GPIO 映射表，顺序必须与 PG_ID 枚举保持一致。 */
static const PG_PinDef pg_table[PG_COUNT] = {
    {PG1_GPIO_Port, PG1_Pin},
    {PG2_GPIO_Port, PG2_Pin},
    {PG3_GPIO_Port, PG3_Pin},
    {PG4_GPIO_Port, PG4_Pin},
    {PG5_GPIO_Port, PG5_Pin},
    {PG6_GPIO_Port, PG6_Pin},
    {PG7_GPIO_Port, PG7_Pin},
    {PG8_GPIO_Port, PG8_Pin},
    {PG9_GPIO_Port, PG9_Pin},
    {PG10_GPIO_Port, PG10_Pin},
    {PG11_GPIO_Port, PG11_Pin},
    {PG12_GPIO_Port, PG12_Pin},
    {PG13_GPIO_Port, PG13_Pin},
    {PG14_GPIO_Port, PG14_Pin},
    {PG15_GPIO_Port, PG15_Pin},
    {PG16_GPIO_Port, PG16_Pin},
};

bool PG_IsValid(PG_ID id)
{
    return id < PG_COUNT;
}

GPIO_PinState PG_ReadRaw(PG_ID id)
{
    /* 非法编号按未触发处理，避免上层误判为有效位置。 */
    if (!PG_IsValid(id)) {
        return GPIO_PIN_SET;
    }

    return HAL_GPIO_ReadPin(pg_table[id].port, pg_table[id].pin);
}

bool PG_IsActive(PG_ID id)
{
    /* 当前光电传感器为低电平有效。 */
    if (!PG_IsValid(id)) {
        return false;
    }

    return PG_ReadRaw(id) == GPIO_PIN_RESET;
}

uint16_t PG_ReadMask(void)
{
    uint16_t mask = 0;

    /* bit0 对应 PG1；掩码中的 1 表示该 PG 当前有效。 */
    for (uint8_t i = 0; i < PG_COUNT; i++) {
        if (PG_IsActive((PG_ID)i)) {
            mask |= (uint16_t)(1U << i);
        }
    }

    return mask;
}

PG_ID PG_FromNumber(uint8_t number)
{
    if (number == 0U || number > PG_COUNT) {
        return PG_INVALID;
    }

    return (PG_ID)(number - 1U);
}

uint8_t PG_ToNumber(PG_ID id)
{
    if (!PG_IsValid(id)) {
        return 0U;
    }

    return (uint8_t)id + 1U;
}

int8_t PG_FindIndexInList(PG_ID id, const PG_ID *list, uint8_t count)
{
    if (list == 0) {
        return -1;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (list[i] == id) {
            return (int8_t)i;
        }
    }

    return -1;
}

int8_t PG_GetActiveIndexInList(const PG_ID *list, uint8_t count)
{
    if (list == 0) {
        return -1;
    }

    /* 如果多个 PG 同时有效，当前返回第一个。后续可在这里扩展异常检测。 */
    for (uint8_t i = 0; i < count; i++) {
        if (PG_IsActive(list[i])) {
            return (int8_t)i;
        }
    }

    return -1;
}

int8_t PG_GetCurrentZIndex(void)
{
    return PG_GetActiveIndexInList(APP_Z_ORDER, APP_Z_ORDER_COUNT);
}
