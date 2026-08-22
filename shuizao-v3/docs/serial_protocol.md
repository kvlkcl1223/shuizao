# 陶晶驰串口屏与 STM32 通信协议

本文档对应 `shuizao-v3` 当前固件。串口屏连接 `USART3`，调试日志连接 `USART2`。

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无
- MCU 接收方式：USART3 DMA + IDLE 中断
- 字符编码：屏幕发给 MCU 的命令使用 ASCII
- MCU 上电或收到 `#RESET;` 软件复位后，会先短时下行，再上行复位到最高点；当前最高点为 PG3。

USART2 调试日志：

- 波特率：115200
- 数据位：8
- 停止位：1
- 校验位：无
- 方向：MCU 输出日志为主，不作为 HMI 命令输入口
- 日志内容：上电初始化、TIM5 生命灯启动、已解析命令、状态切换、报警、自动流程关键阶段
- 日志正文保持 ASCII，不输出中文，避免不支持 UTF-8 的串口助手显示乱码

日志基本格式：

```text
[0000012345ms][TAG] key=value key=value ...
```

常见 `TAG`：

| TAG | 含义 |
|---|---|
| `BOOT` | 上电初始化、日志串口、生命灯、上电复位 |
| `CMD` | 已经被 MCU 正确解析的屏幕命令 |
| `STATE` | 主状态机状态切换 |
| `MOVE` | Z 轴移动目标、方向、当前位置判断 |
| `AUTO` | 自动流程计划、Y 轴检查、人工补加等待 |
| `ASP` | 分阶段吸取 |
| `TRIM10` | 预留 10ml 时的定时补吸 |
| `SPRAY` | 三段喷淋和单泵补偿停止 |
| `HOME` | 回原点动作 |
| `ALARM` | 报警和故障上下文 |

示例：

```text
[0000000012ms][BOOT] logger_ready uart=USART2 baud=115200 format=ASCII
[0000000015ms][BOOT] power_reset_down_ms=1000
[0000001016ms][BOOT] power_reset_down_stop reason=TIME_DONE
[0000001017ms][BOOT] power_reset_up_seek_home
[0000005000ms][CMD] rx=START volume_ml=100 keep10=1 pgmask=0x0004
[0000005005ms][AUTO] plan volume_ml=100 machine_ml=90 reserved_ml=10 asp_count=3 spray_count=3 trim10=1 pump_speed=60%
[0000005010ms][STATE] code=1 name=HOMING pgmask=0x0004
[0000005012ms][MOVE] step_target=HOME step_index=0 final_target=HOME final_index=0 current_index=-1 mode=SENSOR sensor_pg=PG3 dir=FORWARD motor_speed=700 limit_ms=45000 pgmask=0x0000
[0000006500ms][ASP] start stage=1/3 target_pos=200ml target_index=3 sensor_pg=PG4 duration_ms=5000 pump_speed=60% pgmask=0x0008
[0000011501ms][ASP] done stage=1 elapsed_ms=5001 pgmask=0x0008
[0000015000ms][SPRAY] pump_stop stage=1 pump=3 duration_ms=5000 elapsed_ms=5000 active_mask=0x37 pgmask=0x0010
[0000045001ms][ALARM] code=4 name=Z_TIMEOUT state=14/POWER_ON_RESET target_pos=HOME target_index=0 sensor_pg=PG3 pgmask=0x0000 elapsed_ms=45001 timeout_ms=45000
```

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

- `volume`：目标总量，只支持 `50/100/150/200`，单位 ml。
- `keep10`：是否预留 10ml 给科研人员人工清洗接液烧杯，`0` 不预留，`1` 预留。

示例：

