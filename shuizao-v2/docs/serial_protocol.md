# 陶晶驰串口屏与 STM32 通信协议

本文档对应 `shuizao-v2` 当前固件。串口屏连接 `USART3`。

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无
- MCU 接收方式：USART3 DMA + IDLE 中断
- 字符编码：屏幕发给 MCU 的命令使用 ASCII
- MCU 上电或收到 `#RESET;` 软件复位后，会自动复位到最高点；当前最高点为 PG3。

## 1. 基本帧格式

屏幕发送给 MCU 的控制命令使用文本帧：

```text
#命令,参数1,参数2;
```

规则：

- `#` 为帧头。
- `;` 为帧尾。
- 参数用英文逗号 `,` 分隔。
- 命令区分大小写，建议全部大写。
- 当前版本无校验和，便于陶晶驰按钮事件直接发送。
- MCU 支持一包串口数据里包含多条命令，例如 `#SPD,60;#GET,STATE;`。

陶晶驰固定命令示例：

```text
prints "#START,100,1;",0
```

## 2. 自动流程命令

### 2.1 启动自动流程

```text
#START,<volume>,<keep10>;
```

参数：

- `volume`：目标总量，支持 `50/100/150/200/300/400/500/600/700/800`，单位 ml。
- `keep10`：是否预留 10ml 给科研人员人工清洗接液烧杯，`0` 不预留，`1` 预留。

示例：

```text
#START,100,0;
#START,100,1;
#START,800,1;
```

`keep10=1` 的当前含义：

- 不再移动到“10ml 光电位”，因为该 PG 未知且新需求不是这个动作。
- 机器自动处理的目标体积为 `volume - 10ml`。
- 例如 `#START,100,1;` 表示机器自动完成约 90ml 的吸取和喷淋流程。
- 因没有 90ml 独立 PG，固件先按 100ml 档位定位，再执行一次 `TRIM10_MS` 定时补吸作为 10ml 占位实现。
- 自动喷淋结束并回原点后，屏幕应提示科研人员用预留 10ml 人工清洗接液烧杯并补加，完成后点击确认按钮发送 `#OK;`。

### 2.2 人工补加确认

```text
#OK;
```

只在 MCU 状态为 `WAIT_MANUAL_CUP_CLEAN` 时有效。屏幕应在人工清洗/补加提示页的确认按钮中发送：

```text
prints "#OK;",0
```

## 3. 工艺参数命令

### 3.1 设置蠕动泵速度

```text
#SPD,<percent>;
```

参数：

- `percent`：泵速度百分比，范围 `10~100`。
- 小于 10 会按 10 处理，大于 100 会按 100 处理。
- 速度不保存，MCU 复位后恢复 `APP_DEFAULT_PUMP_SPEED_PERCENT`。

示例：

```text
#SPD,60;
#SPD,100;
```

### 3.2 设置阶段时间

```text
#SET,<param>,<time_ms>;
```

参数：

- `param`：时间参数名。
- `time_ms`：毫秒，固件会夹紧到 `100~600000`。
- 设置值不保存，断电或复位后恢复 `My/app_config.h` 中默认值。
- 自动流程运行中发送 `#SET` 会被拒绝并返回 BUSY 状态。

支持的参数：

| 参数名 | 含义 | 默认宏 |
|---|---|---|
| `ASP_MS` 或 `ASPIRATE_MS` | 每个吸取档位的固定吸取时间 | `APP_ASPIRATE_PHASE_MS` |
| `TRIM10_MS` 或 `TRIM_MS` | 预留 10ml 时的定时补吸时间 | `APP_TRIM_10ML_MS` |

示例：

```text
#SET,ASP_MS,5000;
#SET,TRIM10_MS,1000;
```

## 4. 停止、回原点和复位

```text
#STOP;
#ESTOP;
#HOME;
#RESET;
```

含义：

- `STOP`：停止当前动作。若自动流程正在运行，停止泵和 Z 轴后回原点；人工补加等待中发送则取消并回到空闲。
- 上电复位状态下发送 `STOP` 不取消复位，Z 轴会继续回最高点。
- `ESTOP`：立即停止全部泵和 Z 轴，进入急停状态，不自动回原点。
- `HOME`：Z 轴向原点运动，默认原点为 PG3。
- `RESET`：触发 `HAL_NVIC_SystemReset()`。

