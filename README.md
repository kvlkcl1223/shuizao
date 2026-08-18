# 水藻设备当前使用说明

当前请使用以下两个文件/工程：

- STM32 固件工程：`shuizao-v3`
- 陶晶驰串口屏工程：`大型水藻v2.HMI`

## 1. STM32 固件

固件请打开并编译：

```text
shuizao-v3/MDK-ARM/shuizao.uvprojx
```

不要再使用 `shuizao_v1` 或 `shuizao-v2` 作为当前版本固件。

`shuizao-v3` 是当前维护版本，已经包含：

- 4 个 Z 轴光电位置：PG3 上限位、PG4 100ml、PG5 50ml、PG6 下限位。
- 其余体积位置通过定时步进实现。
- 上电自动复位：先下行一段时间，再上行寻找 PG3。
- Z 轴正反转切换保护：反向前会先空档停顿，避免驱动芯片直接反转。
- USART3 与陶晶驰串口屏通信。
- USART2 输出 ASCII 调试日志。
- LED1 生命灯。

详细流程、参数和调试位置见：

```text
shuizao-v3/README.md
shuizao-v3/docs/serial_protocol.md
```

## 2. 陶晶驰串口屏

串口屏请使用：

```text
大型水藻v2.HMI
```

不要使用：

```text
大型水藻10.28.HMI
大型水藻触摸屏.HMI
```

当前 MCU 固件默认通过 USART3 接收串口屏命令，命令格式请参考：

```text
shuizao-v3/docs/serial_protocol.md
```

## 3. 调试时优先修改的位置

固件主要参数集中在：

```text
shuizao-v3/My/app_config.h
shuizao-v3/My/app_config.c
```

常见需要真机调整的内容：

- Z 轴相邻步进时间：`APP_Z_STEP_DOWN_MS`、`APP_Z_STEP_UP_MS`
- 上电复位下行时间：`APP_POWER_ON_RESET_DOWN_MS`
- Z 轴反向停顿时间：`APP_Z_REVERSE_DEADTIME_MS`
- 吸取时间：`APP_ASPIRATE_PHASE_MS`
- 预留 10ml 的补吸时间：`APP_TRIM_10ML_MS`
- 6 个蠕动泵喷淋补偿时间：`APP_SPRAY_PUMP_MS`
- 默认泵速：`APP_DEFAULT_PUMP_SPEED_PERCENT`

## 4. 当前版本约定

- PG 信号低电平有效。
- PG1 默认作为 Y 轴允许工作位置。
- PG3 默认作为 Z 轴最高点/原点。
- 自动流程结束后会回 PG3。
- 如果选择预留 10ml，机器目标体积会少做 10ml，剩余 10ml 由科研人员人工清洗接液烧杯后补加，并在屏幕点击确认。
