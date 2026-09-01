# STM32F103C8T6 + 0.96" SSD1306 OLED(SPI) + 无线手柄 + SD 卡 + 游戏/相册/视频/雷达/信号发生器

基于 STM32F103C8T6（Cortex-M3 @ 72MHz，Flash 64KB / SRAM 20KB）的多媒体娱乐工程：
**整屏翻页主页 + 应用菜单（游戏 / 相册 / 视频 / 雷达 / 信号发生器）+ SD 卡 FAT32 文件系统**。

开发环境：Keil MDK（ARMCC V5.06）+ 标准外设库 SPL 3.5.0 + FatFS R0.14b。

- 显示：0.96 寸 SSD1306 OLED，128x64，单色，**4 线软件 SPI**（双缓冲 + 脏矩形）
- 手柄：无线手柄（蓝牙/2.4G 连电脑）→ USB-TTL → USART1（115200），18 字节帧
- 存储：SD 卡（SPI1 + FatFS），FAT32 自动格式化，分区管理图片/视频
- 主页：整屏翻页（LT 左翻 / RT 右翻，sin 缓动），共 **5 页**（GAME / ALBUM / VIDEO / RADAR / SIGNAL）
- 应用：GAME（贪吃蛇、小恐龙）、ALBUM（相册）、VIDEO（视频播放，DL 下载在列表最底部）、
  RADAR（S3010 舵机 + HC-SR04 超声波雷达）、SIGNAL（USB 信号发生器 + GPIO 方波）

## ⭐ 特别标注：DL —— TTL 直接把视频写入 SD 卡 /VIDEO/

**不需要拔卡、不需要读卡器**：电脑上把 `video_xxx.txt` 转成 BIN 后，
通过 USB-TTL 串口直接写进 STM32 的 SD 卡 `/VIDEO/` 目录，开机即可播放。

```
video_xxx.txt ──► video_to_bin.py 转换 ──► 18BADAPP.BIN
                                                  │
                    电脑 ──USB线──► USB-TTL 模块 ──3根杜邦线──► STM32F103C8T6
                                                  │                   │
                                                  │           VIDEO 列表最底部选 DL (A 进入)
                                                  └──USART1──────────┘
                                          写进 SD 卡 /VIDEO/ 目录
```

| 连接 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| USB-TTL TX | PA10（USART1_RX） | 接收电脑发来的视频数据 |
| USB-TTL RX | PA9（USART1_TX） | **必须接**（要回 ACK 确认每块数据） |
| USB-TTL GND | GND | 必须与 MCU 共地 |

> 下载时手柄转发程序（handle_to_serial.py）要先关掉，串口不能被占用。
> 全流程见 `VideoToBinConversion/README.md`；命令：`python video_to_bin.py --send-sd`。
> **DL 只在 VIDEO 应用列表最底部，主页没有 DL 项。**

## 功能总览

| 界面 | 操作 | 说明 |
| :--- | :--- | :--- |
| 主页 | LT/RT 翻页，A 进入 | **5 页**整幅图片（GAME / ALBUM / VIDEO / RADAR / SIGNAL），sin 缓动翻页动画 |
| GAME | LT/RT 移光标，A 启动，X 返回 | 软件列表：1.SNAKE 贪吃蛇、2.DINO 小恐龙 |
| ALBUM | LT/RT 切图，A/X 返回 | 浏览 SD 卡根目录 `.BIN` 图片 |
| VIDEO | LT/RT 移光标，A 播放，B 暂停，X 返回 | 播放 SD 卡 `/VIDEO/` 目录视频；**列表最底部 = DL 下载** |
| RADAR | 进入先选模式（1 MANUAL / 2 AUTO） | 舵机 + 超声波：手调角度 或 自动扫描雷达图像 |
| SIGNAL | 进入先选模式（1 USB / 2 GPIO） | 信号发生器：USB 出 vofa+ 波形 或 PA4 出方波/PWM 波形 |

## 引脚连接

### OLED：SSD1306 0.96"（4 线软件 SPI）

| 模块引脚 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| VCC | 3.3V | 逻辑电源 |
| GND | GND | 与 MCU 共地 |
| D0 (SCLK) | PB12 | 软件 SPI 时钟（模式 0，上升沿采样） |
| D1 (MOSI/SDIN) | PB13 | 软件 SPI 数据 |
| RES | PB14 | 复位（低有效） |
| DC (A0) | PB15 | 0=命令 / 1=数据 |
| CS | PA8 | 片选（低有效） |

