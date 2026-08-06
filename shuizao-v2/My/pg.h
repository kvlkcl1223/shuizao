#ifndef __PG_H__
#define __PG_H__

/*
 * pg.h
 * 光电传感器读取接口。
 * 统一封装 PG1~PG16，屏蔽 GPIO 端口细节，并按低电平有效输出布尔状态。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* 光电传感器总数。硬件上 PG1~PG16 都接入到 GPIO 输入。 */
#define PG_COUNT 16U

/* PG 编号采用 0 基枚举，PG_1 对应实际丝印/文档中的 PG1。 */
typedef enum {
    PG_1 = 0,
    PG_2,
    PG_3,
    PG_4,
    PG_5,
    PG_6,
    PG_7,
    PG_8,
    PG_9,
    PG_10,
    PG_11,
    PG_12,
    PG_13,
    PG_14,
    PG_15,
    PG_16,
    PG_INVALID = 0xFF
} PG_ID;

/* 判断 PG 编号是否合法。 */
bool PG_IsValid(PG_ID id);

/* 读取原始 GPIO 电平，不做有效电平转换。 */
GPIO_PinState PG_ReadRaw(PG_ID id);

/* 读取 PG 是否触发。当前硬件定义为低电平有效。 */
bool PG_IsActive(PG_ID id);

/* 返回 16 路 PG 有效掩码，bit0=PG1，bit15=PG16，1 表示有效。 */
uint16_t PG_ReadMask(void);

/* 将 1~16 的数字转换为 PG_ID；非法数字返回 PG_INVALID。 */
PG_ID PG_FromNumber(uint8_t number);

/* 将 PG_ID 转换回 1~16 的数字；非法 ID 返回 0。 */
uint8_t PG_ToNumber(PG_ID id);

/* 在指定 PG 列表中查找某个 PG 的索引，用于根据位置顺序判断运动方向。 */
int8_t PG_FindIndexInList(PG_ID id, const PG_ID *list, uint8_t count);

/* 返回指定 PG 列表中当前第一个有效位置的索引；无有效位置返回 -1。 */
int8_t PG_GetActiveIndexInList(const PG_ID *list, uint8_t count);

/* 按 APP_Z_ORDER 配置表判断当前 Z 轴所在位置索引。 */
int8_t PG_GetCurrentZIndex(void);

#ifdef __cplusplus
}
#endif

#endif /* __PG_H__ */