```text
#START,100,0;
#START,100,1;
#START,200,1;
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

### 3.1 设置吸取速度

```text
#SPD,<percent>;
```

参数：

- `percent`：吸取速度百分比，范围 `10~100`。
- 小于 10 会按 10 处理，大于 100 会按 100 处理。
- `#SPD` 先写入 RAM，发送 `#SAVE,SPD;` 或 `#SAVE,ALL;` 后保存到 Flash。
- MCU 复位后优先读取 Flash 保存速度；Flash 无效时恢复 `APP_DEFAULT_PUMP_SPEED_PERCENT`。
- 该命令只影响自动吸取和预留 10ml 补吸，不影响自动喷淋。
- 自动喷淋速度使用代码内置 `APP_SPRAY_SPEED_PERCENT`，需要改喷淋速度时修改固件宏并重新烧录。

示例：

```text
#SPD,60;
#SPD,100;
```

### 3.2 设置阶段时间

```text
#SET,<param>,<time_ms>;
#SET,SPRAY1_MS,<time_ms>,<volume>;
```

参数：

- `param`：时间参数名。
- `time_ms`：毫秒，固件会夹紧到 `0~6000`。
- `volume`：喷淋时间所属体积档位，只能是 `200/150/100/50`。
- `ASP_MS` 和 `TRIM10_MS` 不保存，断电或复位后恢复 `My/app_config.h` 中默认值。新的分阶段吸取停留时间使用 `APP_ASP_DWELL_800_MS` 到 `APP_ASP_DWELL_50_MS`，暂不由 HMI 修改。
- `SPRAY1_MS` 到 `SPRAY6_MS` 必须携带体积档位，写入该档位对应的第一段喷淋 RAM 表；发送 `#SAVE,SPRAY_MS;` 后把 4 个档位的第一段喷淋时间写入 Flash。
- `Z_DN_HOME_800_MS` 等 Z 虚拟位置时间会夹紧到 `1000~20000ms`，默认 `3000ms`。
- 自动流程运行中发送 `#SET` 会被拒绝并返回 BUSY 状态。

支持的参数：

| 参数名 | 含义 | 默认宏 |
|---|---|---|
| `ASP_MS` 或 `ASPIRATE_MS` | 旧版吸取固定时间，当前仅保留协议兼容 | `APP_ASPIRATE_PHASE_MS` |
| `TRIM10_MS` 或 `TRIM_MS` | 预留 10ml 时的定时补吸时间 | `APP_TRIM_10ML_MS` |
| `SPRAY1_MS` | 当前档位第一段喷淋的泵 1 补偿时间 | `APP_SPRAY_PUMP1_MS` 或 Flash 保存值 |
| `SPRAY2_MS` | 当前档位第一段喷淋的泵 2 补偿时间 | `APP_SPRAY_PUMP2_MS` 或 Flash 保存值 |
| `SPRAY3_MS` | 当前档位第一段喷淋的泵 3 补偿时间 | `APP_SPRAY_PUMP3_MS` 或 Flash 保存值 |
| `SPRAY4_MS` | 当前档位第一段喷淋的泵 4 补偿时间 | `APP_SPRAY_PUMP4_MS` 或 Flash 保存值 |
| `SPRAY5_MS` | 当前档位第一段喷淋的泵 5 补偿时间 | `APP_SPRAY_PUMP5_MS` 或 Flash 保存值 |
| `SPRAY6_MS` | 当前档位第一段喷淋的泵 6 补偿时间 | `APP_SPRAY_PUMP6_MS` 或 Flash 保存值 |
| `Z_DN_HOME_800_MS` | HOME/PG3 下行到 800ml 虚拟位 | `APP_ZVIRT_TIME_DEFAULT_MS` 或 Flash 保存值 |
| `Z_DN_800_300_MS` | 800ml 虚拟位下行到 300ml 虚拟位 | `APP_ZVIRT_TIME_DEFAULT_MS` 或 Flash 保存值 |
| `Z_UP_300_800_MS` | 300ml 虚拟位上行到 800ml 虚拟位 | `APP_ZVIRT_TIME_DEFAULT_MS` 或 Flash 保存值 |
| `Z_UP_200_300_MS` | 200ml/PG4 上行到 300ml 虚拟位 | `APP_ZVIRT_TIME_DEFAULT_MS` 或 Flash 保存值 |

示例：

