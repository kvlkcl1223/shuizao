#include "app_config.h"

/*
 * app_config.c
 * 当前工程的实际硬件配置。
 * 光电顺序、体积档位、电机方向或泵方向确认后，只需要改本文件中的表和常量。
 */

/* 电机硬件分配：第一路 DRV8870 控制 Z 轴。 */
const Motor_ID APP_Z_MOTOR_ID = MOTOR_1;

/* 第二到第七路 DRV8870 控制 6 个蠕动泵，第八路保留不用。 */
const Motor_ID APP_PUMP_MOTOR_IDS[APP_PUMP_COUNT] = {
    MOTOR_2,
    MOTOR_3,
    MOTOR_4,
    MOTOR_5,
    MOTOR_6,
    MOTOR_7,
};

/* 方向映射集中在这里。上机后若发现方向反了，只交换对应 FORWARD/REVERSE。 */
const uint8_t APP_Z_UP_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_Z_DOWN_DIRECTION = MOTOR_REVERSE;
const uint8_t APP_PUMP_IN_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_PUMP_OUT_DIRECTION = MOTOR_REVERSE;

/* Y 轴默认使用 PG1 作为允许工作位置。 */
const PG_ID APP_Y_READY_PG = PG_1;

/* v3 当前 Z 轴真实传感器：PG3 上限，PG4 200ml，PG5 150ml，PG6 100ml，PG7 50ml/下限。 */
const PG_ID APP_Z_HOME_PG = PG_3;
const PG_ID APP_Z_200ML_PG = PG_4;
const PG_ID APP_Z_150ML_PG = PG_5;
const PG_ID APP_Z_100ML_PG = PG_6;
const PG_ID APP_Z_50ML_PG = PG_7;
const PG_ID APP_Z_BOTTOM_PG = PG_7;

/* Z 轴真实传感器从上到下的光电顺序。800ml 和 300ml 是定时虚拟位置，不在这里出现。 */
const PG_ID APP_Z_ORDER[] = {
    PG_3,
    PG_4,
    PG_5,
    PG_6,
    PG_7,
};
const uint8_t APP_Z_ORDER_COUNT = sizeof(APP_Z_ORDER) / sizeof(APP_Z_ORDER[0]);

/*
 * Z 轴相邻逻辑位置步进时间。
 * down[i] 表示从位置 i 到 i+1；up[i] 表示从位置 i+1 回到 i。
 * 逻辑顺序：
 * HOME, 800, 700, 600, 500, 400, 300, 200, 150, 100, 50
 * 其中 800ml 到 300ml 没有独立传感器，靠相邻步进时间到达。
 * 真实调试时先用手动低速确认方向，再逐段实测并修改这两个表。
 */
const uint32_t APP_Z_STEP_DOWN_MS[APP_Z_STEP_COUNT] = {
    APP_Z_STEP_DEFAULT_MS, /* HOME -> 800 */
    APP_Z_STEP_DEFAULT_MS, /* 800  -> 700 */
    APP_Z_STEP_DEFAULT_MS, /* 700  -> 600 */
    APP_Z_STEP_DEFAULT_MS, /* 600  -> 500 */
    APP_Z_STEP_DEFAULT_MS, /* 500  -> 400 */
    APP_Z_STEP_DEFAULT_MS, /* 400  -> 300 */
    APP_Z_STEP_DEFAULT_MS, /* 300  -> 200，200ml 用 PG4 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 200  -> 150，150ml 用 PG5 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 150  -> 100，100ml 用 PG6 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 100  -> 50，50ml/下限用 PG7 最终确认 */
};

const uint32_t APP_Z_STEP_UP_MS[APP_Z_STEP_COUNT] = {
    APP_Z_STEP_DEFAULT_MS, /* 800  -> HOME，HOME 用 PG3 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 700  -> 800 */
    APP_Z_STEP_DEFAULT_MS, /* 600  -> 700 */
    APP_Z_STEP_DEFAULT_MS, /* 500  -> 600 */
    APP_Z_STEP_DEFAULT_MS, /* 400  -> 500 */
    APP_Z_STEP_DEFAULT_MS, /* 300  -> 400 */
    APP_Z_STEP_DEFAULT_MS, /* 200  -> 300 */
    APP_Z_STEP_DEFAULT_MS, /* 150  -> 200，200ml 用 PG4 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 100  -> 150，150ml 用 PG5 最终确认 */
    APP_Z_STEP_DEFAULT_MS, /* 50   -> 100，100ml 用 PG6 最终确认 */
};

/*
 * 自动吸取体积档位表，必须按从高液位到低液位排列。
 * 当前默认：
 * - HOME 是原点/最高限位，不参与体积吸取。
 * - 自动模式只支持 200/150/100/50ml。
 * - 200/150/100/50ml 均使用真实传感器定位，50ml 同时是下限位。
 * - first_spray_pos 用于第一次“吸取位置下一档”喷淋，允许和吸取位置独立配置。
 */
