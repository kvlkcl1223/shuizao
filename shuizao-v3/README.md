# 水藻自动化设备 v3 固件说明

本文档说明 `shuizao-v3` 当前固件的自动化流程、手动流程，以及真机调试时最需要修改和确认的位置。

更完整的串口通信协议见：

- `docs/serial_protocol.md`

## 1. 当前版本范围

当前 `v3` 固件沿用 v2 的陶晶驰串口屏协议、自动流程、手动流程、USART2 日志和 LED1 生命灯，但 Z 轴位置检测改为 5 个真实传感器加 2 个定时虚拟喷淋位置。

当前约定：

- 串口屏通信使用 `USART3`。
- 调试日志输出使用 `USART2`，115200 8N1，只输出日志，不接收 HMI 命令。
- LED1 是生命灯，由 TIM5 周期中断分频翻转，上电并启动应用后应持续闪烁。
- 屏幕发给 MCU 的命令格式为 `#命令,参数;`。
- MCU 给屏幕写陶晶驰/Nextion 原生命令，自动追加 `0xFF 0xFF 0xFF`。
- PG1 和 PG2 属于 Y 轴位置检测，目前默认只使用 PG1 作为允许 Z 轴动作的位置。
- Z 轴使用 5 个真实传感器：PG3 上限/原点，PG4 200ml，PG5 150ml，PG6 100ml，PG7 50ml/下限/底部。
- 自动模式只支持 `200/150/100/50ml`。
- 300ml 和 800ml 只作为第二、第三次喷淋的虚拟位置，靠相邻位置定时步进到达；关键虚拟位置时间可通过 HMI 调节并保存到 Flash。
- PG8 当前用于漏水检测，不参与 Z 轴定位；PG9 到 PG16 当前备用。
- 所有 PG 低电平有效。
- 第 1 路 DRV8870 控制 Z 轴。
- 第 2 到第 7 路 DRV8870 控制 6 路蠕动泵。
- 第 8 路 DRV8870 备用。
- 6 路蠕动泵当前作为一个泵组，同时同速同向动作。
- 自动吸取泵速和手动泵速可分别通过屏幕设置，范围为 10% 到 100%；自动喷淋泵速使用代码内置值。
- 第一段喷淋补偿时间可以保存到 Flash；第二段和第三段喷淋时间在程序内固定配置，其他运行参数 MCU 复位或断电后恢复默认值。
- 上电后默认自动复位到最高点，当前最高点为 PG3；屏幕发送 `#RESET;` 软件复位后也会执行同样的上电复位。
- v3 当前不做校准流程，定时步进参数需要真机实测后写入 `My/app_config.c`。

## 2. 代码模块分工

主要业务代码位于 `My/` 目录：

| 文件 | 作用 |
|---|---|
| `My/app.c` / `My/app.h` | 主状态机，负责自动流程、手动流程、停止、急停、回原点、屏幕状态刷新 |
| `My/app_config.c` / `My/app_config.h` | 硬件映射、PG 顺序、体积档位、默认时间、默认速度、屏幕控件名 |
| `My/protocol.c` / `My/protocol.h` | 解析屏幕发来的 `#...;` 文本命令 |
| `My/logger.c` / `My/logger.h` | USART2 调试日志输出 |
| `My/screen.c` / `My/screen.h` | MCU 向陶晶驰屏幕写控件 |
| `My/pg.c` / `My/pg.h` | PG1~PG16 光电输入读取，当前按低电平有效 |
| `My/pump.c` / `My/pump.h` | 6 路蠕动泵统一控制 |
| `My/motor.c` / `My/motor.h` | 8 路 DRV8870 电机/PWM 底层驱动 |

串口中断接入点在：

- `Core/Src/stm32f1xx_it.c`

生命灯定时器回调也在：

- `Core/Src/stm32f1xx_it.c`

主循环入口在：

- `Core/Src/main.c`

## 3. 自动化流程

### 3.0 上电复位流程

MCU 每次上电或执行 `#RESET;` 软件复位后，会自动进入上电复位状态：

1. 初始化 GPIO、DMA、定时器、USART、电机、泵、协议和屏幕。
2. 启动 USART2 调试日志。
3. 启动 TIM5 定时中断，LED1 作为生命灯按 `APP_LED1_HEARTBEAT_TIM5_TICKS` 分频翻转。
4. 停止全部蠕动泵，Z 轴刹车。
5. Z 轴先向下运行 `APP_POWER_ON_RESET_DOWN_MS`。
6. 如果下行过程中提前触发下限位 `APP_Z_BOTTOM_PG`，立即停止下行。
7. 下行结束后，Z 轴先进入 `APP_Z_REVERSE_DEADTIME_MS` 空档停顿，再向最高点运动。
8. 当前默认最高点为 PG3，即 `APP_Z_HOME_PG`。
9. PG3 触发后停止，状态进入 `IDLE`。
10. 如果超过 `APP_HOME_TIMEOUT_MS` 仍未触发 PG3，MCU 会停止全部动作、报 `Z_TIMEOUT`、跳转 `warn` 页面，并停留在上电复位等待人工修正状态。
11. 用户将 Z 轴机械位置处理正确后，在 `warn` 页面点击确认并发送 `#OK;`，MCU 会重新执行上电复位。

上电复位过程中：

- `#START;`、`#MAN;`、`#SET;` 会被当作忙碌状态拒绝。
- `#STOP;` 不取消复位；若处于下行阶段，会停止下行并继续上行寻找 PG3。
- 如果已经卡在上电复位等待人工修正状态，`#STOP;` 不会恢复运动，只会继续保持警告；需要通过 `#OK;` 继续复位。
- `#ESTOP;` 仍然立即停机。
- 如果 Y 轴允许位置 `APP_Y_READY_PG` 不满足，MCU 同样会停止动作、跳转 `warn` 页面，并等待人工修正后通过 `#OK;` 继续复位。

Z 轴所有可能从上行切到下行、或从下行切到上行的动作，都会先空档停顿 `APP_Z_REVERSE_DEADTIME_MS`，避免驱动芯片直接正反转切换。

屏幕通过下面命令启动自动流程：

```text
#START,<volume>,<keep10>;
```

示例：

```text
#START,100,0;
#START,100,1;
#START,200,1;
```

参数含义：

- `volume`：目标总量，当前只支持 `50/100/150/200` ml。
- `keep10`：是否预留 10ml 给科研人员人工清洗接液烧杯，`0` 不预留，`1` 预留。

### 3.1 自动流程总步骤

当前自动流程由 `My/app.c` 中的状态机执行：