```text
#SET,ASP_MS,5000;
#SET,TRIM10_MS,1000;
#SET,SPRAY1_MS,3500,100;
#SET,Z_DN_HOME_800_MS,3000;
```

### 3.3 读取吸取泵速

```text
#GET,SPD;
```

含义：

- MCU 同时回填 `n_speed.val` 和 `h_speed.val`。
- 该值是当前自动吸取、预留 10ml 补吸、以及手动泵命令速度为 `0` 时使用的泵速。

### 3.4 读取指定喷淋档位

```text
#GET,SPRAY_MS,<volume>;
```

参数：

- `volume`：只能是 `200/150/100/50`。
- `#GET,SPRAY_MS,50;`：读取 50ml 档位的 6 个喷淋时间，并由 MCU 回填滑轴、数值和档位显示。

注意：

- MCU 不保存“当前正在调节哪个档位”的状态。
- HMI 需要自己用 `n_spray_vol.val` 保存当前选择的档位，并在读取和滑轴设置命令中把该值带给 MCU。
- 自动运行时，固件会按 `#START` 的体积自动选择对应档位的喷淋时间，不依赖 HMI 当前停留在哪个调试档位。
- 每个体积档位有 6 个第一段喷淋时间；第二段 300ml 和第三段 800ml 使用程序固定表，不受屏幕调节影响。
- `#SAVE,SPRAY_MS;` 保存全部 4 个档位共 24 个第一段喷淋时间。

## 4. 停止、回原点和复位

```text
#STOP;
#ESTOP;
#HOME;
#RESET;
```

含义：

- `STOP`：停止当前动作。若自动流程正在运行，停止泵和 Z 轴后回原点；人工补加等待中发送则取消并回到空闲。
- 上电复位状态下发送 `STOP` 不取消复位；若正在下行，会停止下行并继续上行寻找 PG3。
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
- 下降到底部 PG7 后自动停止，PG7 同时是 50ml 位置。
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
#GET,SPRAY_MS,<volume>;
#GET,ZVIRT_MS;
#GET,SPD;
```

当前实现中，查询命令会触发 MCU 立即刷新屏幕控件。

- `#GET,PG;`：刷新 PG 掩码。
- `#GET,STATE;`：刷新状态、报警、速度、阶段和进度。
- `#GET,SPRAY_MS,<volume>;`：刷新指定体积档位、6 个喷淋时间滑轴和 6 个喷淋时间数值控件。
- `#GET,ZVIRT_MS;`：刷新 4 个 Z 轴虚拟位置时间滑轴和数值控件。
- `#GET,SPD;` 或 `#GET,SPEED;`：刷新当前吸取泵速 `n_speed.val` 和 `h_speed.val`。

## 7. 保存命令

```text
#SAVE,SPRAY_MS;
#SAVE,ZVIRT_MS;
#SAVE,SPD;
#SAVE,ALL;
```

含义：

- `#SAVE,SPRAY_MS;`、`#SAVE,ZVIRT_MS;`、`#SAVE,SPD;`、`#SAVE,ALL;` 当前都会整包保存全部可保存参数。
- 保存内容包括 4 个体积档位共 24 个第一段喷淋时间、4 个 Z 轴虚拟位置时间，以及当前吸取泵速。
- 这样做是为了避免 Flash 中喷淋参数和 Z 虚拟位置参数互相覆盖。
- MCU 初始化时优先从 Flash 读取；Flash 记录无效时使用 `app_config.h` 默认值。
- 自动流程运行中发送保存命令会被拒绝并显示 `BUSY`。
- 保存成功显示 `SAVE OK`，保存失败显示 `SAVE ERR` 并报 `SAVE_FAILED`。

