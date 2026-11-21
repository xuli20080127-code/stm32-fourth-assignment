# 电控组第四次培训作业

## 基本信息

- 姓名：许锂
- 学号：20263008243
- 学院：信息与通信工程学院
- GitHub 账号：xuli20080127-code
- 项目仓库：https://github.com/xuli20080127-code/RM-2027------
- 开发板：STM32F103C8T6
- 开发环境：STM32CubeMX、CLion 2026.2.2、ARM GCC 10.3-2021.10、CMake、Ninja
- 下载器：ST-Link V2
- 串口：CH340，USART1，`115200 8N1`

## 项目功能

本项目使用 PWM 驱动 180° SG90 舵机，并完成以下功能：

- 上电后按 `0° → 45° → 90° → 135° → 180°` 依次定位，每个角度保持 1 秒；
- OLED 同时显示目标角度、比较寄存器 CCR 和实际脉宽；
- USART1 同步输出与 OLED 相同的数据；
- PA11 按键每次增加 15°，PA12 按键每次减少 15°，范围限制为 0°~180°；
- 按键使用 20 ms 消抖，按下一次只改变一次角度；
- 序列和按键处理均不使用长时间阻塞延时，按键可以中断自动序列并进入手动控制。

安全脉宽范围取 `550~2450 µs`。作业前文有一处写“两端各留 2°”，后文又明确建议使用 `550~2450 µs`；本项目采用后文的安全范围，避免 SG90 在机械限位处堵转发热。

## 硬件连接

本机实际测试使用以下连接。它与作业示例中的 OLED `PB8/PB9`、按键 `PA1/PA2` 不同，但功能等价，且当前工程不存在引脚冲突。

| 功能 | 单片机引脚 | 连接说明 |
|---|---|---|
| 舵机信号 | PA0 | TIM2_CH1，50 Hz PWM |
| 舵机电源 | 独立 5 V | 红线接独立 5 V/≥1 A 电源 |
| 舵机地 | 独立电源 GND | 与 STM32 GND 共地 |
| OLED SCL | PB1 | 软件 I²C，开漏输出 |
| OLED SDA | PB0 | 软件 I²C，开漏输出 |
| OLED VCC | 3.3 V | 由 STM32 板 3.3 V 供电 |
| OLED GND | GND | 与单片机共地 |
| +15° 按键 | PA11 | 另一端接 GND，内部上拉，下降沿中断 |
| -15° 按键 | PA12 | 另一端接 GND，内部上拉，下降沿中断 |
| CH340 TX | PA10 | STM32 USART1_RX |
| CH340 RX | PA9 | STM32 USART1_TX |
| CH340 GND | GND | 与单片机共地 |
| ST-Link SWDIO | PA13 | Serial Wire 调试 |
| ST-Link SWCLK | PA14 | Serial Wire 调试 |

如果改回作业示例接线，只需将 `Core/Src/OLED.c` 的 SCL/SDA 改为 PB8/PB9，并把 `Core/Inc/main.h`、`Core/Src/gpio.c` 的按键宏和初始化改为 PA1/PA2。

## PSC 与 ARR 推导

系统使用外部 8 MHz 晶振，经 PLL 九倍频得到 72 MHz：

```text
8 MHz × 9 = 72 MHz
```

APB1 分频系数为 2，因此 APB1 外设时钟为 36 MHz。STM32F103 在 APB1 分频系数不为 1 时，会把送给 TIM2 的时钟自动乘 2：

```text
TIM2 时钟 = 36 MHz × 2 = 72 MHz
```

为了直接使用 CCR 表示微秒，先让一个计数周期等于 1 µs：

```text
72 MHz ÷ (PSC + 1) = 1 MHz
PSC + 1 = 72
PSC = 71
```

SG90 要求 50 Hz，因此周期为：

```text
1 / 50 Hz = 20 ms = 20000 µs
1 MHz ÷ 50 Hz = 20000
ARR + 1 = 20000
ARR = 19999
```

最终验证：

```text
72 MHz ÷ (71 + 1) ÷ (19999 + 1) = 50 Hz
```

## 角度与 CCR 换算

一个计数周期为 1 µs，所以设置给 TIM2_CH1 的 CCR 数值等于脉宽微秒数。安全范围中：

```text
550 µs → 0°
1500 µs → 90°
2450 µs → 180°
```

换算公式为：

```text
Pulse(µs) = 550 + angle / 180 × (2450 - 550)
CCR = round(Pulse)
```

