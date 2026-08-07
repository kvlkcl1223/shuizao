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

/* Z 轴 PG3 暂定为原点/定点，PG14 暂定为最底部。 */
const PG_ID APP_Z_HOME_PG = PG_3;
const PG_ID APP_Z_BOTTOM_PG = PG_14;

/* Z 轴从上到下的光电顺序。若实际接线顺序不同，只调整这个表。 */
const PG_ID APP_Z_ORDER[] = {
    PG_3,
    PG_4,
    PG_5,
    PG_6,
    PG_7,
    PG_8,
    PG_9,
    PG_10,
    PG_11,
    PG_12,
    PG_13,
    PG_14,
};
const uint8_t APP_Z_ORDER_COUNT = sizeof(APP_Z_ORDER) / sizeof(APP_Z_ORDER[0]);

/*
 * 自动吸取体积档位表，必须按从高液位到低液位排列。
 * 当前默认：
 * - PG3 是原点/最高限位，不参与体积吸取。
 * - 100ml 默认使用 PG12，50ml 默认使用 PG13，二者不共用传感器。
 * - first_spray_pg 用于第一次“吸取位置下一档”喷淋，允许和吸取 PG 独立配置。
 */
const App_VolumePosition APP_VOLUME_POSITIONS[] = {
    {800U, PG_4,  PG_5,  0U},
    {700U, PG_5,  PG_6,  0U},
    {600U, PG_6,  PG_7,  0U},
    {500U, PG_7,  PG_8,  0U},
    {400U, PG_8,  PG_9,  0U},
    {300U, PG_9,  PG_10, 0U},
    {200U, PG_10, PG_11, 1U},
    {150U, PG_11, PG_12, 1U},
    {100U, PG_12, PG_13, 1U},
    {50U,  PG_13, PG_14, 1U},
};
const uint8_t APP_VOLUME_POSITION_COUNT =
    sizeof(APP_VOLUME_POSITIONS) / sizeof(APP_VOLUME_POSITIONS[0]);

/* 第二次固定 300ml 位置喷淋，第三次固定 800ml 位置喷淋。 */
const uint16_t APP_SPRAY_FIXED_VOLUME_STAGE2_ML = 300U;
const uint16_t APP_SPRAY_FIXED_VOLUME_STAGE3_ML = 800U;

/* 三段喷淋共用同一套泵补偿时间。下标 0~5 对应泵 1~6，实测后只改这个表。 */
const uint32_t APP_SPRAY_PUMP_MS[APP_PUMP_COUNT] = {
    APP_SPRAY_PUMP1_MS,
    APP_SPRAY_PUMP2_MS,
    APP_SPRAY_PUMP3_MS,
    APP_SPRAY_PUMP4_MS,
    APP_SPRAY_PUMP5_MS,
    APP_SPRAY_PUMP6_MS,
};
