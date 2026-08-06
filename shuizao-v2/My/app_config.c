#include "app_config.h"

/*
 * app_config.c
 * 当前工程的实际硬件配置。
 * 光电顺序、10 ml 位置、Z 轴方向或泵方向确认后，只需要改本文件中的表和常量。
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

/* 方向映射集中在这里，若上机后发现方向反了，只交换对应 FORWARD/REVERSE。 */
const uint8_t APP_Z_UP_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_Z_DOWN_DIRECTION = MOTOR_REVERSE;
const uint8_t APP_PUMP_IN_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_PUMP_OUT_DIRECTION = MOTOR_REVERSE;

/* Y 轴默认使用 PG1 作为允许工作位置。 */
const PG_ID APP_Y_READY_PG = PG_1;

/* Z 轴 PG3 暂定为原点/定点，PG14 暂定为最底部。 */
const PG_ID APP_Z_HOME_PG = PG_3;
const PG_ID APP_Z_BOTTOM_PG = PG_14;

/* 预留 10 ml 的位置尚未确认，当前先用 PG5 占位，后续只改这一行。 */
const PG_ID APP_Z_KEEP10_PG = PG_5;

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

/* 分阶段吸取：先到 PG3 吸取固定时间，再到 PG4 吸取固定时间。 */
const PG_ID APP_ASPIRATE_PG_SEQUENCE[] = {
    PG_3,
    PG_4,
};
const uint8_t APP_ASPIRATE_PHASE_COUNT =
    sizeof(APP_ASPIRATE_PG_SEQUENCE) / sizeof(APP_ASPIRATE_PG_SEQUENCE[0]);

/* 分阶段喷淋：先在 PG4 喷淋固定时间，再到 PG3 喷淋固定时间。 */
const PG_ID APP_SPRAY_PG_SEQUENCE[] = {
    PG_4,
    PG_3,
};
const uint8_t APP_SPRAY_PHASE_COUNT =
    sizeof(APP_SPRAY_PG_SEQUENCE) / sizeof(APP_SPRAY_PG_SEQUENCE[0]);