急停后建议先发送：

```text
#HOME;
```

确认机构回原点后再允许重新启动。

## 5. 手动控制命令

### 5.1 手动 Z 轴

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

### 5.2 手动蠕动泵

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

## 6. 查询命令

```text
#GET,PG;
#GET,STATE;
```

当前实现中，查询命令会触发 MCU 立即刷新屏幕状态控件。

## 7. MCU 到屏幕状态刷新

MCU 向陶晶驰屏幕发送原生命令，结尾固定追加：

```text
FF FF FF
```

默认控件名在 `My/app_config.h` 中定义：

| 控件宏 | 默认控件 | 含义 |
|---|---|---|
| `APP_SCREEN_MESSAGE_OBJ` | `t6` | 状态文本 |
| `APP_SCREEN_STATE_OBJ` | `n_state` | 状态码 |
| `APP_SCREEN_PHASE_OBJ` | `n_phase` | 当前阶段，空闲时为 0 |
| `APP_SCREEN_SPEED_OBJ` | `n_speed` | 当前泵速度百分比 |
| `APP_SCREEN_PGMASK_OBJ` | `n_pgmask` | 16 路 PG 有效位掩码 |
| `APP_SCREEN_KEEP10_OBJ` | `n_keep10` | 是否预留 10ml |
| `APP_SCREEN_ALARM_OBJ` | `n_alarm` | 报警码 |
| `APP_SCREEN_PROGRESS_OBJ` | `j_progress` | 当前定时阶段进度 |

PG 掩码规则：

- bit0 对应 PG1。
- bit1 对应 PG2。
- 以此类推。
- PG 低电平有效，掩码中 `1` 表示该 PG 当前有效。

示例：

```text
n_speed.val=60 FF FF FF
n_pgmask.val=3 FF FF FF
t6.txt="READY" FF FF FF
```

## 8. 状态码

状态码对应 `App_State`：

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

建议 HMI 用 `n_state.val` 做中文映射，而不是依赖 `t6` 的英文短文本。

## 9. 报警码

报警码对应 `App_Alarm`：

| 报警码 | 名称 | 含义 |
|---:|---|---|
| 0 | `NONE` | 无报警 |
| 1 | `BUSY` | 当前忙，拒绝命令 |
| 2 | `BAD_VOLUME` | 体积参数错误 |
| 3 | `Y_NOT_READY` | Y 轴不在允许位置 |
| 4 | `Z_TIMEOUT` | Z 轴到位超时 |
| 5 | `BAD_COMMAND` | 命令无效 |
| 6 | `BAD_CONFIG` | 固件体积或喷淋配置错误 |

## 10. 当前硬件映射

硬件映射集中在 `My/app_config.c`：

| 功能 | 当前映射 |
|---|---|
| Z 轴 | DRV8870 第 1 路，`MOTOR_1` |
| 蠕动泵 1~6 | DRV8870 第 2~7 路，`MOTOR_2` 到 `MOTOR_7` |
| 备用电机 | DRV8870 第 8 路，`MOTOR_8` |
| Y 轴允许工作位置 | 默认 PG1 |
| Z 轴原点/定点 | PG3 |
| 上电复位目标 | PG3 |
| Z 轴底部 | PG14 |
| PG 有效电平 | 低电平有效 |

## 11. 当前体积档位和喷淋规则

体积档位集中在 `My/app_config.c` 的 `APP_VOLUME_POSITIONS`，当前默认：

| 体积 | 吸取 PG | 第一次喷淋 PG | 说明 |
|---:|---|---|---|
| 800ml | PG4 | PG5 | 停一段时间 |
| 700ml | PG5 | PG6 | 停一段时间 |
| 600ml | PG6 | PG7 | 停一段时间 |
| 500ml | PG7 | PG8 | 停一段时间 |
| 400ml | PG8 | PG9 | 停一段时间 |
| 300ml | PG9 | PG10 | 停一段时间 |
| 200ml | PG10 | PG11 | 含定位吸取 |
| 150ml | PG11 | PG12 | 含定位吸取 |
| 100ml | PG12 | PG13 | 含定位吸取 |
| 50ml | PG13 | PG14 | 定位吸取 |

自动吸取：