## 8. MCU 到屏幕状态刷新

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
| `APP_SCREEN_SPEED_OBJ` | `n_speed` | 当前吸取速度百分比 |
| `APP_SCREEN_SPEED_SLIDER_OBJ` | `h_speed` | 当前吸取速度滑轴 |
| `APP_SCREEN_PGMASK_OBJ` | `n_pgmask` | 16 路 PG 有效位掩码 |
| `APP_SCREEN_KEEP10_OBJ` | `n_keep10` | 是否预留 10ml |
| `APP_SCREEN_ALARM_OBJ` | `n_alarm` | 报警码 |
| `APP_SCREEN_PROGRESS_OBJ` | `j_progress` | 当前定时阶段进度 |
| `APP_SCREEN_WARNING_PAGE` | `warn` | Y 轴异常时跳转的警告页面 |
| `APP_SCREEN_SPRAY1_SLIDER_OBJ` | `h_spray1_ms` | 泵 1 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY2_SLIDER_OBJ` | `h_spray2_ms` | 泵 2 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY3_SLIDER_OBJ` | `h_spray3_ms` | 泵 3 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY4_SLIDER_OBJ` | `h_spray4_ms` | 泵 4 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY5_SLIDER_OBJ` | `h_spray5_ms` | 泵 5 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY6_SLIDER_OBJ` | `h_spray6_ms` | 泵 6 喷淋时间滑轴 |
| `APP_SCREEN_SPRAY1_VALUE_OBJ` | `n_spray1_ms` | 泵 1 喷淋时间数值 |
| `APP_SCREEN_SPRAY2_VALUE_OBJ` | `n_spray2_ms` | 泵 2 喷淋时间数值 |
| `APP_SCREEN_SPRAY3_VALUE_OBJ` | `n_spray3_ms` | 泵 3 喷淋时间数值 |
| `APP_SCREEN_SPRAY4_VALUE_OBJ` | `n_spray4_ms` | 泵 4 喷淋时间数值 |
| `APP_SCREEN_SPRAY5_VALUE_OBJ` | `n_spray5_ms` | 泵 5 喷淋时间数值 |
| `APP_SCREEN_SPRAY6_VALUE_OBJ` | `n_spray6_ms` | 泵 6 喷淋时间数值 |
| `APP_SCREEN_SPRAY_VOLUME_VALUE_OBJ` | `n_spray_vol` | 当前正在调节的喷淋档位数值，200/150/100/50 |
| `APP_SCREEN_SPRAY_VOLUME_TEXT_OBJ` | `t_spray_vol` | 当前正在调节的喷淋档位文本，例如 `100ml` |

当 PG1 在任意 Z 轴动作前或动作中无效时，MCU 会停止执行机构，报警 `Y_NOT_READY`，并发送：

```text
page warn FF FF FF
```

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
h_spray1_ms.val=3500 FF FF FF
n_spray1_ms.val=3500 FF FF FF
n_spray_vol.val=100 FF FF FF
t_spray_vol.txt="100ml" FF FF FF
```

## 9. 状态码

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

## 10. 报警码

报警码对应 `App_Alarm`：

| 报警码 | 名称 | 含义 |
|---:|---|---|
| 0 | `NONE` | 无报警 |
| 1 | `BUSY` | 当前忙，拒绝命令 |
| 2 | `BAD_VOLUME` | 体积参数错误 |
| 3 | `Y_NOT_READY` | Y 轴不在允许位置；触发后 MCU 会跳转到 `warn` 页面 |
| 4 | `Z_TIMEOUT` | Z 轴到位超时 |
| 5 | `BAD_COMMAND` | 命令无效 |
| 6 | `BAD_CONFIG` | 固件体积或喷淋配置错误 |
| 7 | `SAVE_FAILED` | Flash 保存喷淋补偿时间失败 |

## 11. 当前硬件映射

硬件映射集中在 `My/app_config.c`：

| 功能 | 当前映射 |
|---|---|
| Z 轴 | DRV8870 第 1 路，`MOTOR_1` |
| 蠕动泵 1~6 | DRV8870 第 2~7 路，`MOTOR_2` 到 `MOTOR_7` |
| 备用电机 | DRV8870 第 8 路，`MOTOR_8` |
| Y 轴允许工作位置 | 默认 PG1 |
| Z 轴上限/原点 | PG3 |
| Z 轴 200ml 定位 | PG4 |
| Z 轴 150ml 定位 | PG5 |
| Z 轴 100ml 定位 | PG6 |
| Z 轴 50ml 定位/下限/底部 | PG7 |
| 上电复位目标 | PG3 |
| PG 有效电平 | 低电平有效 |