### SD 卡模块（硬件 SPI1，CS 在 PB0）

| 模块引脚 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| VCC | **5V** | 本模块兼容 5V 供电（板载稳压/电平转换）；3.3V 供电会导致卡初始化失败（`r1:01`） |
| GND | GND | 必须与 MCU 共地 |
| CS | **PB0** | 片选（GPIO 推挽，避开 SPI1 硬件引脚限制） |
| SCK | PA5 | SPI1_SCK |
| MISO | PA6 | SPI1_MISO |
| MOSI | PA7 | SPI1_MOSI |

> SD 卡支持 SDSC / SDHC / SDXC；初始化用 281kHz，运行提速 9MHz。
> 未格式化（非 FAT32）的卡会**自动格式化为 FAT32**（`f_mkfs`），无需电脑读卡器。
> 开机有 SD 诊断屏：`code:1 d:1`=CMD0 无响应（查接线）、`d:2 r1:01`=供电不足/卡未就绪、
> `d:2 r1:FF`=MISO 断。**改 CS 引脚不影响卡内容**，只有挂载失败才会自动格式化。

### 无线手柄（USB-TTL 转发）与 TTL 下载共用 USART1

| 连接 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| USB-TTL TX | PA10（USART1_RX） | 手柄帧 / 下载数据输入 |
| USB-TTL RX | PA9（USART1_TX） | 手柄模式可留空；**DL 下载必须接**（回 ACK） |
| USB-TTL GND | GND | 必须与 MCU 共地 |

> 上位机使用 `TTlHandleConversion/handle_to_serial.py`（无线手柄 → 电脑 → USB-TTL），
> 以 100Hz 发送 **18 字节帧**（`0xAA 0x55` 帧头 + 左/右摇杆 int16 + LT/RT 扳机
> uint16 + 按键位图 uint16 + 十字键 + 校验和），波特率 115200 8N1。
> STM32 端由 `gamepad.c` 在 USART1 RXNE 中断中解析。

### RADAR：S3010 舵机 + HC-SR04 超声波

| 模块 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| 舵机信号线 | **PA2**（TIM2_CH3） | S3010，50Hz PWM（0.5~2.5ms = 0°~180°） |
| HC-SR04 Trig | **PA0** | 触发（推挽输出） |
| HC-SR04 Echo | **PA1** | 回波（下拉输入；模块 5V 供电时建议串 500Ω 或分压，PA1 非 5V 容忍） |
| HC-SR04 VCC | 5V | 模块需 5V（3.3V 供电只能测几厘米） |
| 舵机/模块 GND | GND | 与 MCU 共地；舵机建议独立电源只共地 |

### SIGNAL：USB / GPIO 输出

| 连接 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| micro USB 口 | PA11(DP)/PA12(DM) | **直连电脑**（USB CDC 虚拟串口，模式 1，无需 TTL） |
| 波形输出 | **PA4** | GPIO 推挽输出（模式 2，方波/PWM 波形） |
| GND | GND | 与查看设备共地 |

### 调试接口

SWD：PA13（SWDIO）/ PA14（SWCLK），应用代码不占用。

## SD 卡分区约定

开机流程：SD 初始化 → FAT32 挂载（自动格式化）→ 写入内置图片（首次）→ 创建 `/VIDEO` 目录 → 进入主页。

| 位置 | 内容 | 格式 |
| :--- | :--- | :--- |
| SD 根目录 | 相册图片（SNAKE.BIN / WHALE.BIN / SCREEN.BIN，开机自动写入） | 1024 字节页格式位图 |
| SD `/VIDEO/` | 视频文件（如 `18BADAPP.BIN`） | 连续帧流，每帧 1024 字节页格式 |

### 图片格式（相册）

- 每张 **1024 字节**：128x64 单色，**页格式**（`bmp[page*128+x]` 的 bit `y%8` 表示像素 (x,y)），与 SSD1306 显存布局一致，可直接整帧显示
- 开机时内置的 `SNAKE.BIN`（蛇）、`WHALE.BIN`（鲸鱼女孩）、`SCREEN.BIN`（软件界面）自动写入根目录
- 新增图片：任意 128x64 页格式位图，以 `.BIN` 后缀放 SD 卡根目录，相册自动枚举

### 视频格式（VIDEO 应用）

