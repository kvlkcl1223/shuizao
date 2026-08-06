#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/*
 * app_config.h
 * 应用层硬件映射和默认参数声明。
 * 所有后续需要频繁调整的 PG 顺序、默认时间、速度和屏幕控件名都集中在这里。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "motor.h"
#include "pg.h"
#include <stdint.h>

#define APP_PUMP_COUNT 6U

/* 蠕动泵速度使用屏幕显示百分比，实际转换到 Motor_Run 的 0~1000 速度值。 */
#define APP_MIN_PUMP_SPEED_PERCENT      10U
#define APP_MAX_PUMP_SPEED_PERCENT      100U
#define APP_DEFAULT_PUMP_SPEED_PERCENT  60U

/* Z 轴默认运动速度百分比。若上机发现过快或过慢，优先改这里。 */
#define APP_Z_SPEED_PERCENT             70U

/* 自动流程默认时间。当前未确认真实工艺时间，先用 5 秒占位。 */
#define APP_ASPIRATE_PHASE_MS           5000U
#define APP_SPRAY_PHASE_MS              5000U

/* Z 轴运动超时保护，避免传感器异常时电机持续运行。 */
#define APP_Z_MOVE_TIMEOUT_MS           30000U
#define APP_HOME_TIMEOUT_MS             45000U

/* MCU 定期向串口屏刷新状态的周期。 */
#define APP_SCREEN_UPDATE_MS            500U

/* 陶晶驰屏幕控件名称。若 HMI 控件名不同，只需改这些宏。 */
#define APP_SCREEN_MESSAGE_OBJ          "t6"
#define APP_SCREEN_STATE_OBJ            "n_state"
#define APP_SCREEN_PHASE_OBJ            "n_phase"
#define APP_SCREEN_SPEED_OBJ            "n_speed"
#define APP_SCREEN_PGMASK_OBJ           "n_pgmask"
#define APP_SCREEN_KEEP10_OBJ           "n_keep10"
#define APP_SCREEN_ALARM_OBJ            "n_alarm"
#define APP_SCREEN_PROGRESS_OBJ         "j_progress"

/* 设备映射：Z 轴、电机方向、蠕动泵编号都集中由 app_config.c 定义。 */
extern const Motor_ID APP_Z_MOTOR_ID;
extern const Motor_ID APP_PUMP_MOTOR_IDS[APP_PUMP_COUNT];

extern const uint8_t APP_Z_UP_DIRECTION;
extern const uint8_t APP_Z_DOWN_DIRECTION;
extern const uint8_t APP_PUMP_IN_DIRECTION;
extern const uint8_t APP_PUMP_OUT_DIRECTION;

/* 关键 PG 位置。PG 为低电平有效，实际顺序不确定时只改 app_config.c。 */
extern const PG_ID APP_Y_READY_PG;
extern const PG_ID APP_Z_HOME_PG;
extern const PG_ID APP_Z_BOTTOM_PG;
extern const PG_ID APP_Z_KEEP10_PG;

/* Z 轴从原点到最底部的物理顺序表，用于自动判断上升/下降方向。 */
extern const PG_ID APP_Z_ORDER[];
extern const uint8_t APP_Z_ORDER_COUNT;

/* 自动吸取和喷淋阶段序列。当前按用户要求为 PG3->PG4、PG4->PG3。 */
extern const PG_ID APP_ASPIRATE_PG_SEQUENCE[];
extern const uint8_t APP_ASPIRATE_PHASE_COUNT;
extern const PG_ID APP_SPRAY_PG_SEQUENCE[];
extern const uint8_t APP_SPRAY_PHASE_COUNT;

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