Y 轴保护规则：

- 自动流程开始前会检查 PG1。
- 所有 Z 轴动作启动前都会检查 PG1。
- Z 轴动作过程中会持续检查 PG1，包括上电复位、回原点、移动到吸取位、移动到喷淋位、自动结束回原点和手动 Z 轴上下。
- PG1 无效时立即停止全部执行机构，进入 `ERROR`，报警码为 `Y_NOT_READY`，并跳转到 `warn` 页面。

## 12. 当前体积档位和喷淋规则

体积档位集中在 `My/app_config.c` 的 `APP_VOLUME_POSITIONS`，当前默认：

| 体积 | 吸取逻辑位置 | 第一次喷淋逻辑位置 | 到位方式 |
|---:|---|---|---|
| 200ml | `APP_Z_POS_200ML` | `APP_Z_POS_150ML` | PG4 传感器确认 |
| 150ml | `APP_Z_POS_150ML` | `APP_Z_POS_100ML` | PG5 传感器确认 |
| 100ml | `APP_Z_POS_100ML` | `APP_Z_POS_50ML` | PG6 传感器确认 |
| 50ml | `APP_Z_POS_50ML` | `APP_Z_POS_50ML` | PG7 传感器确认，原地喷 |

Z 轴逻辑顺序：

```text
HOME(PG3) -> 800(虚拟) -> 700(虚拟) -> 600(虚拟) -> 500(虚拟) -> 400(虚拟) -> 300(虚拟) -> 200(PG4) -> 150(PG5) -> 100(PG6) -> 50(PG7/下限)
```

相邻步进时间：

- `APP_Z_STEP_DOWN_MS[i]`：从逻辑位置 `i` 向下到 `i+1`。
- `APP_Z_STEP_UP_MS[i]`：从逻辑位置 `i+1` 向上到 `i`。
- 两个时间表都在 `My/app_config.c`，默认先填 `1000ms`，真机调试时必须实测修改。

上电复位和反向保护：

- `APP_POWER_ON_RESET_DOWN_MS`：上电复位开始时先向下运行的时间，默认 `1000ms`。
- 如果下行过程中触发 PG7，下行立即停止，然后转为上行寻找 PG3。
- `APP_Z_REVERSE_DEADTIME_MS`：Z 轴从上行切到下行、或从下行切到上行前的空档停顿时间，默认 `300ms`。
- 反向保护由 MCU 内部自动执行，HMI 不需要新增命令。

自动吸取：

- 自动模式仍然只支持 `200/150/100/50ml`。
- 每次自动吸取都会先移动到 800ml 虚拟位置；HOME 到 800ml 的移动不开泵，到达 800ml 后开泵吸取。
- 从 800ml 继续向 700/600/500/400/300ml 虚拟位置以及后续真实 PG 档位下行时，泵保持吸取，不在相邻吸取阶段之间停止。
- 然后从 200ml 开始进入真实 PG 定位吸取段。
- 真实段下降找 PG 时泵已经开始吸取；到达 PG 后继续停留吸取该段默认时间。
- 目标为 200ml 时，真实吸取只执行到 PG4/200ml。
- 目标为 150ml 时，真实吸取执行 PG4/200ml 和 PG5/150ml。
- 目标为 100ml 时，真实吸取执行 PG4/200ml、PG5/150ml 和 PG6/100ml。
- 目标为 50ml 时，真实吸取执行 PG4/200ml、PG5/150ml、PG6/100ml 和 PG7/50ml。
- 从 200ml 往后继续下降时泵保持吸取，不在相邻真实档位之间停止。
- 吸取停留时间由 `APP_ASP_DWELL_800_MS` 到 `APP_ASP_DWELL_50_MS` 分别定义，当前默认全部为 `3000ms`。
- `#SPD` 只调吸取泵速，不调喷淋泵速。

