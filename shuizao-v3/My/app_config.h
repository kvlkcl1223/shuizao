#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/*
 * app_config.h
 * 应用层硬件映射和默认工艺参数声明。
 * 光电顺序、体积档位、默认时间、速度和屏幕控件名都集中在这里，后续现场调试优先改本模块。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "motor.h"
#include "pg.h"
#include <stdint.h>

#define APP_PUMP_COUNT 6U

/* 自动流程最多经过的吸取阶段数量。当前体积表最多 10 个阶段，预留余量方便后续增档。 */
#define APP_MAX_AUTO_PHASES             12U
#define APP_SPRAY_STAGE_COUNT           3U

/* Z 轴逻辑位置：只有 HOME/100ml/50ml/BOTTOM 有真实传感器，其余位置靠相邻步进定时到达。 */
typedef enum {
    APP_Z_POS_HOME = 0,
    APP_Z_POS_800ML,
    APP_Z_POS_700ML,
    APP_Z_POS_600ML,
    APP_Z_POS_500ML,
    APP_Z_POS_400ML,
    APP_Z_POS_300ML,
    APP_Z_POS_200ML,
    APP_Z_POS_150ML,
    APP_Z_POS_100ML,
    APP_Z_POS_50ML,
    APP_Z_POS_BOTTOM,
    APP_Z_POS_INVALID = 0xFF
} App_ZPosition;

#define APP_Z_POSITION_COUNT            12U
#define APP_Z_STEP_COUNT                (APP_Z_POSITION_COUNT - 1U)

/* 预留给科研人员人工清洗接液烧杯的体积。固件不保存该选项，每次由屏幕 START 命令携带。 */
#define APP_MANUAL_RESERVED_VOLUME_ML   10U

/* 蠕动泵速度使用屏幕显示百分比，实际转换到 Motor_Run 的 0~1000 速度值。 */
#define APP_MIN_PUMP_SPEED_PERCENT      10U
#define APP_MAX_PUMP_SPEED_PERCENT      100U
#define APP_DEFAULT_PUMP_SPEED_PERCENT  60U

/* Z 轴默认运动速度百分比。若上机后发现过快或过慢，优先改这里。 */
#define APP_Z_SPEED_PERCENT             70U

/* 自动流程默认时间。真实工艺时间未确认前先用占位值，HMI 也可通过 #SET 临时修改。 */
#define APP_ASPIRATE_PHASE_MS           5000U
#define APP_TRIM_10ML_MS                1000U
#define APP_SPRAY_PUMP1_MS              5000U
#define APP_SPRAY_PUMP2_MS              5000U
#define APP_SPRAY_PUMP3_MS              5000U
#define APP_SPRAY_PUMP4_MS              5000U
#define APP_SPRAY_PUMP5_MS              5000U
#define APP_SPRAY_PUMP6_MS              5000U

/* Z 轴运动超时保护，避免传感器异常时电机持续运行。 */
#define APP_Z_MOVE_TIMEOUT_MS           30000U
#define APP_HOME_TIMEOUT_MS             45000U

/*
 * Z 轴反向保护时间。
 * 当 Z 轴刚执行过上行又要下行，或刚执行过下行又要上行时，先让驱动空档停顿一段时间再启动反方向。
 */
#define APP_Z_REVERSE_DEADTIME_MS       300U

/* 其余虚拟体积位置的相邻步进默认时间。真实调试时优先修改 app_config.c 的两个时间表。 */
#define APP_Z_STEP_DEFAULT_MS           1000U

/* 上电后是否自动复位到最高点。最高点由 APP_Z_HOME_PG 决定，当前默认 PG3。 */
#define APP_POWER_ON_RESET_ENABLE       1U

/* 上电复位第一步先向下运行的时间；如果提前触发 APP_Z_BOTTOM_PG，会立即停止并改为向上寻找 PG3。 */
#define APP_POWER_ON_RESET_DOWN_MS      1000U

/* MCU 定期向串口屏刷新状态的周期。 */
#define APP_SCREEN_UPDATE_MS            500U

/* USART2 调试日志配置。USART3 仍专用于陶晶驰串口屏。 */
#define APP_LOG_ENABLE                  1U
#define APP_LOG_UART_TIMEOUT_MS         50U

/*
 * LED1 生命灯使用 TIM5 周期中断分频翻转。
 * 当前 CubeMX 参数看起来约为 1ms 中断，默认 1000 次翻转一次；若 TIM5 改成 1s 中断，改为 1U。
 */
#define APP_LED1_HEARTBEAT_TIM5_TICKS   1000U

/* 陶晶驰屏幕控件名称。若 HMI 控件名不同，只需修改这些宏。 */
#define APP_SCREEN_MESSAGE_OBJ          "t6"
#define APP_SCREEN_STATE_OBJ            "n_state"
#define APP_SCREEN_PHASE_OBJ            "n_phase"
#define APP_SCREEN_SPEED_OBJ            "n_speed"
#define APP_SCREEN_PGMASK_OBJ           "n_pgmask"
#define APP_SCREEN_KEEP10_OBJ           "n_keep10"
#define APP_SCREEN_ALARM_OBJ            "n_alarm"
#define APP_SCREEN_PROGRESS_OBJ         "j_progress"

/* 体积档位表。first_spray_pos 是“吸取位置下一档喷淋”的实际目标。 */
typedef struct {
    uint16_t volume_ml;
    App_ZPosition aspirate_pos;
    App_ZPosition first_spray_pos;
    uint8_t precise_aspirate;
} App_VolumePosition;

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
extern const PG_ID APP_Z_100ML_PG;
extern const PG_ID APP_Z_50ML_PG;
extern const PG_ID APP_Z_BOTTOM_PG;

/* Z 轴真实传感器从上到下的物理顺序表，用于传感器调试。 */
extern const PG_ID APP_Z_ORDER[];
extern const uint8_t APP_Z_ORDER_COUNT;

/* Z 轴逻辑位置相邻步进时间表。down[i] 表示 i -> i+1，up[i] 表示 i+1 -> i。 */
extern const uint32_t APP_Z_STEP_DOWN_MS[APP_Z_STEP_COUNT];
extern const uint32_t APP_Z_STEP_UP_MS[APP_Z_STEP_COUNT];

/* 自动吸取体积档位表，按从高液位到低液位的顺序排列。 */
extern const App_VolumePosition APP_VOLUME_POSITIONS[];
extern const uint8_t APP_VOLUME_POSITION_COUNT;

/* 第二、第三次喷淋使用固定体积档位。 */
extern const uint16_t APP_SPRAY_FIXED_VOLUME_STAGE2_ML;
extern const uint16_t APP_SPRAY_FIXED_VOLUME_STAGE3_ML;
extern const uint32_t APP_SPRAY_PUMP_MS[APP_PUMP_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
