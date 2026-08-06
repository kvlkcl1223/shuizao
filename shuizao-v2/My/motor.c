/**
  ******************************************************************************
  * @file    motor.c
  * @brief   DRV8870 电机驱动实现
  *          PWM 频率 20kHz, 慢衰减模式
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "motor.h"
#include "tim.h"

/* 常量 --------------------------------------------------------------------*/

#define PWM_PERIOD      3599U   /* ARR: 72MHz / 3600 = 20kHz          */
#define PWM_FULL_HIGH   (PWM_PERIOD + 1U)  /* CCR > ARR → 恒高 (制动分量) */
#define PWM_FULL_LOW    0U      /* CCR = 0 → 恒低 (驱动分量)          */

/* ---- DRV8870 控制逻辑 (慢衰减/同步整流) ----
 *
 *   IN1=H, IN2=L  →  正转驱动 (OUT1→电机→OUT2→GND)
 *   IN1=L, IN2=H  →  反转驱动 (OUT2→电机→OUT1→GND)
 *   IN1=L, IN2=L  →  Coast (滑行)
 *   IN1=H, IN2=H  →  Brake (刹车 / 慢衰减)
 *
 *   采用"一路恒高 + 一路 PWM"模式:
 *     正转: IN1=恒高, IN2=PWM (低电平时间 → 驱动; 高电平时间 → 制动)
 *     反转: IN1=PWM, IN2=恒高
 *     IN2 CCR 越小 → 低电平时间越长 → 驱动越多 → 转速越快
 */

/* 电机设备结构体 ----------------------------------------------------------*/
typedef struct {
    TIM_HandleTypeDef *htim;     /* 所属定时器           */
    uint32_t           ch_in1;   /* IN1 通道             */
    uint32_t           ch_in2;   /* IN2 通道             */
    uint8_t            dir;      /* 当前方向             */
    uint16_t           speed;    /* 当前转速 (0~1000)    */
} Motor_Device;

/* 电机通道映射表 ----------------------------------------------------------*/
static Motor_Device motor_table[MOTOR_COUNT] = {
    /* id         TIM      IN1            IN2            引脚          */
    { &htim1, TIM_CHANNEL_1, TIM_CHANNEL_2, MOTOR_STOP, 0 }, /* M1  PA8 / PA9   */
    { &htim1, TIM_CHANNEL_3, TIM_CHANNEL_4, MOTOR_STOP, 0 }, /* M2  PA10/ PA11  */
    { &htim2, TIM_CHANNEL_1, TIM_CHANNEL_2, MOTOR_STOP, 0 }, /* M3  PA15/ PB3   */
    { &htim2, TIM_CHANNEL_3, TIM_CHANNEL_4, MOTOR_STOP, 0 }, /* M4  PB10/ PB11  */
    { &htim3, TIM_CHANNEL_1, TIM_CHANNEL_2, MOTOR_STOP, 0 }, /* M5  PA6 / PA7   */
    { &htim3, TIM_CHANNEL_3, TIM_CHANNEL_4, MOTOR_STOP, 0 }, /* M6  PB0 / PB1   */
    { &htim4, TIM_CHANNEL_1, TIM_CHANNEL_2, MOTOR_STOP, 0 }, /* M7  PB6 / PB7   */
    { &htim4, TIM_CHANNEL_3, TIM_CHANNEL_4, MOTOR_STOP, 0 }, /* M8  PB8 / PB9   */
};

/* 内部辅助函数 ------------------------------------------------------------*/

/**
  * @brief  将用户转速 (0~1000) 映射为 PWM 比较值 (0~3599)
  */
static uint16_t SpeedToCCR(uint16_t speed)
{
    return (uint32_t)speed * PWM_PERIOD / MOTOR_SPEED_MAX;
}

/**
  * @brief  配置单路电机的 CCR 寄存器 (底层)
  * @param  m       电机对象
  * @param  ccr_in1 IN1 通道的比较值
  * @param  ccr_in2 IN2 通道的比较值
  */
static void Motor_SetCCR(const Motor_Device *m, uint16_t ccr_in1, uint16_t ccr_in2)
{
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, ccr_in1);
    __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, ccr_in2);
}

/* 公共 API ----------------------------------------------------------------*/

/**
  * @brief  初始化全部 8 路电机
  *         - 重配定时器 ARR 为 3599 (20kHz)
  *         - 清除所有 CCR
  *         - 启动全部 16 路 PWM 通道
  */