自动喷淋：

- 第一次：使用目标档位表中的 `first_spray_pos`，表示“吸取位置下一档位置喷淋”。
- 第二次：固定 300ml 虚拟位置喷淋。
- 第三次：固定 800ml 虚拟位置喷淋。
- 50ml 的第一次喷淋没有下一档，保持在 50ml 原地喷。
- 200/150/100/50 四个自动档位各有一套第一段 6 泵补偿时间。上电时优先使用 Flash 保存值；没有有效保存值时使用 `My/app_config.c` 的 `APP_SPRAY_PUMP_MS` 默认值填充每个档位。
- 喷淋泵速不跟随 `#SPD`，固定使用 `My/app_config.h` 中的 `APP_SPRAY_SPEED_PERCENT`。
- 第一段使用 HMI/Flash 可调时间；第二段 300ml 使用 `APP_SPRAY_STAGE2_PUMP_MS`；第三段 800ml 使用 `APP_SPRAY_STAGE3_PUMP_MS`。
- 第二段和第三段也按 200/150/100/50 档位区分，并且每个泵独立，只能通过修改 `My/app_config.c` 的固定表调整。
- 每段喷淋开始时 6 个泵同时启动，每个泵按自己的补偿时间停止；6 个泵全部停止后才进入下一段喷淋。

## 13. HMI 页面事件建议

### 13.1 主页面或配方选择页面

建议放置：

- 体积选择控件：固定按钮或下拉，值为 `50/100/150/200`。
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

### 13.2 吸取速度设置页面

建议使用滑轴 `h_speed` 和数值控件 `n_speed`，范围都限制为 `10~100`。滑轴弹起事件先同步数值控件，再在屏幕内拼完整字符串，最后一次性发送：

```text
n_speed.val=h_speed.val
cov h_speed.val,t_tmp.txt,0
t_cmd.txt="#SPD,"
t_cmd.txt+=t_tmp.txt
t_cmd.txt+=";"
prints t_cmd.txt,0
```

读取按钮按下或页面打开事件：

```text
prints "#GET,SPD;",0
```

保存按钮按下或弹起事件：

```text
prints "#SAVE,SPD;",0
```

### 13.3 时间设置页面

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

第一段喷淋补偿时间由 HMI 设置页面调整。你已定义滑轴 `h_spray1_ms` 到 `h_spray6_ms`，以及数值 `n_spray1_ms` 到 `n_spray6_ms`。

还需要新增两个显示控件：

| 控件名 | 类型建议 | 作用 |
|---|---|---|
| `n_spray_vol` | 数值 | 当前正在调节的体积档位，值为 200/150/100/50 |
| `t_spray_vol` | 文本 | 当前正在调节的体积档位，例如 `100ml` |

页面打开时建议先设置默认档位为 200ml，再读取 MCU 当前值：

```text
n_spray_vol.val=200
t_spray_vol.txt="200ml"
cov n_spray_vol.val,t_tmp.txt,0
prints "#GET,SPRAY_MS,",0
prints t_tmp.txt,0
prints ";",0
```

新增一个“切换喷淋档位”按钮。这个按钮只修改 HMI 本地变量和显示，不向 MCU 发送命令：

```text
if(n_spray_vol.val==200)
{
  n_spray_vol.val=150
  t_spray_vol.txt="150ml"
}else if(n_spray_vol.val==150)
{
  n_spray_vol.val=100
  t_spray_vol.txt="100ml"
}else if(n_spray_vol.val==100)
{
  n_spray_vol.val=50
  t_spray_vol.txt="50ml"
}else
{
  n_spray_vol.val=200
  t_spray_vol.txt="200ml"
}
```

每按一次，HMI 本地按下面顺序切换：

```text
200 -> 150 -> 100 -> 50 -> 200
```

切换后如果需要立刻读取该档位的保存值，再额外放一个“读取当前档位”按钮，或在切换按钮最后追加读取命令：

```text
cov n_spray_vol.val,t_tmp.txt,0
prints "#GET,SPRAY_MS,",0
prints t_tmp.txt,0
prints ";",0
```

