/**
  ******************************************************************************
  * @file    motor.h
  * @brief   DRV8870 电机驱动库
  *          - 控制 8 路直流电机 (16 路 PWM, 每两路一组)
  *          - PWM 频率: 20kHz
  *          - 支持 正转 / 反转 / 停止 / 调速
  *          - 内置测试功能
  ******************************************************************************
  */

#ifndef __MOTOR_H__
#define __MOTOR_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* 电机编号 ----------------------------------------------------------------*/
typedef enum {
    MOTOR_1 = 0,
    MOTOR_2 = 1,
    MOTOR_3 = 2,
    MOTOR_4 = 3,
    MOTOR_5 = 4,
    MOTOR_6 = 5,
    MOTOR_7 = 6,
    MOTOR_8 = 7,
    MOTOR_COUNT = 8
} Motor_ID;

/* 方向常量 ----------------------------------------------------------------*/
#define MOTOR_STOP    0   /* 停止 (Coast)      */
#define MOTOR_FORWARD 1   /* 正转              */
#define MOTOR_REVERSE 2   /* 反转              */

/* 转速范围 ----------------------------------------------------------------*/
#define MOTOR_SPEED_MIN     0    /* 最低转速 (停转)   */
#define MOTOR_SPEED_MAX  1000    /* 最高转速          */
#define MOTOR_SPEED_DEF   500    /* 默认测试转速       */

/* 公共 API ----------------------------------------------------------------*/

/**
  * @brief  初始化全部 8 路电机
  * @note   将 TIM1/2/3/4 重配置为 20kHz PWM (ARR=3599)
  *         并启动全部 16 路 PWM 通道 (初始均为停止状态)
  */
void Motor_Init(void);

/**
  * @brief  运行电机
  * @param  id        电机编号 (MOTOR_1 ~ MOTOR_8)
  * @param  direction MOTOR_FORWARD / MOTOR_REVERSE / MOTOR_STOP
  * @param  speed     转速 0~1000
  */
void Motor_Run(uint8_t id, uint8_t direction, uint16_t speed);

/**
  * @brief  停止电机 (Coast 模式)
  * @param  id  电机编号
  */
void Motor_Stop(uint8_t id);

void Motor_Coast(uint8_t id);

void Motor_Brake(uint8_t id);

/**
  * @brief  仅调整转速 (不改变方向)
  * @param  id     电机编号
  * @param  speed  转速 0~1000
  * @note   若当前为停止状态, 此函数无操作
  */
void Motor_SetSpeed(uint8_t id, uint16_t speed);

/**
  * @brief  逐路测试全部 8 路电机
  * @note   每路: 正转 5s → 停止 2s → 反转 5s → 停止 2s
  *         总计约 112s, 阻塞运行
  */
void Motor_TestAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H__ */