1. 接收 `#START,<volume>,<keep10>;`。
2. 检查目标体积是否合法。
3. 根据 `My/app_config.c` 的体积档位表和 `APP_ASPIRATE_STAGES` 吸取阶段表生成本次吸取和喷淋计划。
4. Z 轴先回原点，当前默认原点为 PG3。
5. 检查 Y 轴是否在允许工作位置，当前默认要求 PG1 有效；后续所有 Z 轴动作前和动作中也会持续检查 PG1。
6. 先移动到 800ml 虚拟位置，HOME 到 800ml 的移动不开泵；到达 800ml 后开泵吸取，后续向 700/600/500/400/300ml 和真实 PG 档位下行时泵保持运行。
7. 再进入 200/150/100/50ml 真实 PG 段；下降找 PG 的过程中泵已经开始吸取，到位后继续停留吸取。
8. 如果启用预留 10ml，执行一次定时补吸。
9. 执行三段喷淋。
10. 喷淋结束后自动回原点 PG3。
11. 如果未启用预留 10ml，进入完成状态。
12. 如果启用预留 10ml，回原点后等待科研人员人工清洗接液烧杯并补加 10ml，屏幕点击确认后进入完成状态。

### 3.2 当前体积档位

体积档位表在 `My/app_config.c` 的 `APP_VOLUME_POSITIONS` 中。

当前默认映射：

| 体积 | 吸取逻辑位置 | 第一次喷淋逻辑位置 | 到位方式 |
|---:|---|---|---|
| 200ml | `APP_Z_POS_200ML` | `APP_Z_POS_150ML` | PG4 传感器确认 |
| 150ml | `APP_Z_POS_150ML` | `APP_Z_POS_100ML` | PG5 传感器确认 |
| 100ml | `APP_Z_POS_100ML` | `APP_Z_POS_50ML` | PG6 传感器确认 |
| 50ml | `APP_Z_POS_50ML` | `APP_Z_POS_50ML` | PG7 传感器确认，原地喷 |

注意：

- PG3 当前作为上限/原点，不参与体积吸取档位。
- PG7 当前作为 Z 轴 50ml 位置，同时也是下限/底部。
- 200ml 默认 PG4，150ml 默认 PG5，100ml 默认 PG6，50ml 默认 PG7。
- 800/700/600/500/400/300ml 没有独立传感器，靠相邻定时移动形成虚拟位置。

### 3.3 分阶段吸取逻辑

自动吸取不是直接去目标体积，而是先经过 800ml 到 300ml 的虚拟吸取段，再从 200ml 开始进入真实 PG 定位吸取段。

例如启动：

```text
#START,150,0;
```

当前会执行：

1. 回原点 PG3。
2. 检查 PG1；如果 PG1 后续在 Z 轴动作中失效，立即停机并跳转到 `warn` 页面。
3. 先移动到 800ml 虚拟位置，移动过程不开泵；到 800ml 后开泵吸取默认时间。
4. 继续向 700/600/500/400/300ml 虚拟位置下行，移动和到位停留期间泵都保持吸取。
5. 从 300ml 向 PG4/200ml 移动，泵保持吸取。
6. 到 PG4 后继续停留吸取 200ml 段默认时间。
7. 继续向 PG5/150ml 移动，泵保持吸取不中断。
8. 到 PG5 后继续停留吸取 150ml 段默认时间。
9. 停止吸取，进入喷淋流程。

吸取停留时间按阶段分开定义，当前默认全部为 `3000ms`。这些值表示 100% 满速泵下的标定时间；自动运行时会按当前自动吸取速度自动放大，计算关系近似为 `实际停留时间 = 满速标定时间 * 100 / 自动吸取速度百分比`。例如满速标定 `3000ms`，自动吸取速度为 `60%` 时，本次计划中的停留时间约为 `5000ms`。计算结果仍会受 `APP_TIME_MAX_MS` 上限限制：

| 宏 | 含义 |
|---|---|
| `APP_ASP_DWELL_800_MS` | 800ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_700_MS` | 700ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_600_MS` | 600ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_500_MS` | 500ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_400_MS` | 400ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_300_MS` | 300ml 虚拟位置到位后的吸取时间 |
| `APP_ASP_DWELL_200_MS` | 200ml/PG4 到位后的继续吸取时间 |
| `APP_ASP_DWELL_150_MS` | 150ml/PG5 到位后的继续吸取时间 |
| `APP_ASP_DWELL_100_MS` | 100ml/PG6 到位后的继续吸取时间 |
| `APP_ASP_DWELL_50_MS` | 50ml/PG7 到位后的继续吸取时间 |

如果吸取阶段的停留时间设为 `0U`，并且下一段仍然同方向下行，状态机会连续通过该位置：不主动刹车、不进入停留吸取状态，直接切到下一段移动。这个规则同时适用于 800/700/600/500/400/300ml 虚拟位置，以及 200/150/100ml 这类真实 PG 位置。最终目标位置和后面不再同方向移动的位置仍会停止，例如 50ml/PG7 作为最终目标时不会继续向下。

`#SET,ASP_MS,<time_ms>;` 仍保留给旧协议兼容，但新的分阶段吸取默认不再用它决定各档位停留时间。

每个吸取阶段使用同一个自动吸取泵速：

- 默认宏：`APP_DEFAULT_PUMP_SPEED_PERCENT`
- 运行时屏幕命令：`#SPD,<percent>;`
- 保存命令：`#SAVE,SPD;` 或 `#SAVE,ALL;`
- 读取命令：`#GET,SPD;`
- `#SPD` 只影响吸取和预留 10ml 补吸，不影响喷淋。

手动泵使用独立的手动泵速：

- 运行时屏幕命令：`#MSPD,<percent>;`
- 保存命令：`#SAVE,MSPD;` 或 `#SAVE,ALL;`
- 读取命令：`#GET,MSPD;`
- 手动泵命令速度写 `0` 时使用手动泵速，例如 `#MAN,PUMP,IN,0;` 和 `#MAN,PUMP,OUT,0;`
- 手动吸取和手动喷淋都会受到手动泵速影响。

### 3.4 预留 10ml 的当前逻辑

预留 10ml 的意思是：最终目标总量仍然是用户选择的体积，但机器自动喷洗漏斗后只完成 `目标体积 - 10ml`，剩下 10ml 交给科研人员手动清洗接液烧杯并补加。

例如：

```text
#START,100,1;
```

含义是：

- 目标总量为 100ml。
- 机器自动部分目标为约 90ml。
- 自动喷淋结束后，系统提示人工使用预留 10ml 清洗接液烧杯并补加。

