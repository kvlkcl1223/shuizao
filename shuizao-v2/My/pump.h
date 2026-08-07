#ifndef __PUMP_H__
#define __PUMP_H__

/*
 * pump.h
 * 蠕动泵控制接口。
 * 当前设备有 6 路蠕动泵：吸取流程仍按整组同开同停，喷淋流程可按单泵补偿时间分别停止。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 蠕动泵方向。IN=吸取，OUT=喷淋/排出，STOP=停止。 */
typedef enum {
    PUMP_DIR_STOP = 0,
    PUMP_DIR_IN,
    PUMP_DIR_OUT
} Pump_Direction;

/* 初始化泵控制层，当前行为是停止全部蠕动泵。 */
void Pump_Init(void);

/* 将屏幕输入速度限制在 10%~100%。 */
uint8_t Pump_ClampSpeedPercent(uint8_t speed_percent);

/* 将百分比速度换算为 motor.c 接口使用的 0~1000 速度值。 */
uint16_t Pump_SpeedPercentToMotorSpeed(uint8_t speed_percent);

/* 单路蠕动泵运行。pump_index 为 0~5，对应泵 1~6。 */
void Pump_RunOne(uint8_t pump_index, Pump_Direction direction, uint8_t speed_percent);

/* 停止单路蠕动泵。pump_index 为 0~5，对应泵 1~6。 */
void Pump_StopOne(uint8_t pump_index);

/* 6 路蠕动泵同时按同一方向、同一速度运行。 */
void Pump_RunAll(Pump_Direction direction, uint8_t speed_percent);

/* 停止全部蠕动泵。当前采用 DRV8870 刹车模式。 */
void Pump_StopAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __PUMP_H__ */