代码使用整数四舍五入，避免浮点库带来的额外空间和运行开销：

```c
return (uint16_t)(550U +
                  ((uint32_t)angle * 1900U + 90U) / 180U);
```

计算得到：

| 角度 | CCR / 脉宽 |
|---:|---:|
| 0° | 550 / 0.550 ms |
| 45° | 1025 / 1.025 ms |
| 90° | 1500 / 1.500 ms |
| 135° | 1975 / 1.975 ms |
| 180° | 2450 / 2.450 ms |

## OLED 显示

OLED 使用电控组提供的 4 针软件 I²C 驱动，从机地址为 `0x78`，显示三行固定宽度信息：

```text
Target:090deg
CCR   :1500
Pulse :1.500ms
```

屏幕只在角度变化时刷新。刷新函数位于 `Core/Src/main.c` 的 `OLED_ShowStatus()`。

软件 I²C 使用 1 µs 边沿延时，SCL 频率约为 250 kHz，低于 SSD1306 的
400 kHz 上限。每次发送 8 个数据位后会释放 SDA 并补足 ACK 时钟；初始化
时还会产生 9 个恢复时钟，防止上一次中断留下的总线状态导致偶发乱码。

## 串口输出

USART1 参数为 `115200 8N1`，每次角度变化输出：

```text
Target=090  CCR=1500  Pulse=1.500ms
```

串口采用阻塞发送。每次只发送约 40 字节，在 115200 波特率下耗时远低于 5 ms，并且一秒才更新一次，因此不会明显影响按键和序列运行。

实机串口测试记录保存在 [docs/serial-output.txt](docs/serial-output.txt)，
采集结果图片位于 [docs/serial-output.png](docs/serial-output.png)。

## 运行流程

1. 初始化 72 MHz 时钟、GPIO、TIM2 和 USART1；
2. 校验 TIM2 输入时钟必须为 72 MHz；
3. 启动 TIM2_CH1 PWM；
4. 初始化 OLED，并输出第一个角度 0°；
5. 主循环每轮处理按键事件和自动序列；
6. 每 1 秒进入序列的下一个位置；
7. 任一按键在消抖后立即结束自动序列，改为手动 ±15° 控制；
8. PC13 板载 LED 每 500 ms 翻转一次，作为程序运行心跳。

关键源码：

- `Core/Src/main.c`：PWM 启动、角度换算、OLED 与串口同步、按键和序列状态机；
- `Core/Src/tim.c`：TIM2 初始化与 PA0 复用功能配置；
- `Core/Src/gpio.c`：OLED、按键和状态灯 GPIO 配置；
- `Core/Src/usart.c`：USART1 115200 8N1 配置；
- `Core/Src/OLED.c`：软件 I²C OLED 驱动；
- `ServoOledAssignment4.ioc`：CubeMX 引脚、时钟、定时器和串口配置。

## 编译方法

在 CLion 中打开项目根目录，等待 CMake 重新加载，选择 `ServoOledAssignment4.elf` 后执行 Build。

本机使用 CLion 自带的 ARM GCC 和 CMake，终端等价命令为：

```powershell
cmake -S . -B cmake-build-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc
cmake --build cmake-build-debug --parallel
```

当前 Debug 构建结果：

```text
RAM:   4320 B / 20 KB  (21.09%)
FLASH: 42440 B / 64 KB (64.76%)
```

## 验收记录

2026-11-21 实机测试通过：

- ST-Link 识别目标为 STM32F103 Medium-density，Flash 大小 64 KB；
- 下载和校验成功；
- COM8 串口依次输出五个规定角度；
- 45° 和 135° 的脉宽同时确认了整数线性换算；
- PA12 按键将 180° 调整为 165°，对应 `CCR=2292`、`2.292 ms`；
- PA11 按键将 165° 调回 180°，对应 `CCR=2450`、`2.450 ms`；
- 舵机能够按序列定位，舵机电源与开发板共地；
- OLED 三行内容与串口数值同步；
- 自动序列结束后可继续用按键微调。

## 参考资料与驱动来源

1. 电控组《南海鲨电控第四次培训_自学引导文档》。
2. 电控组第四次培训资料中的 `OLED.c`、`OLED.h`、`OLED_Font.h` 软件 I²C 驱动。
3. STM32F103 参考手册中的 RCC、TIM PWM、GPIO、USART 章节。
4. STM32F1 HAL 库 `STM32Cube FW_F1 V1.8.7`。
5. 江协科技 OLED 与 PWM 教程思路。