- 文件放 **SD 卡 `/VIDEO/` 目录**，扩展名 `.BIN`
- 内容为**连续帧流**：每帧 1024 字节页格式（与显存一致，直接显示）
- **帧率 = 文件名前 2 位数字**：如 `18BADAPP.BIN` = 18fps；非数字前缀默认 18fps
- 示例：`video_badapple.txt`（3504 帧文本）已在本目录转换为 `18BADAPP.BIN`（3.4MB），放入 `/VIDEO/` 即可播放
- 新增视频：命名 `帧率+名字.BIN`（如 `24CLOCK.BIN` = 24fps）放入 `/VIDEO/`

> 注意：视频文件较大（MB 级），远超 64KB Flash，**必须存于 SD 卡**；
> 没有读卡器时可用 DL 下载（TTL 串口写入）。

## 应用说明

### GAME：软件列表

```
SOFTWARE
1.SNAKE     >      ← LT/RT 移光标，选中行尾箭头
2.DINO
```

| 游戏 | 操作 |
| :--- | :--- |
| 贪吃蛇（1.SNAKE） | A/十字键上=开始确认，十字键/左摇杆=方向（禁 180° 掉头），B=暂停，X=退出；无边界穿墙 |
| 小恐龙（2.DINO） | A/十字键上=跳跃，B=暂停，X=退出；障碍速度随分数加快，撞到 GAME OVER |

### ALBUM：相册

- LT/RT 切换上一张/下一张，A 或 X 返回主页
- 显示 SD 卡根目录所有 `.BIN` 图片（全屏）

### VIDEO：视频播放

- 列表：枚举 `/VIDEO/*.BIN`，LT/RT 移光标，行尾 `>` 箭头，A 播放；**最底部固定一行 = DL 下载**
- 播放：按帧率节奏从 SD 流式读帧（双缓冲整帧显示），**B 暂停/继续**，**Y 删除确认弹窗**，**X 退出**，播完循环

### RADAR：舵机 + 超声波雷达

进入后先选模式（LT/RT 选择，A 确认，X 返回）：

**1 MANUAL** — 手调舵机角度：
- LT 逆时针 / RT 顺时针，**按住持续加速**（速度逐级累加，封顶 8°/拍，松开清零）
- 屏幕显示大号角度数字 + 进度条

**2 AUTO** — 自动旋转雷达：
- 舵机 0°~180° 往返扫描（每拍 2°，25ms）
- 每个角度触发 HC-SR04 测距（声速换算 `us/58=cm`，两次触发间隔强制 ≥60ms 防丢远回波）
- 屏幕绘制**极坐标雷达图像**：三圈量程圈 + 30° 刻度线 + 余辉轨迹（3x3 加粗点）+ 扫描线
- 顶部实时显示 `A:角度 D:距离cm`；**倒立安装时左右镜像**（`RADAR_MOUNT_INVERT=1`）

### SIGNAL：信号发生器

进入后先选模式（LT/RT 选择，A 确认，X 返回）：

**1 USB → PC** — micro USB 直连电脑：
- **USB CDC 虚拟串口**（ST 标准 VID/PID，装 VCP 驱动后出现 COM 口），无需 TTL 转接
- 波形数据按 **JustFloat 协议**发送（float 值 + 帧尾 `00 00 80 7F`），vofa+ 选 JustFloat 解析实时显示
- 正弦 SIN / 三角 TRI / 方波 SQR / 锯齿 SAW 四种波形，LT/RT 切换
- A 循环切换参数（F 频率 / A 幅度 / D 占空比），左右键调节
- 屏幕底部 `TX:包数` 诊断；USB 未枚举成功时不阻塞界面

**2 GPIO → PA4** — PA4 推挽输出（不依赖 DAC/TIM）：
- **方波 / PWM 调制波形**：25kHz 载波，占空比按波形表逐点变化，正弦/三角/方波/锯齿都支持
- 频率 10Hz~3kHz（每周期最少 8 个采样点）、幅度、占空比可调
- 波形还原：接 RC 低通（1kΩ+100nF）得平滑模拟波，或示波器看 PWM 包络

> 说明：本方案 GPIO 输出**不依赖 DAC/DMA/TIM 等 APB1 外设**（部分板子 APB1 域异常、
> DAC 不可用时仍可输出波形）。若你的板子 DAC 正常，可用带 DAC 的早期版本固件。

## 工程结构