当前因为没有独立 90ml、40ml、790ml 等光电位，固件实现方式是：

1. 先按目标档位定位，例如 100ml 档。
2. 再执行一次定时补吸，时间为 `TRIM10_MS`。
3. 之后进入喷淋流程。
4. 自动流程完成并回原点后，进入人工补加确认状态。

定时补吸时间：

- 默认宏：`APP_TRIM_10ML_MS`
- 运行时屏幕命令：`#SET,TRIM10_MS,<time_ms>;`

屏幕确认命令：

```text
#OK;
```

### 3.5 三段喷淋逻辑

自动喷淋分三次：

1. 第一次：按目标吸取位置的下一档位置喷淋，用于冲击沉淀物、强制混匀。
2. 第二次：固定到 300ml 位置喷淋。
3. 第三次：固定到 800ml 位置喷淋。

当前第一次喷淋位置由 `APP_VOLUME_POSITIONS` 表中的 `first_spray_pos` 决定，而不是在代码里临时计算。50ml 的下一档不存在，所以 50ml 第一次喷淋配置为 `APP_Z_POS_50ML`，也就是原地喷。

当前固定喷淋位置：

- 第二次：`APP_SPRAY_FIXED_STAGE2_POS = APP_Z_POS_300ML`
- 第三次：`APP_SPRAY_FIXED_STAGE3_POS = APP_Z_POS_800ML`

注意：300ml 和 800ml 当前没有 PG 传感器，是虚拟位置，准确性依赖 Z 虚拟位置时间。当前开放调节 `HOME->800`、`800->300`、`300->800`、`200->300` 四段。

4 个自动体积档位的喷淋时间分三段管理：

- 第一段 RAM 运行表：`app_spray_profile_ms`
- 第一段默认时间表：`APP_SPRAY_PUMP_MS`
- 第二段 300ml 固定时间表：`APP_SPRAY_STAGE2_PUMP_MS`
- 第三段 800ml 固定时间表：`APP_SPRAY_STAGE3_PUMP_MS`
- 默认值位置：`My/app_config.c`
- 档位顺序：`200/150/100/50`
- 下标 `0~5` 对应泵 `1~6`
- 自动运行时，MCU 按 `#START` 的体积选择对应档位的 6 个喷淋时间
- 屏幕 `#SET,SPRAYx_MS,<time>,<volume>` 只修改第一段喷淋时间
- 第二段和第三段不受屏幕调节影响，需要修改程序内固定表
- 每段喷淋开始时 6 个泵同时启动，每个泵按自己的补偿时间停止
- 6 个泵全部停止后，才移动到下一段喷淋位置

第一段喷淋补偿时间可以通过串口屏滑轴修改。HMI 用本地变量 `n_spray_vol.val` 保存当前调节档位，读取时发送 `#GET,SPRAY_MS,<volume>;`，滑轴弹起时发送 `#SET,SPRAY1_MS,<time_ms>,<volume>;` 这类带体积的命令。点击保存按钮发送 `#SAVE,SPRAY_MS;` 后，4 个档位共 24 个第一段喷淋时间写入 Flash。MCU 下次初始化时优先读取 Flash 保存值，Flash 无效时才使用 `APP_SPRAY_PUMP_MS` 默认值填充第一段。

### 3.6 自动流程中的停止和急停

自动流程运行中：

- `#STOP;`：停止当前泵和 Z 轴动作，然后回原点。
- 上电复位状态下发送 `#STOP;`：不取消复位，仍继续回最高点。
- `#ESTOP;`：立即停泵并刹停 Z 轴，不自动回原点。

急停后建议人工确认机械状态，再发送：

```text
#HOME;
```

## 4. 手动流程

手动控制主要用于真机调试、排液、确认方向和确认光电位置。

自动流程运行中会拒绝手动命令，避免手动控制和状态机同时抢电机。

### 4.1 手动 Z 轴

命令：

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

当前保护：

- 上升到 PG3 自动停止。
- 下降到 PG7 自动停止，PG7 同时是 50ml 和下限位。
- 吸取速度参数仍按百分比处理，低于 10% 会夹到 10%。

陶晶驰按钮建议：

- 按下事件发送 `#MAN,Z,UP,50;` 或 `#MAN,Z,DOWN,50;`
- 松开事件发送 `#MAN,Z,STOP;`

### 4.2 手动蠕动泵

命令：

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

当前行为：

- 6 路泵同时运行。
- `IN` 表示吸取方向。
- `OUT` 表示喷淋/排出方向。
- 手动页面当前没有单泵独立控制；自动喷淋内部会按补偿时间分别停止单泵。

陶晶驰按钮建议：

- 泵吸取按钮按下发送 `#MAN,PUMP,IN,60;`
- 泵吸取按钮松开发送 `#MAN,PUMP,STOP;`
- 泵喷淋按钮按下发送 `#MAN,PUMP,OUT,60;`
- 泵喷淋按钮松开发送 `#MAN,PUMP,STOP;`

### 4.3 手动回原点

命令：

```text
#HOME;
```

当前行为：

- Z 轴向原点方向运动。
- 到达 PG3 后停止。

### 4.4 查询状态和 PG

命令：

```text
#GET,STATE;
#GET,PG;
```

当前行为：

- MCU 立即刷新屏幕状态控件。
- `n_pgmask.val` 可用于观察 PG1~PG16 哪一路正在触发。

PG 掩码：

- bit0 = PG1
- bit1 = PG2
- bit2 = PG3
- 以此类推
- 掩码中 `1` 表示对应 PG 当前有效

## 5. 屏幕侧当前需要的控件

MCU 默认写这些控件名，定义在 `My/app_config.h`：

| 控件名 | 类型建议 | 作用 |
|---|---|---|
| `t6` | 文本 | MCU 状态短文本 |
| `n_state` | 数值 | MCU 状态码 |
| `n_phase` | 数值 | 当前阶段号 |
| `n_speed` | 数值 | 当前吸取泵速百分比 |
| `h_speed` | 滑轴 | 当前吸取泵速百分比 |
| `n_mspd` | 数值 | 当前手动泵速百分比 |
| `h_mspd` | 滑轴 | 当前手动泵速百分比 |
| `n_pgmask` | 数值 | PG 有效掩码 |
| `n_keep10` | 数值 | 是否预留 10ml |
| `n_alarm` | 数值 | 报警码 |
| `j_progress` | 进度条 | 当前定时阶段进度 |
| `h_spray1_ms` ~ `h_spray6_ms` | 滑轴 | 泵 1~6 第一段喷淋补偿时间 |
| `n_spray1_ms` ~ `n_spray6_ms` | 数值 | 泵 1~6 第一段喷淋补偿时间 |

