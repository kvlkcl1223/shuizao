# 陶晶驰串口屏与 STM32 通信协议

本文档对应 `shuizao-v2` 当前固件协议。串口屏连接 USART3，默认参数为：

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无
- MCU 接收方式：USART3 DMA + IDLE 中断

## 1. 基本规则

屏幕发送给 MCU 的控制命令使用 ASCII 文本帧：

```text
#命令,参数1,参数2;
```

规则：

- `#` 是帧头。
- `;` 是帧尾。
- 参数用英文逗号 `,` 分隔。
- 命令区分大小写，建议全部大写。
- 当前版本不使用校验和，便于陶晶驰按钮事件直接发送。
- MCU 支持一次串口接收中包含多条命令，例如 `#SPD,60;#GET,STATE;`。

陶晶驰按钮事件示例：

```text
prints "#START,100,1;",0
```

## 2. 屏幕到 MCU 命令

### 启动自动流程

```text
#START,<volume>,<keep10>;
```

参数：

- `volume`：目标体积，当前支持 `50` 或 `100`。
- `keep10`：是否预留 10ml 人工清洗烧杯，`0` 不预留，`1` 预留。

示例：

```text
#START,50,0;
#START,100,1;
```

当前自动流程：

1. 回原点 PG3。
2. 检查 Y 轴允许位置，默认 PG1 有效。
3. 移动到 PG3，吸取固定时间。
4. 移动到 PG4，吸取固定时间。
5. 如果启用预留 10ml，移动到预留 10ml PG，停止并等待人工清洗确认。
6. 移动到 PG4，喷淋固定时间。
7. 移动到 PG3，喷淋固定时间。
8. 回原点 PG3。
9. 完成。

### 人工清洗确认

```text
#OK;
```

只在 MCU 状态为 `WAIT_MANUAL_CLEAN` 时有效。屏幕应在人工清洗提示页放置确认按钮，按钮事件发送：

```text
prints "#OK;",0
```

### 设置蠕动泵速度

```text
#SPD,<percent>;
```

参数：

- `percent`：泵速度百分比，合法范围 `10~100`。
- 小于 10 会按 10 处理，大于 100 会按 100 处理。

示例：

```text
#SPD,60;
#SPD,100;
```

### 停止与急停

```text
#STOP;
#ESTOP;
```

区别：

- `STOP`：停止当前动作；如果正在自动流程中，停止泵和 Z 轴后回原点。
- `ESTOP`：立即停止全部泵和 Z 轴，进入急停状态，不自动回原点。

急停后建议先发送：

```text
#HOME;
```

确认机构回原点后再允许重新启动。

### 回原点

```text
#HOME;
```

Z 轴向原点运动，默认原点为 PG3。

### MCU 软件复位

```text
#RESET;
```

触发 `HAL_NVIC_SystemReset()`。

### 手动 Z 轴

```text
#MAN,Z,UP,<percent>;
#MAN,Z,DOWN,<percent>;
#MAN,Z,STOP;
```

示例：

```text
#MAN,Z,UP,50;
#MAN,Z,DOWN,50;
#MAN,Z,STOP;
```

保护：

- 上升到原点 PG3 后自动停止。
- 下降到底部 PG14 后自动停止。
- 自动流程运行中拒绝手动 Z 轴命令。

### 手动蠕动泵

```text
#MAN,PUMP,IN,<percent>;
#MAN,PUMP,OUT,<percent>;
#MAN,PUMP,STOP;
```

示例：

```text
#MAN,PUMP,IN,60;
#MAN,PUMP,OUT,60;
#MAN,PUMP,STOP;
```

说明：

- 当前版本 6 路蠕动泵同时动作。
- 自动流程运行中拒绝手动泵命令。

### 查询状态

```text
#GET,PG;
#GET,STATE;
```

当前实现中，查询命令会触发 MCU 立即刷新屏幕状态控件。

## 3. MCU 到屏幕

MCU 向陶晶驰屏幕发送原生命令，结尾固定追加：

```text
FF FF FF
```

当前默认控件名在 `My/app_config.h` 中定义：

| 控件宏 | 默认控件 | 含义 |
|---|---|---|
| `APP_SCREEN_MESSAGE_OBJ` | `t6` | 状态文本 |
| `APP_SCREEN_STATE_OBJ` | `n_state` | 状态码 |
| `APP_SCREEN_PHASE_OBJ` | `n_phase` | 当前阶段，从 1 开始 |
| `APP_SCREEN_SPEED_OBJ` | `n_speed` | 当前泵速度百分比 |
| `APP_SCREEN_PGMASK_OBJ` | `n_pgmask` | 16 路 PG 有效位掩码 |
| `APP_SCREEN_KEEP10_OBJ` | `n_keep10` | 是否预留 10ml |
| `APP_SCREEN_ALARM_OBJ` | `n_alarm` | 报警码 |
| `APP_SCREEN_PROGRESS_OBJ` | `j_progress` | 吸取/喷淋阶段进度 |