泵 1 滑轴弹起事件：

```text
n_spray1_ms.val=h_spray1_ms.val
prints "#SET,SPRAY1_MS,",0
cov h_spray1_ms.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ",",0
cov n_spray_vol.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

泵 2 滑轴弹起事件：

```text
n_spray2_ms.val=h_spray2_ms.val
prints "#SET,SPRAY2_MS,",0
cov h_spray2_ms.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ",",0
cov n_spray_vol.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

泵 3 到泵 6 依次把编号改成 `3~6`。

保存按钮按下或弹起事件：

```text
prints "#SAVE,SPRAY_MS;",0
```

注意：

- 滑轴发送的 `SPRAY1_MS` 到 `SPRAY6_MS` 只会修改当前 `n_spray_vol` 对应档位的第一段喷淋时间。
- 自动运行 `#START,100,0;` 时，MCU 第一段使用 100ml 档位的 6 个可调喷淋时间，第二段和第三段使用程序固定表。
- 自动运行 `#START,50,0;` 时，MCU 第一段使用 50ml 档位的 6 个可调喷淋时间，第二段和第三段使用程序固定表。
- HMI 当前调试页面选中哪个档位，不会影响正在自动运行的任务；自动任务只看 `#START` 的体积。

### 13.4 Z 虚拟位置时间调试页面

新增 4 个滑轴和 4 个数值控件：

| 滑轴控件 | 数值控件 | 含义 |
|---|---|---|
| `h_zd_h8` | `n_zd_h8` | HOME/PG3 下行到 800ml 虚拟位 |
| `h_zd_83` | `n_zd_83` | 800ml 虚拟位下行到 300ml 虚拟位 |
| `h_zu_38` | `n_zu_38` | 300ml 虚拟位上行到 800ml 虚拟位 |
| `h_zu_23` | `n_zu_23` | 200ml/PG4 上行到 300ml 虚拟位 |

滑轴范围建议全部设置为：

```text
minval=1000
maxval=20000
```

页面打开或读取按钮事件：

```text
prints "#GET,ZVIRT_MS;",0
```

命名说明：

- `zd` 表示 Z down，下行。
- `zu` 表示 Z up，上行。
- `h8` 表示 HOME 到 800ml。
- `83` 表示 800ml 到 300ml。
- `38` 表示 300ml 到 800ml。
- `23` 表示 200ml 到 300ml。

`h_zd_h8` 弹起事件：

```text
n_zd_h8.val=h_zd_h8.val
prints "#SET,Z_DN_HOME_800_MS,",0
cov h_zd_h8.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

`h_zd_83` 弹起事件：

```text
n_zd_83.val=h_zd_83.val
prints "#SET,Z_DN_800_300_MS,",0
cov h_zd_83.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

`h_zu_38` 弹起事件：

```text
n_zu_38.val=h_zu_38.val
prints "#SET,Z_UP_300_800_MS,",0
cov h_zu_38.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

`h_zu_23` 弹起事件：

```text
n_zu_23.val=h_zu_23.val
prints "#SET,Z_UP_200_300_MS,",0
cov h_zu_23.val,t_tmp.txt,0
prints t_tmp.txt,0
prints ";",0
```

保存按钮事件：

```text
prints "#SAVE,ZVIRT_MS;",0
```

保存全部按钮也可以写：

```text
prints "#SAVE,ALL;",0
```

注意：这 4 个时间只用于没有独立 PG 的虚拟位置定位。HOME、200ml、150ml、100ml、50ml 都有真实 PG，最终停止仍以 PG 为准。

### 13.5 手动调试页面

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

### 13.6 人工补加确认页面

当 `n_state.val==9` 时，HMI 应进入或弹出人工提示页面。页面文字建议由屏幕资源显示：

```text
请使用预留 10ml 人工清洗接液烧杯并补加，完成后点击确认
```

确认按钮事件：

```text
prints "#OK;",0
```

### 13.7 状态刷新和报警显示

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