```
User/
  main.c          主程序：SD 初始化 + 生成主页页面 + 进入翻页菜单（5 页）
  menu.h/.c       整屏翻页菜单：双向链表 + 状态机 + sin 缓动 + 脏矩形
  gamesnake.h/.c  贪吃蛇游戏
  gamedino.h/.c   小恐龙跑酷游戏
  album.h/.c      相册：枚举 SD 根目录 .BIN 图片，LT/RT 切换
  video.h/.c      视频：枚举 /VIDEO/*.BIN，按帧率播放，Y 删除，底部 DL 入口
  sd_dl.h/.c      TTL 下载：USART1 接收视频 BIN -> FatFS 写入 /VIDEO/（逐块 ACK）
  gamepad.h/.c    无线手柄驱动：USART1 中断接收 + 18 字节帧解析
  sd_spi.h/.c     SD 卡 SPI 驱动：初始化序列 + 扇区读写 + CSD 容量解析（CS=PB0）
  ssd1306.h/.c    SSD1306 驱动：4 线软件 SPI、双缓冲、区域裁剪绘制
  servo.h/.c      RADAR 应用：S3010 舵机（TIM2_CH3/PA2）+ HC-SR04（PA0/PA1）+ 雷达图像
  signal.h/.c     SIGNAL 应用：USB CDC（vofa+）+ GPIO PWM 波形输出（PA4）
  images.h        内置图片（snake/whale/screen/cmd/radar/signal，水平格式）
  bsp_delay.h/.c  SysTick 1ms 时基 + DWT 微秒延时
  fonts.h/.c      8x16（VGA）与 8x8 ASCII 字库，index = ch - 0x20
  stm32f10x_it.c  SysTick_Handler 维护 1ms 计数；USB 中断（USB_Istr）
  usb/            ST USB-FS-Device 库：CDC 虚拟串口（usb_core/desc/prop/endp/pwr/...）
    hw_config.c   板级适配：USB 时钟 48MHz、中断、CDC_Send_DATA
    platform_config.h  引脚/ID 适配（PA15 上拉占位）
FatFs/
  ff.c ff.h ffconf.h ffsystem.c diskio.c diskio.h   FatFS R0.14b
  diskio.c        底层对接：SD_ReadSector / SD_WriteSector
Library/          SPL 3.5.0 外设库
Start/            启动文件、core_cm3、stm32f10x.h、system_stm32f10x.c
```

## 关键实现

- **手柄帧解析**：USART1 RXNE 中断逐字节状态机收 18 字节帧，帧头 + 前 17 字节累加校验；
  按键 0->1 边沿锁存（按一次响一次）；扳机/摇杆实时电平查询；**LT/RT 触发阈值 = 后 1/5 行程**
  （`GAMEPAD_TRIGGER_ON = 26214`）。
- **整屏翻页菜单**：每页一张 128x64 图片全屏；LT/RT 翻页时旧页滑出、新页滑入，
  **easeInOutSine（sin 曲线）缓动**，两端平滑无顿挫；底部居中显示应用名。
- **SD 卡驱动**：CMD0 → CMD8（SDHC 检测）→ ACMD41（HCS=1 失败自动降 HCS=0 兼容老卡）→
  CMD58（高容量判断）→ CMD16；初始化 281kHz、运行 9MHz；ACMD41 超时 3s。
- **FatFS 集成**：`diskio.c` 对接 `SD_ReadSector/WriteSector`；`ffconf.h` 启用
  `FF_USE_MKFS`（自动格式化 FAT32）、`FF_CODE_PAGE=437`、`FF_FS_NORTC=1`。
- **SSD1306 双缓冲**：绘制缓冲 `s_fb` + 传输缓冲 `s_tx`；`Display()` 先快照再整帧发送，
  发送期间可继续绘制不撕裂；`DisplayRange()` 支持脏矩形局部刷新；
  `ShowBitmapRegion()` 支持负坐标裁剪（翻页动画用）。
- **视频播放**：顺序 f_read 1024 字节/帧（不逐帧 lseek），仅循环回绕时定位；帧率 1~60fps 由文件名解析。
- **TTL 下载**：DL 模式暂时关闭手柄 RXNE 中断，轮询 USART1 收帧（`0xA5 0x5A` 帧头 +
  命令 + 校验和，`SUM = (CMD+payload) & 0xFF`）；每块 512B 收完回 ACK（`0x06`）、
  校验错回 NAK（`0x15`）让电脑重发；写 SD 卡用 FatFS `f_write`，结束后自动恢复手柄。
- **超声波测距**：DWT 微秒计时 Echo 高电平时间，`us/58=cm`；两次触发间隔强制 ≥60ms
  （HC-SR04 手册要求，过快会丢远回波只测近处）。