如果 HMI 中控件名称不同，优先修改 `My/app_config.h` 中的宏。

PG 掩码规则：

- bit0 对应 PG1。
- bit1 对应 PG2。
- 以此类推。
- PG 为低电平有效，掩码中 `1` 表示该 PG 当前有效。

示例：

```text
n_speed.val=60 FF FF FF
n_pgmask.val=3 FF FF FF
t6.txt="READY" FF FF FF
```

## 4. 状态码

状态码对应 `App_State`：

| 状态码 | 名称 | 含义 |
|---:|---|---|
| 0 | `IDLE` | 空闲 |
| 1 | `HOMING` | 回原点 |
| 2 | `CHECK_Y` | 检查 Y 轴位置 |
| 3 | `MOVE_TO_ASPIRATE` | 移动到吸取位置 |
| 4 | `ASPIRATING` | 吸取中 |
| 5 | `MOVE_TO_KEEP10` | 移动到预留 10ml 位置 |
| 6 | `WAIT_MANUAL_CLEAN` | 等待人工清洗确认 |
| 7 | `MOVE_TO_SPRAY` | 移动到喷淋位置 |
| 8 | `SPRAYING` | 喷淋中 |
| 9 | `RETURN_HOME` | 回原点 |
| 10 | `DONE` | 完成 |
| 11 | `ERROR` | 故障 |
| 12 | `ESTOP` | 急停 |
| 13 | `MANUAL` | 手动控制 |

## 5. 报警码

报警码对应 `App_Alarm`：

| 报警码 | 名称 | 含义 |
|---:|---|---|
| 0 | `NONE` | 无报警 |
| 1 | `BUSY` | 当前忙，拒绝命令 |
| 2 | `BAD_VOLUME` | 体积参数错误 |
| 3 | `Y_NOT_READY` | Y 轴不在允许位置 |
| 4 | `Z_TIMEOUT` | Z 轴到位超时 |
| 5 | `BAD_COMMAND` | 命令无效 |

## 6. 当前硬件映射

硬件映射集中在 `My/app_config.c`：

| 功能 | 当前映射 |
|---|---|
| Z 轴 | DRV8870 第 1 路，`MOTOR_1` |
| 蠕动泵 1~6 | DRV8870 第 2~7 路，`MOTOR_2` 到 `MOTOR_7` |
| 备用电机 | DRV8870 第 8 路，`MOTOR_8` |
| Y 轴允许位置 | 默认 PG1 |
| Z 轴原点/定点 | PG3 |
| Z 轴底部 | PG14 |
| 预留 10ml 位置 | 当前占位 PG5，确认后修改 |

PG 低电平有效。

## 7. 后续最常改的地方

如果光电顺序或位置后续确认有变化，优先改 `My/app_config.c`：

```c
const PG_ID APP_Y_READY_PG = PG_1;
const PG_ID APP_Z_HOME_PG = PG_3;
const PG_ID APP_Z_BOTTOM_PG = PG_14;
const PG_ID APP_Z_KEEP10_PG = PG_5;

const PG_ID APP_Z_ORDER[] = {
    PG_3, PG_4, PG_5, PG_6, PG_7, PG_8,
    PG_9, PG_10, PG_11, PG_12, PG_13, PG_14,
};

const PG_ID APP_ASPIRATE_PG_SEQUENCE[] = {
    PG_3,
    PG_4,
};

const PG_ID APP_SPRAY_PG_SEQUENCE[] = {
    PG_4,
    PG_3,
};
```

如果默认时间需要调整，改 `My/app_config.h`：

```c
#define APP_ASPIRATE_PHASE_MS 5000U
#define APP_SPRAY_PHASE_MS    5000U
```

如果 Z 轴或泵方向反了，改 `My/app_config.c`：

```c
const uint8_t APP_Z_UP_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_Z_DOWN_DIRECTION = MOTOR_REVERSE;
const uint8_t APP_PUMP_IN_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_PUMP_OUT_DIRECTION = MOTOR_REVERSE;
```

## 8. 陶晶驰控件建议

建议新增或确认这些控件：

| 控件 | 类型 | 用途 |
|---|---|---|
| `t6` | 文本 | MCU 状态文本 |
| `n_state` | 数值 | MCU 状态码 |
| `n_phase` | 数值 | 当前阶段 |
| `n_speed` | 数值 | 当前速度 |
| `n_pgmask` | 数值 | PG 掩码 |
| `n_keep10` | 数值 | 是否预留 10ml |
| `n_alarm` | 数值 | 报警码 |
| `j_progress` | 进度条 | 吸取/喷淋阶段进度 |

按钮事件示例：

```text
// 100ml，预留 10ml
prints "#START,100,1;",0

// 设置速度为 75%
prints "#SPD,75;",0

// 人工清洗完成
prints "#OK;",0

// 急停
prints "#ESTOP;",0
```
