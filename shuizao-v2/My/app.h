#ifndef __APP_H__
#define __APP_H__

/*
 * app.h
 * 水藻设备主应用状态机接口。
 * 负责自动流程、手动控制、急停、回原点和屏幕状态刷新。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 应用主状态机。状态码会同步到串口屏 n_state 控件。 */
typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_HOMING,
    APP_STATE_CHECK_Y,
    APP_STATE_MOVE_TO_ASPIRATE,
    APP_STATE_ASPIRATING,
    APP_STATE_MOVE_TO_KEEP10,
    APP_STATE_WAIT_MANUAL_CLEAN,
    APP_STATE_MOVE_TO_SPRAY,
    APP_STATE_SPRAYING,
    APP_STATE_RETURN_HOME,
    APP_STATE_DONE,
    APP_STATE_ERROR,
    APP_STATE_ESTOP,
    APP_STATE_MANUAL
} App_State;

/* 报警码。报警码会同步到串口屏 n_alarm 控件。 */
typedef enum {
    APP_ALARM_NONE = 0,
    APP_ALARM_BUSY = 1,
    APP_ALARM_BAD_VOLUME = 2,
    APP_ALARM_Y_NOT_READY = 3,
    APP_ALARM_Z_TIMEOUT = 4,
    APP_ALARM_BAD_COMMAND = 5
} App_Alarm;

/* 初始化电机、泵、协议和屏幕层。 */
void App_Init(void);

/* 主循环任务入口，必须高频调用，内部不做长时间阻塞延时。 */
void App_Task(void);

/* 调试/查询接口：当前应用状态。 */
App_State App_GetState(void);

/* 调试/查询接口：当前蠕动泵速度百分比。 */
uint8_t App_GetPumpSpeedPercent(void);

/* 调试/查询接口：当前 PG 有效掩码。 */
uint16_t App_GetPGMask(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H__ */
