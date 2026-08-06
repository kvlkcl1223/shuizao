#include "pump.h"
#include "app_config.h"
#include "motor.h"

/*
 * pump.c
 * 蠕动泵控制实现。
 * 通过 app_config.c 中的电机映射，将 6 路 DRV8870 包装成统一的泵组动作。
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

void Pump_RunAll(Pump_Direction direction, uint8_t speed_percent)
{
    uint16_t speed = Pump_SpeedPercentToMotorSpeed(speed_percent);
    uint8_t motor_direction;

    if (direction == PUMP_DIR_IN) {
        motor_direction = APP_PUMP_IN_DIRECTION;
    } else if (direction == PUMP_DIR_OUT) {
        motor_direction = APP_PUMP_OUT_DIRECTION;
    } else {
        Pump_StopAll();
        return;
    }

    /* 当前工艺要求 6 路泵同速同向动作，不再做 v1 的单泵校准延时。 */
    for (uint8_t i = 0; i < APP_PUMP_COUNT; i++) {
        Motor_Run(APP_PUMP_MOTOR_IDS[i], motor_direction, speed);
    }
}

void Pump_StopAll(void)
{
    /* 蠕动泵停止时使用刹车模式，停得更干脆。 */
    for (uint8_t i = 0; i < APP_PUMP_COUNT; i++) {
        Motor_Brake(APP_PUMP_MOTOR_IDS[i]);
    }
}