Y 轴异常警告页面：

- MCU 默认发送 `page warn`。
- 页面名由 `My/app_config.h` 的 `APP_SCREEN_WARNING_PAGE` 定义。
- 当 PG1 在任意 Z 轴动作前或动作中无效，MCU 会报 `Y_NOT_READY`，停止执行机构，并跳转到该页面。

如果 HMI 工程中控件名不同，优先修改：

- `My/app_config.h` 中的 `APP_SCREEN_*_OBJ`

建议 HMI 用 `n_state.val` 和 `n_alarm.val` 做中文界面逻辑，不要依赖 `t6` 的英文短文本。

第一段喷淋补偿时间页面：

- 页面打开时先设置 `n_spray_vol.val`，再发送 `#GET,SPRAY_MS,<n_spray_vol>;`，MCU 会回填该档位的 6 个滑轴和 6 个数值控件。
- 切换喷淋档位按钮只修改 HMI 本地的 `n_spray_vol.val` 和 `t_spray_vol.txt`，不直接发送串口命令。
- 滑轴弹起时发送 `#SET,SPRAYx_MS,<value>,<n_spray_vol>;`。
- 保存按钮按下或弹起时发送 `#SAVE,SPRAY_MS;`。

## 6. 状态码

| 状态码 | 名称 | 含义 |
|---:|---|---|
| 0 | `IDLE` | 空闲 |
| 1 | `HOMING` | 回原点 |
| 2 | `CHECK_Y` | 检查 Y 轴位置 |
| 3 | `MOVE_TO_ASPIRATE` | 移动到吸取位置 |
| 4 | `ASPIRATING` | 吸取中 |
| 5 | `TRIM_ASPIRATING` | 预留 10ml 时定时补吸 |
| 6 | `MOVE_TO_SPRAY` | 移动到喷淋位置 |
| 7 | `SPRAYING` | 喷淋中 |
| 8 | `RETURN_HOME` | 回原点 |
| 9 | `WAIT_MANUAL_CUP_CLEAN` | 等待人工清洗接液烧杯并补加 10ml |
| 10 | `DONE` | 完成 |
| 11 | `ERROR` | 故障 |
| 12 | `ESTOP` | 急停 |
| 13 | `MANUAL` | 手动控制 |
| 14 | `POWER_ON_RESET` | 上电复位到最高点中 |

## 7. 报警码

| 报警码 | 名称 | 含义 |
|---:|---|---|
| 0 | `NONE` | 无报警 |
| 1 | `BUSY` | 当前忙，拒绝命令 |
| 2 | `BAD_VOLUME` | 体积参数错误 |
| 3 | `Y_NOT_READY` | Y 轴不在允许位置 |
| 4 | `Z_TIMEOUT` | Z 轴到位超时 |
| 5 | `BAD_COMMAND` | 命令无效 |
| 6 | `BAD_CONFIG` | 固件体积或喷淋配置错误 |
| 7 | `SAVE_FAILED` | Flash 保存喷淋补偿时间失败 |

## 8. 真机调试时最需要修改的地方

这一节是现场调试的重点。先按优先级改这些位置，不要一上来改状态机。

### 8.1 修改 Z 轴真实传感器映射

文件：

- `My/app_config.c`

重点修改：

```c
const PG_ID APP_Z_ORDER[] = {
    PG_3,
    PG_4,
    PG_5,
    PG_6,
    PG_7,
};
```

作用：

- 告诉固件 Z 轴从上到下有哪些真实传感器。
- 当前固定为 PG3 上限、PG4 200ml、PG5 150ml、PG6 100ml、PG7 50ml/下限。
- 如果 PCB 接线变化，先改 `APP_Z_HOME_PG`、`APP_Z_200ML_PG`、`APP_Z_150ML_PG`、`APP_Z_100ML_PG`、`APP_Z_50ML_PG`、`APP_Z_BOTTOM_PG` 和这个顺序表。

### 8.2 修改 Z 轴相邻步进时间

文件：

- `My/app_config.c`

重点修改：

```c
const uint32_t APP_Z_STEP_DOWN_MS[APP_Z_STEP_COUNT] = {
    APP_Z_STEP_DEFAULT_MS, /* HOME -> 800 */
    APP_Z_STEP_DEFAULT_MS, /* 800  -> 300 */
    APP_Z_STEP_DEFAULT_MS, /* 300  -> 200 */
    APP_Z_STEP_DEFAULT_MS, /* 200  -> 150 */
    APP_Z_STEP_DEFAULT_MS, /* 150  -> 100 */
    APP_Z_STEP_DEFAULT_MS, /* 100  -> 50 */
};
```

`APP_Z_STEP_UP_MS` 是反方向时间表，建议单独实测，不要直接复制下行时间。

### 8.3 修改体积档位和第一次喷淋位置

文件：

- `My/app_config.c`

重点修改：

```c
const App_VolumePosition APP_VOLUME_POSITIONS[] = {
    {200U, APP_Z_POS_200ML, APP_Z_POS_150ML, 1U},
    {150U, APP_Z_POS_150ML, APP_Z_POS_100ML, 1U},
    {100U, APP_Z_POS_100ML, APP_Z_POS_50ML,  1U},
    {50U,  APP_Z_POS_50ML,  APP_Z_POS_50ML,  1U},
};
```

每行含义：

```c
{体积ml, 吸取逻辑位置, 第一次喷淋逻辑位置, 是否定位吸取}
```

现场最可能要改：

- 第一次喷淋到底应该去目标下一档的哪个逻辑位置。
- 50ml 是否仍然保持第一次原地喷。

当前 `precise_aspirate` 字段已经预留，但状态机暂时没有为它做独立动作。后续如果要让“定位吸取”和“停一段时间吸取”有不同行为，就从这个字段扩展。

### 8.4 修改固定喷淋体积

文件：

- `My/app_config.c`

重点修改：

```c
const App_ZPosition APP_SPRAY_FIXED_STAGE2_POS = APP_Z_POS_300ML;
const App_ZPosition APP_SPRAY_FIXED_STAGE3_POS = APP_Z_POS_800ML;
```

作用：

- 第二次喷淋固定去 300ml 虚拟位置。
- 第三次喷淋固定去 800ml 虚拟位置。

如果真实工艺调整为其他固定喷淋点，只改这里即可。

### 8.5 修改 Y 轴允许工作位置