const App_VolumePosition APP_VOLUME_POSITIONS[] = {
    {200U, APP_Z_POS_200ML, APP_Z_POS_150ML, 1U},
    {150U, APP_Z_POS_150ML, APP_Z_POS_100ML, 1U},
    {100U, APP_Z_POS_100ML, APP_Z_POS_50ML,  1U},
    {50U,  APP_Z_POS_50ML,  APP_Z_POS_50ML,  1U},
};
const uint8_t APP_VOLUME_POSITION_COUNT =
    sizeof(APP_VOLUME_POSITIONS) / sizeof(APP_VOLUME_POSITIONS[0]);

/*
 * 分阶段吸取表：
 * - HOME 到 800ml 的移动不开泵；到达 800ml 后开泵，并在后续下行吸取过程中保持运行。
 * - 200ml 到 50ml 是真实 PG 定位段，下降找 PG 时开泵，到位后继续按本段 dwell_ms 吸取。
 */
const App_AspirateStage APP_ASPIRATE_STAGES[] = {
    {800U, APP_Z_POS_800ML, APP_ASP_DWELL_800_MS, 0U, 1U},
    {700U, APP_Z_POS_700ML, APP_ASP_DWELL_700_MS, 1U, 1U},
    {600U, APP_Z_POS_600ML, APP_ASP_DWELL_600_MS, 1U, 1U},
    {500U, APP_Z_POS_500ML, APP_ASP_DWELL_500_MS, 1U, 1U},
    {400U, APP_Z_POS_400ML, APP_ASP_DWELL_400_MS, 1U, 1U},
    {300U, APP_Z_POS_300ML, APP_ASP_DWELL_300_MS, 1U, 1U},
    {200U, APP_Z_POS_200ML, APP_ASP_DWELL_200_MS, 1U, 1U},
    {150U, APP_Z_POS_150ML, APP_ASP_DWELL_150_MS, 1U, 1U},
    {100U, APP_Z_POS_100ML, APP_ASP_DWELL_100_MS, 1U, 1U},
    {50U,  APP_Z_POS_50ML,  APP_ASP_DWELL_50_MS,  1U, 1U},
};
const uint8_t APP_ASPIRATE_STAGE_COUNT =
    sizeof(APP_ASPIRATE_STAGES) / sizeof(APP_ASPIRATE_STAGES[0]);

/* 第二次固定 300ml 虚拟位置喷淋，第三次固定 800ml 虚拟位置喷淋。 */
const App_ZPosition APP_SPRAY_FIXED_STAGE2_POS = APP_Z_POS_300ML;
const App_ZPosition APP_SPRAY_FIXED_STAGE3_POS = APP_Z_POS_800ML;

/* 喷淋时间配置档位，顺序必须与 APP_VOLUME_POSITIONS 保持一致。 */
const uint16_t APP_SPRAY_PROFILE_VOLUMES[APP_SPRAY_PROFILE_COUNT] = {
    200U,
    150U,
    100U,
    50U,
};

/* 第一段喷淋默认泵补偿时间。屏幕调节和 Flash 保存只影响第一段。下标 0~5 对应泵 1~6。 */
const uint32_t APP_SPRAY_PUMP_MS[APP_PUMP_COUNT] = {
    APP_SPRAY_PUMP1_MS,
    APP_SPRAY_PUMP2_MS,
    APP_SPRAY_PUMP3_MS,
    APP_SPRAY_PUMP4_MS,
    APP_SPRAY_PUMP5_MS,
    APP_SPRAY_PUMP6_MS,
};

/*
 * 第二段喷淋固定时间表，喷淋位置为 300ml 虚拟位置。
 * 行顺序必须与 APP_SPRAY_PROFILE_VOLUMES 一致：200/150/100/50ml。
 * 列下标 0~5 对应泵 1~6。现场调试第二段喷淋量时只改这里。
 */
const uint32_t APP_SPRAY_STAGE2_PUMP_MS[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT] = {
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 200ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 150ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 100ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 50ml */
};

/*
 * 第三段喷淋固定时间表，喷淋位置为 800ml 虚拟位置。
 * 行顺序必须与 APP_SPRAY_PROFILE_VOLUMES 一致：200/150/100/50ml。
 * 列下标 0~5 对应泵 1~6。现场调试第三段喷淋量时只改这里。
 */
const uint32_t APP_SPRAY_STAGE3_PUMP_MS[APP_SPRAY_PROFILE_COUNT][APP_PUMP_COUNT] = {
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 200ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 150ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 100ml */
    {5000U, 5000U, 5000U, 5000U, 5000U, 5000U}, /* 50ml */
};
