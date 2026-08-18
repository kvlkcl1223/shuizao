#include "pump.h"
#include "app_config.h"
#include "motor.h"
#include <stdbool.h>

/*
 * pump.c
 * 蠕动泵控制实现。
 * 通过 app_config.c 中的电机映射，将 6 路 DRV8870 包装成整组动作和单泵动作两类接口。
 */

void Pump_Init(void)
{
    /* 上电默认停泵，避免初始化阶段误动作。 */
    Pump_StopAll();
}

uint8_t Pump_ClampSpeedPercent(uint8_t speed_percent)
{
    /* 用户界面显示范围为 10%~100%，下限避免蠕动泵低速失步或无流量。 */
    if (speed_percent < APP_MIN_PUMP_SPEED_PERCENT) {
        return APP_MIN_PUMP_SPEED_PERCENT;
    }
    if (speed_percent > APP_MAX_PUMP_SPEED_PERCENT) {
        return APP_MAX_PUMP_SPEED_PERCENT;
    }

    return speed_percent;
}

uint16_t Pump_SpeedPercentToMotorSpeed(uint8_t speed_percent)
{
    uint8_t clamped = Pump_ClampSpeedPercent(speed_percent);

    /* motor.c 内部速度范围为 0~1000，这里做百分比到内部速度的换算。 */
    return (uint16_t)((uint32_t)clamped * MOTOR_SPEED_MAX / 100U);
}

static bool Pump_GetMotorDirection(Pump_Direction direction, uint8_t *motor_direction)
{
    if (motor_direction == 0) {
        return false;
    }

    if (direction == PUMP_DIR_IN) {
        *motor_direction = APP_PUMP_IN_DIRECTION;
        return true;
    }

    if (direction == PUMP_DIR_OUT) {
        *motor_direction = APP_PUMP_OUT_DIRECTION;
        return true;
    }

    return false;
}

void Pump_RunOne(uint8_t pump_index, Pump_Direction direction, uint8_t speed_percent)
{
    uint16_t speed = Pump_SpeedPercentToMotorSpeed(speed_percent);
    uint8_t motor_direction;

    if (pump_index >= APP_PUMP_COUNT) {
        return;
    }

    if (!Pump_GetMotorDirection(direction, &motor_direction)) {
        Pump_StopOne(pump_index);
        return;
    }

    Motor_Run(APP_PUMP_MOTOR_IDS[pump_index], motor_direction, speed);
}

void Pump_StopOne(uint8_t pump_index)
{
    if (pump_index >= APP_PUMP_COUNT) {
        return;
    }

    Motor_Brake(APP_PUMP_MOTOR_IDS[pump_index]);
}

void Pump_RunAll(Pump_Direction direction, uint8_t speed_percent)
{
    /* 吸取和手动整组泵动作仍保持 6 路同速同向。 */
    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        Pump_RunOne(i, direction, speed_percent);
    }
}

void Pump_StopAll(void)
{
    /* 蠕动泵停止时使用刹车模式，停得更干脆。 */
    for (uint8_t i = 0U; i < APP_PUMP_COUNT; i++) {
        Pump_StopOne(i);
    }
}
