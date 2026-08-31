#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

/*
 * app_config.h
 * 应用层硬件映射和默认工艺参数声明。
 * 光电顺序、体积档位、默认时间、速度和屏幕控件名都集中在这里，后续现场调试优先改本模块。
 */

#ifdef __cplusplus
extern "C"
{
#endif

#include "motor.h"
#include "pg.h"
#include <stdint.h>

#define APP_PUMP_COUNT 6U
#define APP_SPRAY_PROFILE_COUNT 4U
#define APP_ZVIRT_COUNT 4U

/* 自动流程最多经过的吸取阶段数量。当前体积表最多 4 个阶段，预留余量方便后续增档。 */
#define APP_MAX_AUTO_PHASES 12U
#define APP_SPRAY_STAGE_COUNT 3U

    /* Z 轴逻辑位置：HOME/200/150/100/50 有真实传感器，其余为定时虚拟位置。 */
    typedef enum
    {
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
        APP_Z_POS_INVALID = 0xFF
    } App_ZPosition;

#define APP_Z_POSITION_COUNT 11U
#define APP_Z_STEP_COUNT (APP_Z_POSITION_COUNT - 1U)

/* 预留给科研人员人工清洗接液烧杯的体积。固件不保存该选项，每次由屏幕 START 命令携带。 */
#define APP_MANUAL_RESERVED_VOLUME_ML 10U

/* 蠕动泵速度使用屏幕显示百分比，实际转换到 Motor_Run 的 0~1000 速度值。 */
#define APP_MIN_PUMP_SPEED_PERCENT 10U
#define APP_MAX_PUMP_SPEED_PERCENT 100U
#define APP_DEFAULT_PUMP_SPEED_PERCENT 60U
#define APP_SPRAY_SPEED_PERCENT 100U

/* Z 轴默认运动速度百分比。若上机后发现过快或过慢，优先改这里。 */
#define APP_Z_SPEED_PERCENT 70U

/* 自动流程默认时间。真实工艺时间未确认前先用占位值，HMI 也可通过 #SET 临时修改。 */
#define APP_TIME_MIN_MS 0U
#define APP_TIME_MAX_MS 60000U
#define APP_ASPIRATE_PHASE_MS 5000U
#define APP_ASP_DWELL_800_MS 3000U
#define APP_ASP_DWELL_700_MS 3000U
#define APP_ASP_DWELL_600_MS 3000U
#define APP_ASP_DWELL_500_MS 3000U
#define APP_ASP_DWELL_400_MS 3000U
#define APP_ASP_DWELL_300_MS 3000U
#define APP_ASP_DWELL_200_MS 3000U
#define APP_ASP_DWELL_150_MS 3000U
#define APP_ASP_DWELL_100_MS 3000U
#define APP_ASP_DWELL_50_MS 3000U
#define APP_TRIM_10ML_MS 1000U
#define APP_SPRAY_PUMP1_MS 5000U
#define APP_SPRAY_PUMP2_MS 5000U
#define APP_SPRAY_PUMP3_MS 5000U
#define APP_SPRAY_PUMP4_MS 5000U
#define APP_SPRAY_PUMP5_MS 5000U
#define APP_SPRAY_PUMP6_MS 5000U

/* Z 轴运动超时保护，避免传感器异常时电机持续运行。 */
#define APP_Z_MOVE_TIMEOUT_MS 30000U
#define APP_HOME_TIMEOUT_MS 45000U

/*
 * Z 轴反向保护时间。
 * 当 Z 轴刚执行过上行又要下行，或刚执行过下行又要上行时，先让驱动空档停顿一段时间再启动反方向。
 */
#define APP_Z_REVERSE_DEADTIME_MS 300U

/* Z 轴虚拟位置步进时间由 HMI 调试，范围独立于喷淋泵时间。 */
#define APP_ZVIRT_TIME_MIN_MS 1000U
#define APP_ZVIRT_TIME_MAX_MS 20000U
#define APP_ZVIRT_TIME_DEFAULT_MS 3000U

/* 其余相邻步进默认时间。真实调试时优先修改 app_config.c 的两个时间表。 */
#define APP_Z_STEP_DEFAULT_MS APP_ZVIRT_TIME_DEFAULT_MS

/* 上电后是否自动复位到最高点。最高点由 APP_Z_HOME_PG 决定，当前默认 PG3。 */
#define APP_POWER_ON_RESET_ENABLE 1U

/* 上电复位第一步先向下运行的时间；如果提前触发 APP_Z_BOTTOM_PG，会立即停止并改为向上寻找 PG3。 */
#define APP_POWER_ON_RESET_DOWN_MS 1000U

/* MCU 定期向串口屏刷新状态的周期。 */
#define APP_SCREEN_UPDATE_MS 500U

/* 漏水检测配置。PG8 默认低电平为正常，高电平为异常。
 * APP_LEAK_DETECT_TEST_ONLY 为 1 时只输出 USART2 日志，不停机、不跳报警页；
 * 真机正式运行前改为 0，异常会关闭全部动作并跳转漏水报警页。
 */
#define APP_LEAK_DETECT_ENABLE 1U
#define APP_LEAK_DETECT_TEST_ONLY 1U
#define APP_LEAK_CHECK_INTERVAL_MS 200U
#define APP_LEAK_TEST_LOG_INTERVAL_MS 1000U
#define APP_LEAK_DEBOUNCE_COUNT 3U

/* USART2 调试日志配置。USART3 仍专用于陶晶驰串口屏。 */
#define APP_LOG_ENABLE 1U
#define APP_LOG_UART_TIMEOUT_MS 50U

/*
 * LED1 生命灯使用 TIM5 周期中断分频翻转。
 * 当前 CubeMX 参数看起来约为 1ms 中断，默认 1000 次翻转一次；若 TIM5 改成 1s 中断，改为 1U。
 */
#define APP_LED1_HEARTBEAT_TIM5_TICKS 1000U

/* 陶晶驰屏幕控件名称。若 HMI 控件名不同，只需修改这些宏。 */
#define APP_SCREEN_MESSAGE_OBJ "t6"
#define APP_SCREEN_STATE_OBJ "n_state"
#define APP_SCREEN_PHASE_OBJ "n_phase"
#define APP_SCREEN_SPEED_OBJ "n_speed"
#define APP_SCREEN_SPEED_SLIDER_OBJ "h_speed"
#define APP_SCREEN_MANUAL_SPEED_OBJ "n_mspd"
#define APP_SCREEN_MANUAL_SPEED_SLIDER_OBJ "h_mspd"
#define APP_SCREEN_PGMASK_OBJ "n_pgmask"
#define APP_SCREEN_KEEP10_OBJ "n_keep10"
#define APP_SCREEN_ALARM_OBJ "n_alarm"
#define APP_SCREEN_PROGRESS_OBJ "j_progress"

/* Y 轴位置异常时跳转的警告页面名称。HMI 页面名变更时只改这里。 */
#define APP_SCREEN_WARNING_PAGE "warn"

/* 漏水异常时跳转的报警页面名。HMI 新增页面建议直接命名为 leak_warn。 */
#define APP_SCREEN_LEAK_WARNING_PAGE "leak_warn"

/* 喷淋补偿时间调试控件。滑轴和数值控件都由 MCU 回填，便于页面打开时同步当前值。 */
#define APP_SCREEN_SPRAY1_SLIDER_OBJ "h_spray1_ms"
#define APP_SCREEN_SPRAY2_SLIDER_OBJ "h_spray2_ms"
#define APP_SCREEN_SPRAY3_SLIDER_OBJ "h_spray3_ms"
#define APP_SCREEN_SPRAY4_SLIDER_OBJ "h_spray4_ms"
#define APP_SCREEN_SPRAY5_SLIDER_OBJ "h_spray5_ms"
#define APP_SCREEN_SPRAY6_SLIDER_OBJ "h_spray6_ms"
#define APP_SCREEN_SPRAY1_VALUE_OBJ "n_spray1_ms"
#define APP_SCREEN_SPRAY2_VALUE_OBJ "n_spray2_ms"
#define APP_SCREEN_SPRAY3_VALUE_OBJ "n_spray3_ms"
#define APP_SCREEN_SPRAY4_VALUE_OBJ "n_spray4_ms"
#define APP_SCREEN_SPRAY5_VALUE_OBJ "n_spray5_ms"
#define APP_SCREEN_SPRAY6_VALUE_OBJ "n_spray6_ms"
#define APP_SCREEN_SPRAY_VOLUME_VALUE_OBJ "n_spray_vol"
#define APP_SCREEN_SPRAY_VOLUME_TEXT_OBJ "t_spray_vol"

/* Z 轴虚拟位置调试控件。 */
#define APP_SCREEN_Z_DN_HOME_800_SLIDER_OBJ "h_zd_h8"
#define APP_SCREEN_Z_DN_800_300_SLIDER_OBJ "h_zd_83"
#define APP_SCREEN_Z_UP_300_800_SLIDER_OBJ "h_zu_38"
#define APP_SCREEN_Z_UP_200_300_SLIDER_OBJ "h_zu_23"
#define APP_SCREEN_Z_DN_HOME_800_VALUE_OBJ "n_zd_h8"
#define APP_SCREEN_Z_DN_800_300_VALUE_OBJ "n_zd_83"
#define APP_SCREEN_Z_UP_300_800_VALUE_OBJ "n_zu_38"
#define APP_SCREEN_Z_UP_200_300_VALUE_OBJ "n_zu_23"

/*
 * 参数保存 Flash 页。
 * STM32F103RC 当前 Keil IROM 已预留最后 2KB，应用程序不要链接到 0x0803F800 之后。
 */
#define APP_SETTINGS_FLASH_ADDR 0x0803F800U
#define APP_SETTINGS_FLASH_PAGE_SIZE 2048U

    /* 体积档位表。first_spray_pos 是“吸取位置下一档喷淋”的实际目标。 */
    typedef struct
    {
        uint16_t volume_ml;
        App_ZPosition aspirate_pos;
        App_ZPosition first_spray_pos;
        uint8_t precise_aspirate;
    } App_VolumePosition;

    /* 自动吸取阶段。虚拟位置只停留不吸取；真实 PG 位置移动定位时开始吸取，到位后继续停留吸取。 */
    typedef struct
    {
        uint16_t volume_ml;
        App_ZPosition pos;
        uint32_t dwell_ms;
        uint8_t pump_during_move;
        uint8_t pump_during_dwell;
    } App_AspirateStage;

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
    extern const PG_ID APP_Z_200ML_PG;
    extern const PG_ID APP_Z_150ML_PG;
    extern const PG_ID APP_Z_100ML_PG;
    extern const PG_ID APP_Z_50ML_PG;
    extern const PG_ID APP_Z_BOTTOM_PG;
    extern const PG_ID APP_LEAK_PG;

    /* Z 轴真实传感器从上到下的物理顺序表，用于传感器调试。 */
    extern const PG_ID APP_Z_ORDER[];
    extern const uint8_t APP_Z_ORDER_COUNT;

    /* Z 轴逻辑位置相邻步进时间表。down[i] 表示 i -> i+1，up[i] 表示 i+1 -> i。 */
    extern const uint32_t APP_Z_STEP_DOWN_MS[APP_Z_STEP_COUNT];
    extern const uint32_t APP_Z_STEP_UP_MS[APP_Z_STEP_COUNT];

    /* 自动吸取体积档位表，按从高液位到低液位的顺序排列。 */
    extern const App_VolumePosition APP_VOLUME_POSITIONS[];
    extern const uint8_t APP_VOLUME_POSITION_COUNT;

    /* 自动吸取阶段表，按从高液位到低液位排列。 */
    extern const App_AspirateStage APP_ASPIRATE_STAGES[];
    extern const uint8_t APP_ASPIRATE_STAGE_COUNT;

    /* 第二、第三次喷淋使用固定虚拟位置。 */
    extern const App_ZPosition APP_SPRAY_FIXED_STAGE2_POS;
    extern const App_ZPosition APP_SPRAY_FIXED_STAGE3_POS;
    extern const uint16_t APP_SPRAY_PROFILE_VOLUMES[APP_SPRAY_PROFILE_COUNT];
    extern const uint32_t APP_SPRAY_PUMP_MS[APP_PUMP_COUNT];
    extern const uint32_t APP_SPRAY_STAGE2_PUMP_MS[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT];
    extern const uint32_t APP_SPRAY_STAGE3_PUMP_MS[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