- **USB CDC**：ST USB-FS-Device 库，micro USB 直连；JustFloat 协议发送；`fSuspendEnabled=FALSE`
  防挂起进 STOP 模式；USB 中断用最低优先级防饿死 SysTick；退出时关闭中断+掉电 USB。
- **GPIO 波形**：PA4 推挽输出 25kHz PWM，占空比按 256 点波形表逐点调制，
  经 RC 低通还原模拟波形；不依赖 APB1 外设。

## 编译与烧录

1. Keil MDK 打开 `Project.uvprojx`
2. Build（0 Error / 0 Warning）
3. ST-Link SWD 下载（**烧录时只接 ST-Link 供电，不要同时插独立电源**，两电源会打架报 internal command error）

## 配置开关

| 宏 | 位置 | 说明 |
| :--- | :--- | :--- |
| `OLED_SELFTEST` | main.c | 1=OLED 自检（几何图形+字符表循环，验新屏）；0=正常模式 |
| `GAMEPAD_BAUDRATE` | gamepad.h | 串口波特率（默认 115200，与上位机一致） |
| `GAMEPAD_STICK_DEADZONE` | gamepad.h | 左摇杆死区（默认 8000，满量程 ±32768） |
| `GAMEPAD_TRIGGER_ON` | gamepad.h | LT/RT 扳机触发阈值（默认 26214 = 后 1/5 行程） |
| `RADAR_MOUNT_INVERT` | servo.c | 1=雷达倒立安装，显示左右镜像 |
| `RADAR_STEP` / `RADAR_TICK_MS` | servo.c | 雷达扫描步进（°）/ 每拍间隔（ms） |
| `SERVO_SPEED_BASE/ACCEL/MAX` | servo.c | 手调模式加速参数 |
| `ULTRA_MIN_INTERVAL_US` | servo.c | 超声波两次触发最小间隔（60000us = 60ms） |
| `SNAKE_TICK_MS` | gamesnake.h | 蛇移动周期（默认 200ms） |
| `DINO_TICK_MS` / `DINO_JUMP_V` | gamedino.h/.c | 小恐龙步进周期 / 跳跃初速度 |
| `MENU_ANIM_MS` | menu.c | 主页翻页动画时长（默认 200ms） |
| `SSD1306_SEGMENT_REMAP` | ssd1306.h | 0=0xA0 非重映射；1=0xA1 段重映射（默认） |
| `SSD1306_COM_SCAN_INVERT` | ssd1306.h | 0=0xC0 COM 正常扫描；1=0xC8 反向扫描（默认） |
| `SSD1306_SPI_HALF_PERIOD_US` | ssd1306.h | 软件 SPI 半周期 2us≈250kHz；调大降速 |
| `FF_USE_MKFS` | FatFs/ffconf.h | 1=允许 f_mkfs 自动格式化 FAT32 |
| `DL_DATA_MAX` | sd_dl.c | 下载单块数据最大字节（512） |

## 注意事项

- **编码**：源文件为 UTF-8 **with BOM**。ARMCC V5 对无 BOM 的 UTF-8 按系统码页（GBK）
  误读，中文注释乱码、中文字符串字面量报 #870。编辑后注意保留 BOM。
- **晶振**：按 8MHz HSE 配置（PLL ×9 = 72MHz）。实际晶振不是 8MHz 时，改
  `stm32f10x.h` 的 `HSE_VALUE`，否则延时与 SPI/I2C 时序均不准。
- **供电必须共地**。LDO 悬浮地会造成 SPI/I2C 波形异常、屏幕闪烁、SD 卡读写失败。
- **换屏后先用 `OLED_SELFTEST=1` 验证**（几何图形 + 字符表），确认正常再切回 `0`。
- **SD 卡供电**：本模块用 **5V**（板载稳压/电平转换）；若用 3.3V 供电，
  ACMD41 初始化会超时（`r1:01`）——5V 供电后恢复正常。
- **HC-SR04 供电**：必须 5V（3.3V 只能测几厘米）；**Echo 输出 5V，PA1 非 5V 容忍**，
  建议 Echo 串 500Ω 电阻或 1k+2k 分压再接 PA1，防止灌电流/损坏。
- **舵机供电**：S3010 电流大，建议独立电源（只与 MCU 共地），不要和 MCU 共用一个弱 5V。
- **烧录供电**：ST-Link 下载时只接 ST-Link（含 3.3V），**不要同时插独立电源**，
  否则两电源打架，下载报 "Internal command error"；烧完拔 ST-Link 用独立电源运行。