文件：

- `My/app_config.c`

重点修改：

```c
const PG_ID APP_Y_READY_PG = PG_1;
```

当前默认 PG1 有效才允许自动流程继续，也才允许 Z 轴上行、下行、回原点和上电复位动作继续执行。

如果 Z 轴动作前或动作中 PG1 失效，固件会：

- 停止全部蠕动泵。
- 停止 Z 轴。
- 报警 `Y_NOT_READY`。
- 发送 `page warn` 跳转到 HMI 的 `warn` 页面。

如果现场确认应该用 PG2，或未来 Y 轴需要更多位置判断，就从这里扩展。

### 8.6 修改 Z 轴原点和底部

文件：

- `My/app_config.c`
- `My/app_config.h`

重点修改：

```c
const PG_ID APP_Z_HOME_PG = PG_3;
const PG_ID APP_Z_200ML_PG = PG_4;
const PG_ID APP_Z_150ML_PG = PG_5;
const PG_ID APP_Z_100ML_PG = PG_6;
const PG_ID APP_Z_50ML_PG = PG_7;
const PG_ID APP_Z_BOTTOM_PG = PG_7;
```

作用：

- `APP_Z_HOME_PG` 用于上电复位目标、自动回原点、手动上升限位。
- `APP_Z_200ML_PG` 用于 200ml 逻辑位置确认。
- `APP_Z_150ML_PG` 用于 150ml 逻辑位置确认。
- `APP_Z_100ML_PG` 用于 100ml 逻辑位置确认。
- `APP_Z_50ML_PG` 用于 50ml 逻辑位置确认。
- `APP_Z_BOTTOM_PG` 当前等于 PG7，用于手动下降限位和上电下探提前停止。

上电复位开关在 `My/app_config.h`：

```c
#define APP_POWER_ON_RESET_ENABLE       1U
```

如果临时不想上电自动回最高点，可改为 `0U`；正常真机版本建议保持 `1U`。

### 8.7 修改电机分配和方向

文件：

- `My/app_config.c`

重点修改：

```c
const Motor_ID APP_Z_MOTOR_ID = MOTOR_1;

const Motor_ID APP_PUMP_MOTOR_IDS[APP_PUMP_COUNT] = {
    MOTOR_2,
    MOTOR_3,
    MOTOR_4,
    MOTOR_5,
    MOTOR_6,
    MOTOR_7,
};

const uint8_t APP_Z_UP_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_Z_DOWN_DIRECTION = MOTOR_REVERSE;
const uint8_t APP_PUMP_IN_DIRECTION = MOTOR_FORWARD;
const uint8_t APP_PUMP_OUT_DIRECTION = MOTOR_REVERSE;
```

现场调试判断：

- 如果 Z 轴上升按钮实际向下走，交换 `APP_Z_UP_DIRECTION` 和 `APP_Z_DOWN_DIRECTION`。
- 如果泵吸取按钮实际在排出，交换 `APP_PUMP_IN_DIRECTION` 和 `APP_PUMP_OUT_DIRECTION`。
- 如果电机接线编号变了，改 `APP_Z_MOTOR_ID` 或 `APP_PUMP_MOTOR_IDS`。

### 8.8 修改默认速度、时间和超时

文件：

- `My/app_config.h`

重点修改：

```c
#define APP_MIN_PUMP_SPEED_PERCENT      10U
#define APP_MAX_PUMP_SPEED_PERCENT      100U
#define APP_DEFAULT_PUMP_SPEED_PERCENT  60U
#define APP_SPRAY_SPEED_PERCENT         100U
#define APP_Z_SPEED_PERCENT             70U

#define APP_TIME_MIN_MS                 0U
#define APP_TIME_MAX_MS                 6000U
#define APP_ASPIRATE_PHASE_MS           5000U
#define APP_ASP_DWELL_800_MS            3000U
#define APP_ASP_DWELL_700_MS            3000U
#define APP_ASP_DWELL_600_MS            3000U
#define APP_ASP_DWELL_500_MS            3000U
#define APP_ASP_DWELL_400_MS            3000U
#define APP_ASP_DWELL_300_MS            3000U
#define APP_ASP_DWELL_200_MS            3000U
#define APP_ASP_DWELL_150_MS            3000U
#define APP_ASP_DWELL_100_MS            3000U
#define APP_ASP_DWELL_50_MS             3000U
#define APP_TRIM_10ML_MS                1000U
#define APP_SPRAY_PUMP1_MS              5000U
#define APP_SPRAY_PUMP2_MS              5000U
#define APP_SPRAY_PUMP3_MS              5000U
#define APP_SPRAY_PUMP4_MS              5000U
#define APP_SPRAY_PUMP5_MS              5000U
#define APP_SPRAY_PUMP6_MS              5000U

#define APP_Z_MOVE_TIMEOUT_MS           30000U
#define APP_HOME_TIMEOUT_MS             45000U
#define APP_Z_REVERSE_DEADTIME_MS       300U
#define APP_POWER_ON_RESET_DOWN_MS      1000U

#define APP_ZVIRT_TIME_MIN_MS           1000U
#define APP_ZVIRT_TIME_MAX_MS           20000U
#define APP_ZVIRT_TIME_DEFAULT_MS       3000U
```

说明：

- 屏幕上的 `#SPD` 和 `#MSPD` 先修改 RAM，`#SAVE,SPD;`、`#SAVE,MSPD;` 或 `#SAVE,ALL;` 后保存到 Flash；`#SET,ASP_MS` 和 `#SET,TRIM10_MS` 只在本次上电运行内生效，不保存。
- `#SPD` 只改变自动吸取速度；`#MSPD` 只改变手动泵速度；自动喷淋速度固定使用 `APP_SPRAY_SPEED_PERCENT`。
- 屏幕上的 `#SET,SPRAY1_MS,<time_ms>,<volume>` 到 `#SET,SPRAY6_MS,<time_ms>,<volume>` 会先修改指定体积档位的第一段 RAM，`#SAVE,SPRAY_MS;` 才会把 4 个档位共 24 个第一段喷淋补偿时间保存到 Flash。
- 第二段 300ml 和第三段 800ml 的喷淋补偿时间不受屏幕滑轴影响，在 `My/app_config.c` 的 `APP_SPRAY_STAGE2_PUMP_MS`、`APP_SPRAY_STAGE3_PUMP_MS` 中按体积档位和泵编号分别修改。
- 屏幕上的 `#SET,Z_DN_HOME_800_MS,<time_ms>`、`#SET,Z_DN_800_300_MS,<time_ms>`、`#SET,Z_UP_300_800_MS,<time_ms>`、`#SET,Z_UP_200_300_MS,<time_ms>` 会先修改 RAM，`#SAVE,ZVIRT_MS;` 或 `#SAVE,ALL;` 写入 Flash。
- 如果调试后确定了稳定工艺参数，应把默认值写回 `My/app_config.h`。
- 若 Z 轴实际移动距离很长导致误报超时，调大 `APP_Z_MOVE_TIMEOUT_MS` 或 `APP_HOME_TIMEOUT_MS`。
- `APP_POWER_ON_RESET_DOWN_MS` 是上电复位开始时的下探时间；如果触发 PG7，会提前结束下探。
- `APP_Z_REVERSE_DEADTIME_MS` 是 Z 轴反向前的空档停顿时间；如果实际驱动仍有冲击或保护报警，应适当调大。

