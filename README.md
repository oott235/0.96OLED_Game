# STM32F103C8T6 + 0.96" SSD1306 OLED + 无线手柄贪吃蛇

基于 STM32F103C8T6（Cortex-M3 @ 72MHz，Flash 64KB / SRAM 20KB）的手柄贪吃蛇游戏工程。
开发环境：Keil MDK（ARMCC V5.06）+ 标准外设库 SPL 3.5.0。

- 显示：0.96 寸 SSD1306 OLED，128x64，单色，I2C（软件模拟）
- 手柄：无线手柄（蓝牙/2.4G 连电脑）→ USB-TTL → USART1（115200），18 字节帧
- 开机即从"是否收到手柄消息"开始：主界面实时显示手柄连接状态（PAD: OK / PAD: WAIT），
  A 键进入游戏
- 小游戏：贪吃蛇，A 键进入 / X 退出 / B 暂停，手柄十字键/左摇杆控制转向

## 引脚连接

### OLED：SSD1306 0.96"（软件 I2C）

| 模块引脚 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| VCC | 3.3V | 逻辑电源 |
| GND | GND | 与 MCU 共地 |
| SCL | PB6 | 软件 I2C 时钟 |
| SDA | PB7 | 软件 I2C 数据 |

> 注意：软件 I2C 使用**开漏输出**（`GPIO_Mode_Out_OD`），SDA 需要被从机
> 拉低以返回 ACK / 读数据，因此总线上**必须有上拉电阻**。OLED 模块通常
> 自带 4.7k 上拉；若无上拉，请在 SCL/SDA 各接 4.7k~10k 上拉到 3.3V。

### 调试接口

SWD：PA13（SWDIO）/ PA14（SWCLK），应用代码不占用。

### 无线手柄（USB-TTL 转发）

| 连接 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| USB-TTL TX | PA10（USART1_RX） | 手柄帧数据输入 |
| USB-TTL RX | PA9（USART1_TX） | 可留空（本工程不回发） |
| USB-TTL GND | GND | 必须与 MCU 共地 |

> 上位机使用 `TTlHandleConversion/handle_to_serial.py`（无线手柄 → 电脑 → USB-TTL），
> 以 100Hz 发送 **18 字节帧**（`0xAA 0x55` 帧头 + 左/右摇杆 int16 + LT/RT 扳机
> uint16 + 按键位图 uint16 + 十字键 + 校验和），波特率 115200 8N1。
> STM32 端由 `gamepad.c` 在 USART1 RXNE 中断中解析。

## 贪吃蛇游戏

主界面按手柄 **A** 键进入游戏，游戏界面顶部显示分数，下方为 16x7 格游戏区。

### 手柄控制（A 确定 / X 退出 / B 暂停）

| 按键 | 功能 |
| :--- | :--- |
| A | 开始游戏（起始画面）/ 重开一局（GAME OVER 画面） |
| B | 暂停 / 继续 |
| X | 退出游戏（返回主界面） |
| 十字键 / 左摇杆 | 控制移动方向（禁止 180° 掉头） |

- 方向读取：优先十字键，其次左摇杆（死区 8000，`gamepad.h` 的 `GAMEPAD_STICK_DEADZONE` 可调）
- 按键为边沿触发（按一次响一次），`gamepad.h` 的 `GAMEPAD_BTN_*` 定义位号

### 游戏规则

- **无边界（穿墙）模式**：蛇从一侧穿出、从对侧穿入（右出左进、上出下进），不会撞墙死亡
- 撞到自己 → GAME OVER（显示分数，A 重开 / X 退出）
- 吃到食物 +10 分，蛇身加长；蛇速 200ms/格（`SNAKE_TICK_MS` 可调）
- 游戏区 16 列 x 7 行（顶部 1 行显示分数）

## 工程结构

```
User/
  main.c          主程序（OLED_SELFTEST 开关切换自检/正常模式；开机显示手柄连接状态）
  ssd1306.h/.c    SSD1306 驱动：软件 I2C、全屏显存、图形/文本接口
  gamesnake.h/.c  贪吃蛇小游戏：无线手柄（十字键/摇杆转向，A/B/X 功能键）
  gamepad.h/.c    无线手柄驱动：USART1 中断接收 + 18 字节帧解析（边沿/电平查询）
  bsp_delay.h/.c  SysTick 1ms 时基 + DWT 微秒延时
  fonts.h/.c      8x16（VGA）与 8x8 ASCII 字库，index = ch - 0x20
  stm32f10x_it.c  SysTick_Handler 维护 1ms 计数
Library/          SPL 3.5.0 外设库
Start/            启动文件、core_cm3、stm32f10x.h、system_stm32f10x.c
```

## 关键实现

- **手柄帧解析**：USART1 RXNE 中断里逐字节状态机收 18 字节帧，`0xAA 0x55` 帧头 +
  前 17 字节累加校验，校验通过才更新状态，抗噪声；按键按 0->1 边沿锁存，提供
  边沿（按下一次）/ 电平（按住）两种查询。