- **USB 模式**：micro USB 直连电脑，需装 ST VCP 驱动（Windows 首次插显示"无法识别"
  时重插/重装驱动）；vofa+ 必须选 **JustFloat** 解析方式；USB 未枚举时界面不卡死。
- **视频文件必须在 SD 卡 `/VIDEO/` 目录**，文件名前 2 位为帧率；图片在根目录。
- **手柄帧小端序**：`gamepad.c` 按小端解析摇杆/扳机/按键（与上位机 `struct.pack("<...")` 一致）。
- **DL 下载前**：关闭手柄转发程序（handle_to_serial.py）、确认串口未被占用、
  USB-TTL 的 RX（→PA9）必须接上（要回 ACK）；下载期间手柄不可用，结束后自动恢复。
- **APB1 外设异常提示**：若板子出现"GPIO 正常但 TIM2/DAC 寄存器写不进"
  （打磨片/翻新片 APB1 域异常），舵机（TIM2）和 DAC 不可用——此时用
  SIGNAL 的 **GPIO 模式**（不依赖 APB1）输出波形；舵机只能换板子。

## 故障排查简表

| 现象 | 排查方向 |
| :--- | :--- |
| OLED 完全无显示 | 4 线 SPI 接线（PB12/13/14/15 + PA8）；RES 复位；供电共地 |
| 所有文字左右镜像 | 改 `SSD1306_SEGMENT_REMAP` 翻转（0xA0 ↔ 0xA1） |
| 画面上下颠倒 | 改 `SSD1306_COM_SCAN_INVERT` 翻转（0xC0 ↔ 0xC8） |
| OLED 有显示但花屏 | `SSD1306_SPI_HALF_PERIOD_US` 调大降速；杜邦线过长 |
| SD: FAIL | 接线（PB0/5/6/7）；**5V 供电**；卡是否损坏；看诊断码（code/d/r1） |
| SD `d:1`（CMD0 无响应） | CS 线没插对 PB0 / MOSI-MISO-SCK 松 / 模块没供电 |
| SD `d:2 r1:01` | 卡未就绪：**供电不足（改用 5V）**、卡接触不良、擦金手指 |
| SD `d:2 r1:FF` | MISO 线断 |
| 相册无图片 | SD 根目录是否有 .BIN；开机是否写入（首次需 SD 正常） |
| 视频列表为空 | `/VIDEO/` 目录是否存在（开机自动创建）；是否有 .BIN 文件 |
| 视频卡顿 | 确认文件名帧率（如 18BADAPP.BIN=18fps）；SD 卡速度 |
| DL: TIMEOUT | 电脑是否已运行 `video_to_bin.py --send-sd`；串口选择；下载前先在 VIDEO 列表进 DL |
| DL: SD OPEN FAIL | SD 卡未就绪/挂载失败；卡是否损坏 |
| DL: FAIL | 下载中断/写卡失败；RX（PA9）未接；校验重发超限；重跑一次即可 |
| 手柄无响应 | 上位机是否在发送；波特率 115200；PA10 接 USB-TTL TX；GND 共地 |
| 雷达扫不到远处 | HC-SR04 是否 5V 供电；Echo 是否分压/限流；触发间隔 60ms 已强制；传感器朝向 |
| 雷达图左右反 | `RADAR_MOUNT_INVERT` 置 1（倒立安装） |
| SIGNAL PA4 无输出 | 用 GPIO 模式（2 GPIO）；确认 PA4 没插别的线；万用表黑笔共地 |
| vofa 无波形 | vofa+ 选 **JustFloat**；打开正确的 COM 口；点开始；装 ST VCP 驱动 |
| USB 插上"无法识别" | 换数据线；重插；重装 ST VCP 驱动；检查 D+ 上拉 |
| 下载报 internal command error | 烧录时只接 ST-Link 供电，拔掉独立电源 |

## 参考

- SSD1306 数据手册（Adafruit SSD1306 例程初始化序列同源）
- SD 卡物理层简化规范 v2.00（SPI 模式命令/响应）
- FatFS 官方文档（elm-chan.org，R0.14b）
- ST USB-FS-Device 库 V4.0（CDC 虚拟串口）
- HC-SR04 超声波模块数据手册
- Linux `lib/fonts/font_8x16.c`（VGA 8x16 字体数据，公有领域）