### 8.9 修改 USART2 日志和 LED1 生命灯

文件：

- `My/app_config.h`
- `Core/Src/stm32f1xx_it.c`
- `My/app.c`

日志开关：

```c
#define APP_LOG_ENABLE                  1U
#define APP_LOG_UART_TIMEOUT_MS         50U
#define APP_LED1_HEARTBEAT_TIM5_TICKS   1000U
```

说明：

- `USART2` 是调试日志口，默认 115200 8N1。
- `USART3` 仍然是陶晶驰串口屏通信口。
- `APP_LOG_ENABLE` 改为 `0U` 可以关闭日志输出。
- 日志在 `My/logger.c` 中实现，已加入 Keil 工程的 `My` 分组。
- 日志正文全部保持 ASCII，不输出中文；中文含义见本 README 和 `docs/serial_protocol.md`，避免 XCOM 这类无 UTF-8 选项的串口助手乱码。
- 日志格式为 `[0000012345ms][TAG] key=value key=value ...`，时间戳来自 `HAL_GetTick()`。
- LED1 在 `HAL_TIM_PeriodElapsedCallback()` 中由 TIM5 分频翻转。
- `App_Init()` 中调用 `HAL_TIM_Base_Start_IT(&htim5)`，如果 CubeMX 后续重生成时改了定时器编号，要同步改这里和中断回调里的 TIM 判断。
- 当前 `tim.c` 参数看起来约为 1ms 中断，所以默认 `APP_LED1_HEARTBEAT_TIM5_TICKS=1000U`；如果现场确认 TIM5 已经是 1s 中断，改成 `1U`。

常见日志字段：

| 字段 | 中文含义 |
|---|---|
| `code` | 状态码或报警码 |
| `name` | 状态或报警的英文名称 |
| `pgmask` | PG1~PG16 有效掩码，`1` 表示低电平有效触发 |
| `step_target` | 本次相邻步进要到达的逻辑位置 |
| `step_index` | 本次相邻步进目标的逻辑位置索引 |
| `final_target` | 当前大目标逻辑位置 |
| `final_index` | 当前大目标逻辑位置索引 |
| `current_index` | 固件记录的当前逻辑位置索引，`-1` 表示未知 |
| `sensor_pg` | 该逻辑位置对应的真实 PG；`PG0` 表示无传感器、靠定时 |
| `mode` | `SENSOR` 表示等 PG，`TIME` 表示定时步进 |
| `dir` | Z 轴运动方向，`FORWARD/REVERSE/STOP` |
| `limit_ms` | 本次相邻步进的时间上限或传感器超时 |
| `elapsed_ms` | 当前阶段已用时间 |
| `timeout_ms` | 当前移动允许的超时时间 |
| `active_mask` | 喷淋时仍在运行的泵掩码，bit0 对应泵 1 |

典型日志：

```text
[0000000012ms][BOOT] logger_ready uart=USART2 baud=115200 format=ASCII
[0000000020ms][CMD] rx=START volume_ml=100 keep10=1 pgmask=0x0004
[0000000025ms][AUTO] plan volume_ml=100 machine_ml=90 reserved_ml=10 asp_count=3 spray_count=3 trim10=1 pump_speed=60%
[0000000030ms][STATE] code=1 name=HOMING pgmask=0x0004
[0000000032ms][MOVE] step_target=HOME step_index=0 final_target=HOME final_index=0 current_index=-1 mode=SENSOR sensor_pg=PG3 dir=FORWARD motor_speed=700 limit_ms=45000 pgmask=0x0000
[0000045001ms][ALARM] code=4 name=Z_TIMEOUT state=14/POWER_ON_RESET target_pos=HOME target_index=0 sensor_pg=PG3 pgmask=0x0000 elapsed_ms=45001 timeout_ms=45000
```

### 8.10 修改屏幕控件名

文件：

- `My/app_config.h`

重点修改：

```c
#define APP_SCREEN_MESSAGE_OBJ          "t6"
#define APP_SCREEN_STATE_OBJ            "n_state"
#define APP_SCREEN_PHASE_OBJ            "n_phase"
#define APP_SCREEN_SPEED_OBJ            "n_speed"
#define APP_SCREEN_SPEED_SLIDER_OBJ     "h_speed"
#define APP_SCREEN_MANUAL_SPEED_OBJ     "n_mspd"
#define APP_SCREEN_MANUAL_SPEED_SLIDER_OBJ "h_mspd"
#define APP_SCREEN_PGMASK_OBJ           "n_pgmask"
#define APP_SCREEN_KEEP10_OBJ           "n_keep10"
#define APP_SCREEN_ALARM_OBJ            "n_alarm"
#define APP_SCREEN_PROGRESS_OBJ         "j_progress"
#define APP_SCREEN_WARNING_PAGE         "warn"
#define APP_SCREEN_SPRAY1_SLIDER_OBJ    "h_spray1_ms"
#define APP_SCREEN_SPRAY2_SLIDER_OBJ    "h_spray2_ms"
#define APP_SCREEN_SPRAY3_SLIDER_OBJ    "h_spray3_ms"
#define APP_SCREEN_SPRAY4_SLIDER_OBJ    "h_spray4_ms"
#define APP_SCREEN_SPRAY5_SLIDER_OBJ    "h_spray5_ms"
#define APP_SCREEN_SPRAY6_SLIDER_OBJ    "h_spray6_ms"
#define APP_SCREEN_SPRAY1_VALUE_OBJ     "n_spray1_ms"
#define APP_SCREEN_SPRAY2_VALUE_OBJ     "n_spray2_ms"
#define APP_SCREEN_SPRAY3_VALUE_OBJ     "n_spray3_ms"
#define APP_SCREEN_SPRAY4_VALUE_OBJ     "n_spray4_ms"
#define APP_SCREEN_SPRAY5_VALUE_OBJ     "n_spray5_ms"
#define APP_SCREEN_SPRAY6_VALUE_OBJ     "n_spray6_ms"
```