void Motor_Init(void)
{
    /* 收集 4 个定时器句柄 */
    TIM_HandleTypeDef *htim_list[4] = { &htim1, &htim2, &htim3, &htim4 };

    for (int i = 0; i < 4; i++)
    {
        TIM_HandleTypeDef *htim = htim_list[i];

        /* 1. 先停止全部通道 (安全操作) */
        HAL_TIM_PWM_Stop(htim, TIM_CHANNEL_1);
        HAL_TIM_PWM_Stop(htim, TIM_CHANNEL_2);
        HAL_TIM_PWM_Stop(htim, TIM_CHANNEL_3);
        HAL_TIM_PWM_Stop(htim, TIM_CHANNEL_4);

        /* 2. 修改 ARR 为 3599 (20kHz) */
        htim->Instance->ARR = PWM_PERIOD;
        htim->Init.Period = PWM_PERIOD; /* 同步句柄中的记录值 */

        /* 3. 清零全部通道比较值 (初始 = 全低 = Coast) */
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, 0);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, 0);
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_4, 0);

        /* 4. 生成更新事件, 将 ARR / CCR 立即装入影子寄存器 */
        htim->Instance->EGR = TIM_EGR_UG;

        /* 5. 启动全部 4 路 PWM 输出 */
        HAL_TIM_PWM_Start(htim, TIM_CHANNEL_1);
        HAL_TIM_PWM_Start(htim, TIM_CHANNEL_2);
        HAL_TIM_PWM_Start(htim, TIM_CHANNEL_3);
        HAL_TIM_PWM_Start(htim, TIM_CHANNEL_4);
    }
}

/**
  * @brief  运行电机
  * @param  id        电机编号
  * @param  direction MOTOR_STOP / MOTOR_FORWARD / MOTOR_REVERSE
  * @param  speed     转速 0~1000
  */
void Motor_Run(uint8_t id, uint8_t direction, uint16_t speed)
{
    if (id >= MOTOR_COUNT || speed > MOTOR_SPEED_MAX)
        return;

    Motor_Device *m = &motor_table[id];

    if (direction == MOTOR_STOP)
    {
        Motor_Stop(id);
        return;
    }

    m->dir   = direction;
    m->speed = speed;

    uint16_t pwm_val = SpeedToCCR(speed);           /* 0 ~ 3599              */

    if (direction == MOTOR_FORWARD)
    {
        /* 正转: IN1=恒高, IN2=PWM (CCR↓ → 低电平↑ → 驱动↑ → 更快)     */
        uint16_t ccr_in2 = PWM_PERIOD - pwm_val;    /* speed=0→3599(恒高=刹车) */
        Motor_SetCCR(m, PWM_FULL_HIGH, ccr_in2);     /* speed=MAX→0(恒低=全速) */
    }
    else /* MOTOR_REVERSE */
    {
        /* 反转: IN1=PWM, IN2=恒高                                   */
        uint16_t ccr_in1 = PWM_PERIOD - pwm_val;
        Motor_SetCCR(m, ccr_in1, PWM_FULL_HIGH);
    }
}

/**
  * @brief  停止电机 (Coast 滑行模式, 两路全低)
  */
void Motor_Stop(uint8_t id)
{
    Motor_Coast(id);
}

void Motor_Coast(uint8_t id)
{
    if (id >= MOTOR_COUNT)
        return;

    Motor_Device *m = &motor_table[id];
    m->dir   = MOTOR_STOP;
    m->speed = 0;

    Motor_SetCCR(m, PWM_FULL_LOW, PWM_FULL_LOW);
}

void Motor_Brake(uint8_t id)
{
    if (id >= MOTOR_COUNT)
        return;

    Motor_Device *m = &motor_table[id];
    m->dir   = MOTOR_STOP;
    m->speed = 0;

    Motor_SetCCR(m, PWM_FULL_HIGH, PWM_FULL_HIGH);
}

/**
  * @brief  运行时调整转速 (不改变方向)
  */
void Motor_SetSpeed(uint8_t id, uint16_t speed)
{
    if (id >= MOTOR_COUNT || speed > MOTOR_SPEED_MAX)
        return;

    Motor_Device *m = &motor_table[id];

    if (m->dir == MOTOR_STOP)
        return;                    /* 停止状态下忽略调速 */

    m->speed = speed;
    uint16_t pwm_val = SpeedToCCR(speed);

    if (m->dir == MOTOR_FORWARD)
    {
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in2, PWM_PERIOD - pwm_val);
    }
    else /* MOTOR_REVERSE */
    {
        __HAL_TIM_SET_COMPARE(m->htim, m->ch_in1, PWM_PERIOD - pwm_val);
    }
}

/**
  * @brief  Test all 8 motors at the same time.
  * @note   Forward 5s, stop 2s, reverse 5s, then keep reversing.
  */
void Motor_TestAll(void)
{
    for (uint8_t id = 0; id < MOTOR_COUNT; id++)
    {
        Motor_Run(id, MOTOR_FORWARD, MOTOR_SPEED_DEF);
    }
    HAL_Delay(5000);

    for (uint8_t id = 0; id < MOTOR_COUNT; id++)
    {
        Motor_Stop(id);
    }
    HAL_Delay(2000);

    for (uint8_t id = 0; id < MOTOR_COUNT; id++)
    {
        Motor_Run(id, MOTOR_REVERSE, MOTOR_SPEED_DEF);
    }
    HAL_Delay(5000);
}