- 从 800ml 档开始，按表格顺序逐档移动和吸取。
- 到达目标档位后停止继续向下。
- 每个档位使用同一个吸取时间 `ASP_MS` 和同一个全局泵速。

自动喷淋：

- 第一次：使用目标档位表中的 `first_spray_pg`，表示“吸取位置下一档位置喷淋”。
- 第二次：固定 300ml 位置喷淋。
- 第三次：固定 800ml 位置喷淋。
- 三段喷淋共用同一套 6 泵补偿时间，补偿时间在 `My/app_config.c` 的 `APP_SPRAY_PUMP_MS` 中内置。
- 每段喷淋开始时 6 个泵同时启动，每个泵按自己的补偿时间停止；6 个泵全部停止后才进入下一段喷淋。

## 12. HMI 页面事件建议

### 12.1 主页面或配方选择页面

建议放置：

- 体积选择控件：固定按钮或下拉，值为 `50/100/150/200/300/400/500/600/700/800`。
- 预留 10ml 开关：双态按钮或复选框，值为 `0/1`。
- 启动按钮：发送 `#START,<volume>,<keep10>;`。
- 停止按钮：发送 `#STOP;`。
- 急停按钮：发送 `#ESTOP;`，建议所有页面都放一个。
- 回原点按钮：发送 `#HOME;`。

固定体积按钮可以直接写：

```text
prints "#START,100,0;",0
```

如果用数值控件拼接动态体积，建议准备一个隐藏文本控件 `t_tmp`，用 `cov` 把数值转成文本后分段发送：

```text
cov n_volume.val,t_tmp.txt,0
prints "#START,",0
prints t_tmp.txt,0
prints ",",0
cov bt_keep10.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

### 12.2 速度设置页面

建议放置滑条或数字输入 `n_speed_set`，范围限制为 `10~100`。确认按钮事件：

```text
cov n_speed_set.val,t_tmp.txt,0
prints "#SPD,",0
prints t_tmp.txt,0
prints ";",0
```

### 12.3 时间设置页面

建议放置两个数字输入，单位毫秒：

- `n_asp_ms`
- `n_trim10_ms`

每个确认按钮分别发送：

```text
cov n_asp_ms.val,t_tmp.txt,0
prints "#SET,ASP_MS,",0
prints t_tmp.txt,0
prints ";",0
```

```text
cov n_trim10_ms.val,t_tmp.txt,0
prints "#SET,TRIM10_MS,",0
prints t_tmp.txt,0
prints ";",0
```

喷淋补偿时间不由 HMI 设置。泵 1~6 的喷淋补偿时间需要在固件 `APP_SPRAY_PUMP_MS` 表中修改。

### 12.4 手动调试页面

建议按钮按“按下开始、松开停止”写事件。

Z 轴上升按钮：

```text
// 按下事件
prints "#MAN,Z,UP,50;",0

// 松开事件
prints "#MAN,Z,STOP;",0
```

Z 轴下降按钮：

```text
// 按下事件
prints "#MAN,Z,DOWN,50;",0

// 松开事件
prints "#MAN,Z,STOP;",0
```

泵吸取按钮：

```text
// 按下事件
prints "#MAN,PUMP,IN,60;",0

// 松开事件
prints "#MAN,PUMP,STOP;",0
```

泵排出/喷淋按钮：

```text
// 按下事件
prints "#MAN,PUMP,OUT,60;",0

// 松开事件
prints "#MAN,PUMP,STOP;",0
```

### 12.5 人工补加确认页面

当 `n_state.val==9` 时，HMI 应进入或弹出人工提示页面。页面文字建议由屏幕资源显示：

```text
请使用预留 10ml 人工清洗接液烧杯并补加，完成后点击确认
```

确认按钮事件：

```text
prints "#OK;",0
```

### 12.6 状态刷新和报警显示

建议 HMI 周期性或在状态控件变化时读取这些 MCU 写入的控件：

- `n_state.val`：切换页面、显示中文状态。
- `n_alarm.val`：非 0 时显示报警。
- `j_progress.val`：显示当前吸取、补吸或喷淋阶段进度。
- `n_pgmask.val`：调试页面显示 16 路 PG 状态。

如需主动刷新，可以在页面打开事件或调试按钮中发送：

```text
prints "#GET,STATE;",0
prints "#GET,PG;",0
```