- **方向控制**：优先读十字键位图，其次左摇杆（死区 8000），映射为 0上 1右 2下 3左；
  贪吃蛇内禁止 180° 掉头（`SetDir` 反向保护）。
- **全屏显存**：SRAM 20KB 足够放下 1KB（128x64/8）显存，绘制后整屏刷新。
- **文本接口**：`SSD1306_ShowChar/ShowString/ShowNum/ShowInt32/ShowFloat/ShowHex`，
  size 1=8x8，size 2=8x16。8x8 与 8x16 字库位序不同（前者 bit0=左，后者 bit7=左），
  驱动已分别处理。

## 编译与烧录

1. Keil MDK 打开 `Project.uvprojx`
2. Build（0 Error / 0 Warning）
3. ST-Link SWD 下载

## 配置开关

| 宏 | 位置 | 说明 |
| :--- | :--- | :--- |
| `OLED_SELFTEST` | main.c | 1=OLED 自检（边框/圆/字符表/扫描线循环，验新屏）；0=正常模式 |
| `GAMEPAD_BAUDRATE` | gamepad.h | 串口波特率（默认 115200，与上位机一致） |
| `GAMEPAD_STICK_DEADZONE` | gamepad.h | 左摇杆死区（默认 8000，满量程 ±32768） |
| `SNAKE_TICK_MS` | gamesnake.h | 蛇移动周期（默认 200ms） |
| `SSD1306_SEGMENT_REMAP` | ssd1306.h | 0=0xA0 非重映射；1=0xA1 段重映射（默认，实测方向正确） |
| `SSD1306_COM_SCAN_INVERT` | ssd1306.h | 0=0xC0 COM 正常扫描；1=0xC8 反向扫描（默认） |
| `SSD1306_I2C_ADDR` | ssd1306.h | OLED I2C 地址 0x3C（SA0=GND）；SA0=VCC 改 0x3D |
| `SSD1306_I2C_HALF_PERIOD_US` | ssd1306.h | I2C 半周期 2us≈250kHz；调大降速（5=100kHz） |
| `SSD1306_SCL_*` / `SSD1306_SDA_*` | ssd1306.h | 软件 I2C 引脚，可任意改 |

## 注意事项

- **晶振**：按 8MHz HSE 配置（PLL ×9 = 72MHz）。实际晶振不是 8MHz 时，改
  `stm32f10x.h` 的 `HSE_VALUE`，否则延时与 I2C 时序均不准。
- **编码**：源文件为 UTF-8 **with BOM**。ARMCC V5 对无 BOM 的 UTF-8 按系统码页（GBK）
  误读，中文注释乱码、中文字符串字面量报 #870。编辑后注意保留 BOM。
- **供电必须共地**。LDO 悬浮地会造成 I2C 波形异常、屏幕闪烁。
- **换屏后先用 `OLED_SELFTEST=1` 验证**（几何图形 + 字符表），确认正常再切回 `0`。
- **总线必须有上拉**：软件 I2C 开漏模式依赖外部上拉，OLED 模块通常自带；
  若没有，SCL/SDA 各接 4.7k~10k 到 3.3V。
- **手柄帧小端序**：`gamepad.c` 按小端解析摇杆/扳机/按键（与上位机 `struct.pack("<...")` 一致）。
- **手柄无响应**：USB-TTL 波特率是否 115200、GND 是否共地、PA10 是否接到 USB-TTL 的 TX；
  上位机未运行/未选择串口时主界面显示 `PAD: WAIT`，游戏不受控。

## 故障排查简表

| 现象 | 排查方向 |
| :--- | :--- |
| OLED 完全无显示 | I2C 地址（SA0）/ 供电共地 / 电荷泵命令；SDA/SCL 是否接反；总线上拉缺失 |
| 仅小字（8x8）镜像、大字（8x16）正常 | 8x8 字库是 LSB-first（bit0=最左），确认 `SSD1306_ShowChar` 按 `lsb_first` 解析 |
| 所有文字左右镜像 | 改 `SSD1306_SEGMENT_REMAP` 翻转（0xA0 ↔ 0xA1） |
| 画面上下颠倒 | 改 `SSD1306_COM_SCAN_INVERT` 翻转（0xC0 ↔ 0xC8） |
| OLED 有显示但花屏 | I2C 降速（`SSD1306_I2C_HALF_PERIOD_US` 调大）；杜邦线过长；上拉缺失 |
| 主界面一直显示 PAD: WAIT | 上位机未运行/未选串口；波特率 115200；PA10 接 USB-TTL TX；GND 共地 |
| 手柄按键/方向无效 | 上位机是否在发送（终端帧数增长）；波特率 115200；PA10 接 USB-TTL TX；GND 共地 |
| 手柄方向相反/按键错位 | 用上位机仪表盘核对轴/按键位号；`--axis-map` 调整轴映射 |

## 参考

- SSD1306 数据手册（Adafruit SSD1306 例程初始化序列同源）
- Linux `lib/fonts/font_8x16.c`（VGA 8x16 字体数据，公有领域）