如果陶晶驰工程里的控件名不一致，要么改 HMI 控件名，要么改这里。两边必须一致。

### 8.11 修改 PG 引脚映射

文件：

- `Core/Inc/main.h`
- `Core/Src/gpio.c`
- `My/pg.c`
- `shuizao.ioc`

当前 `My/pg.c` 使用 CubeMX 生成的 `PG1_Pin`、`PG1_GPIO_Port` 这类宏。

如果只是 PG 物理顺序变化，不需要改 `My/pg.c`，改 `My/app_config.c` 即可。

只有在下面情况才需要动引脚映射：

- PCB 实际接线和 CubeMX 中 PG1~PG16 的 GPIO 定义不一致。
- 新增或删减 PG 输入。
- PG 命名不再是 PG1~PG16。

建议做法：

1. 优先在 CubeMX / `.ioc` 中修改 GPIO 标签。
2. 重新生成工程。
3. 确认 `Core/Inc/main.h` 中仍有 `PG1_Pin` 到 `PG16_Pin`。
4. 确认 `My/pg.c` 的 `pg_table` 顺序和 `PG_ID` 枚举一致。

### 8.12 修改串口协议

文件：

- `My/protocol.c`
- `My/protocol.h`
- `docs/serial_protocol.md`

只有在 HMI 需要新增命令时才改协议。当前已经支持：

- `#START,<volume>,<keep10>;`
- `#SPD,<percent>;`
- `#MSPD,<percent>;`
- `#SET,<param>,<time_ms>;`
- `#STOP;`
- `#ESTOP;`
- `#HOME;`
- `#OK;`
- `#MAN,...;`
- `#GET,STATE;`
- `#GET,PG;`
- `#GET,SPRAY_MS,<volume>;`
- `#GET,ZVIRT_MS;`
- `#GET,SPD;`
- `#GET,MSPD;`
- `#SAVE,SPRAY_MS;`
- `#SAVE,ZVIRT_MS;`
- `#SAVE,SPD;`
- `#SAVE,MSPD;`
- `#SAVE,ALL;`
- `#RESET;`

新增协议时必须同步改 `docs/serial_protocol.md`，否则后面 HMI 事件会很容易写错。

## 9. 推荐真机调试顺序

建议按下面顺序调，不要一开始就跑完整自动流程。

### 9.1 上电基础检查

1. 烧录固件。
2. 打开 USART2 串口助手，参数 115200 8N1。
3. 确认能看到 `[BOOT] logger_ready ...`、`[BOOT] tim5_heartbeat_start ...` 等启动日志。
4. 确认 LED1 大约每 1s 翻转一次。如果不闪，优先检查 TIM5 是否启动、`APP_LED1_HEARTBEAT_TIM5_TICKS` 是否匹配 TIM5 周期、LED1 引脚是否仍为 PB4。
5. 打开串口屏页面。
6. 确认上电后 `n_state.val` 先进入 14，或 `t6` 显示 `PWR HOME`。
7. 确认 Z 轴向最高点 PG3 运动。
8. PG3 触发后，确认 `t6` 显示 `READY`，或 `n_state.val` 为 0。
9. 发送 `#GET,STATE;`，确认屏幕状态控件能刷新，同时 USART2 应输出 `[CMD] rx=GET_STATE ...`。

### 9.2 PG 输入检查

1. 逐个遮挡或触发 PG1~PG16。
2. 发送 `#GET,PG;`。
3. 观察 `n_pgmask.val` 的 bit 是否变化。
4. 确认 PG 低电平有效是否符合现场硬件。

如果 PG 有效电平反了，修改：

- `My/pg.c` 中 `PG_IsActive()` 的判断。

当前判断：

```c
return PG_ReadRaw(id) == GPIO_PIN_RESET;
```

### 9.3 Z 轴方向和限位检查

1. 用低速发送 `#MAN,Z,UP,10;`。
2. 确认 Z 轴确实向上。
3. 发送 `#MAN,Z,STOP;`。
4. 用低速发送 `#MAN,Z,DOWN,10;`。
5. 确认 Z 轴确实向下。
6. 确认到 PG3 上限会停，到 PG7 下限会停。

如果方向反了，改 `My/app_config.c` 的 Z 轴方向映射。

### 9.4 泵方向检查

1. 发送 `#MAN,PUMP,IN,10;`。
2. 确认 6 路泵都是吸取方向。
3. 发送 `#MAN,PUMP,STOP;`。
4. 发送 `#MAN,PUMP,OUT,10;`。
5. 确认 6 路泵都是喷淋/排出方向。

如果方向反了，改 `My/app_config.c` 的泵方向映射。

如果某一路泵和其他泵方向不同，优先检查接线；若硬件确实需要软件单独反向，就需要扩展 `pump.c`，不能只改当前方向常量。

### 9.5 体积档位检查

1. 用手动 Z 轴逐档移动。
2. 记录每个体积实际触发的是哪个 PG。
3. 先修改 `My/app_config.c` 的 `APP_Z_STEP_DOWN_MS` 和 `APP_Z_STEP_UP_MS`。
4. 再确认 `APP_VOLUME_POSITIONS` 的吸取逻辑位置和第一次喷淋逻辑位置是否符合工艺。

这一步很关键。体积档位不准时，不要先调吸取时间，先改 PG 表。

### 9.6 单次短流程检查

建议先把时间调短，例如：

```text
#SET,ASP_MS,500;
#SET,TRIM10_MS,300;
```

然后跑一个小体积：

```text
#START,50,0;
```

确认：

- 会先回原点。
- PG1 未有效时会报 `Y_NOT_READY`。
- PG1 有效后会按体积表移动。
- 吸取方向正确。
- 三段喷淋顺序正确。
- 结束后回 PG3。

### 9.7 预留 10ml 流程检查

发送：

```text
#START,100,1;
```

确认：

- 自动流程中会执行 `TRIM_ASPIRATING`。
- 喷淋结束后回原点。
- 回原点后进入 `WAIT_MANUAL_CUP_CLEAN`，状态码为 9。
- HMI 显示人工清洗接液烧杯并补加 10ml 的提示。
- 点击确认发送 `#OK;` 后状态变为 `DONE`。

## 10. 当前实现的限制和后续扩展点

当前实现有几个有意保留的限制：

- 6 路泵不区分泵组，全部同时运行。
- 吸取停留时间已经按 800/700/600/500/400/300/200/150/100/50 分开写成代码默认值，暂不支持 HMI 调节和 Flash 保存。
- 同一个自动体积档位内部，第一段使用屏幕/Flash 可调的 6 泵补偿时间；第二段和第三段使用程序固定的 6 泵补偿时间。
- 真实 PG 段的定位吸取已由 `APP_ASPIRATE_STAGES` 控制，`precise_aspirate` 字段保留但当前不作为主流程判断依据。
- `keep10` 不保存，每次 START 时由屏幕传入。
- 运行时设置的自动吸取速度可通过 `#SAVE,SPD;` 或 `#SAVE,ALL;` 保存；手动泵速度可通过 `#SAVE,MSPD;` 或 `#SAVE,ALL;` 保存；旧版 `ASP_MS` 不保存。
- 当前没有自动校准流程。

如果后续要扩展：

- 单泵或分泵组控制：扩展 `My/pump.c` 和协议。
- HMI 调节吸取分段时间：为 `APP_ASP_DWELL_800_MS` 到 `APP_ASP_DWELL_50_MS` 增加协议、控件回填和 Flash 保存。
- 更多参数保存：继续扩展 Flash/EEPROM 保存层，并定义保存/恢复协议。
- 更复杂的 Y 轴流程：扩展 `APP_Y_READY_PG` 为 Y 轴位置表。
- 更多体积档位：扩展 `APP_VOLUME_POSITIONS`，同时确认 `APP_MAX_AUTO_PHASES` 足够。

## 11. 编译检查

本工程当前可用 `arm-none-eabi-gcc` 做语法检查，不生成固件：

```powershell
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -finput-charset=UTF-8 -fsyntax-only -DUSE_HAL_DRIVER -DSTM32F103xE -I.\Core\Inc -I.\Drivers\STM32F1xx_HAL_Driver\Inc -I.\Drivers\STM32F1xx_HAL_Driver\Inc\Legacy -I.\Drivers\CMSIS\Device\ST\STM32F1xx\Include -I.\Drivers\CMSIS\Include -I.\My .\Core\Src\main.c .\Core\Src\stm32f1xx_it.c .\My\motor.c .\My\app_config.c .\My\pg.c .\My\pump.c .\My\screen.c .\My\protocol.c .\My\logger.c .\My\app.c
```

在 Keil MDK 中编译时，确认 `MDK-ARM/shuizao.uvprojx` 的 `My` 分组包含以下文件：

- `app.c`
- `app_config.c`
- `pg.c`
- `pump.c`
- `protocol.c`
- `screen.c`
- `motor.c`
- `logger.c`

## 12. 漏水检测

当前漏水检测使用 `PG8`，配置位置在 `My/app_config.c`：

```c
const PG_ID APP_LEAK_PG = PG_8;
```

漏水输入的电平定义和普通位置 PG 的“触发”语义不同：

- `PG8` 高电平：正常
- `PG8` 低电平：漏水异常

因此漏水检测代码读取的是 `PG_ReadRaw(APP_LEAK_PG)` 原始电平，而不是把 `PG_IsActive(APP_LEAK_PG)` 当成异常判断。

相关宏在 `My/app_config.h`：

```c
#define APP_LEAK_DETECT_ENABLE 1U
#define APP_LEAK_DETECT_TEST_ONLY 0U
#define APP_LEAK_CHECK_INTERVAL_MS 200U
#define APP_LEAK_TEST_LOG_INTERVAL_MS 1000U
#define APP_LEAK_DEBOUNCE_COUNT 3U
#define APP_SCREEN_LEAK_WARNING_PAGE "leak_warn"
```

当前默认是正式模式，`APP_LEAK_DETECT_TEST_ONLY` 为 `0U`。MCU 会定时检测 PG8，连续低电平异常达到防抖次数后触发 `APP_ALARM_LEAK_DETECTED`，报警码为 `8`，立即停止全部动作，并跳转到 `leak_warn` 页面。

如果需要临时测试漏水输入，把 `APP_LEAK_DETECT_TEST_ONLY` 改为 `1U`。测试模式下 MCU 只通过 USART2 输出漏水状态日志，不停止电机和泵，也不跳转屏幕报警页面。

HMI 需要新增页面：

```text
leak_warn
```

这个页面建议放固定中文提示，例如“检测到漏水，请关闭设备并检查管路”。如果页面名不同，需要同步修改 `APP_SCREEN_LEAK_WARNING_PAGE`。

## 13. 最容易踩错的点

- 陶晶驰控件名必须和 `My/app_config.h` 一致。
- 上电后会自动回最高点 PG3，调试时要先保证 Z 轴上升方向和 PG3 限位可靠。
- 屏幕发命令必须带 `#` 和 `;`。
- 陶晶驰 `prints` 发送字符串时不要漏掉英文逗号。
- PG 低电平有效，观察 `n_pgmask` 时 `1` 表示触发。
- Z 轴方向反了不要改状态机，先改 `APP_Z_UP_DIRECTION` 和 `APP_Z_DOWN_DIRECTION`。
- 泵方向反了不要改自动流程，先改 `APP_PUMP_IN_DIRECTION` 和 `APP_PUMP_OUT_DIRECTION`。
- 虚拟体积位置不准优先改 `APP_Z_STEP_DOWN_MS` 和 `APP_Z_STEP_UP_MS`。
- 第一次喷淋逻辑位置不符合工艺时再改 `APP_VOLUME_POSITIONS`。
- 第二、第三次喷淋固定位置错了改 `APP_SPRAY_FIXED_STAGE2_POS` 和 `APP_SPRAY_FIXED_STAGE3_POS`。
- `#SET,ASP_MS` 和 `#SET,TRIM10_MS` 不保存，复位后会恢复默认值；`#SPD` 和 `#MSPD` 可通过保存命令写入 Flash。
- `#SPD` 和 `#MSPD` 都不影响自动喷淋；自动喷淋速度需要修改 `APP_SPRAY_SPEED_PERCENT` 后重新烧录。
- `#SET,SPRAY1_MS,<time_ms>,<volume>` 到 `#SET,SPRAY6_MS,<time_ms>,<volume>` 只改指定体积档位的第一段 RAM；`#SAVE,SPRAY_MS;` 会保存 4 个档位共 24 个第一段喷淋补偿时间到 Flash。
- 当前没有 10ml 光电位逻辑，预留 10ml 是自动流程结束后的人工补加逻辑。
