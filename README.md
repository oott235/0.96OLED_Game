# STM32F103C8T6 + 0.96" SSD1306 OLED(SPI) + 无线手柄 + SD 卡 + 游戏/相册/视频

基于 STM32F103C8T6（Cortex-M3 @ 72MHz，Flash 64KB / SRAM 20KB）的多媒体娱乐工程：
**整屏翻页主页 + 应用菜单（游戏 / 相册 / 视频）+ SD 卡 FAT32 文件系统**。

开发环境：Keil MDK（ARMCC V5.06）+ 标准外设库 SPL 3.5.0 + FatFS R0.14b。

- 显示：0.96 寸 SSD1306 OLED，128x64，单色，**4 线软件 SPI**（双缓冲 + 脏矩形）
- 手柄：无线手柄（蓝牙/2.4G 连电脑）→ USB-TTL → USART1（115200），18 字节帧
- 存储：SD 卡（SPI1 + FatFS），FAT32 自动格式化，分区管理图片/视频
- 主页：整屏翻页（LT 左翻 / RT 右翻，sin 缓动），底部居中显示应用名
- 应用：GAME（贪吃蛇、小恐龙）、ALBUM（相册）、VIDEO（视频播放）

## 功能总览

| 界面 | 操作 | 说明 |
| :--- | :--- | :--- |
| 主页 | LT/RT 翻页，A 进入 | 3 页整幅图片（GAME / ALBUM / VIDEO），sin 缓动翻页动画 |
| GAME | LT/RT 移光标，A 启动，X 返回 | 软件列表：1.SNAKE 贪吃蛇、2.DINO 小恐龙 |
| ALBUM | LT/RT 切图，A/X 返回 | 浏览 SD 卡根目录 `.BIN` 图片 |
| VIDEO | LT/RT 移光标，A 播放，B 暂停，X 返回 | 播放 SD 卡 `/VIDEO/` 目录视频 |

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

### SD 卡模块（硬件 SPI1）

| 模块引脚 | STM32 引脚 | 说明 |
| :--- | :--- | :--- |
| VCC | 3.3V | 电源 |
| GND | GND | 必须与 MCU 共地 |
| CS | PA4 | 片选（GPIO 推挽） |
| SCK | PA5 | SPI1_SCK |
| MISO | PA6 | SPI1_MISO |
| MOSI | PA7 | SPI1_MOSI |

> SD 卡支持 SDSC / SDHC / SDXC；初始化用 281kHz，运行提速 9MHz。
> 未格式化（非 FAT32）的卡会**自动格式化为 FAT32**（`f_mkfs`），无需电脑读卡器。

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

> 注意：视频文件较大（MB 级），远超 64KB Flash，**必须存于 SD 卡**，
> 需要读卡器 / 手机 OTG / 电脑卡槽拷入 SD 卡。

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

- 列表：枚举 `/VIDEO/*.BIN`，LT/RT 移光标，行尾 `>` 箭头，A 播放
- 播放：按帧率节奏从 SD 流式读帧（双缓冲整帧显示），**B 暂停/继续**，**X 退出**，播完循环

## 工程结构

```
User/
  main.c          主程序：SD 初始化 + 生成主页页面 + 进入翻页菜单
  menu.h/.c       整屏翻页菜单：双向链表 + 状态机 + sin 缓动 + 脏矩形
  gamesnake.h/.c  贪吃蛇游戏
  gamedino.h/.c   小恐龙跑酷游戏
  album.h/.c      相册：枚举 SD 根目录 .BIN 图片，LT/RT 切换
  video.h/.c      视频：枚举 /VIDEO/*.BIN，按帧率播放
  gamepad.h/.c    无线手柄驱动：USART1 中断接收 + 18 字节帧解析
  sd_spi.h/.c     SD 卡 SPI 驱动：初始化序列 + 扇区读写 + CSD 容量解析
  ssd1306.h/.c    SSD1306 驱动：4 线软件 SPI、双缓冲、区域裁剪绘制
  images.h        内置图片（snake/whale/screen/cmd，水平格式）
  bsp_delay.h/.c  SysTick 1ms 时基 + DWT 微秒延时
  fonts.h/.c      8x16（VGA）与 8x8 ASCII 字库，index = ch - 0x20
  stm32f10x_it.c  SysTick_Handler 维护 1ms 计数
FatFs/
  ff.c ff.h ffconf.h ffsystem.c diskio.c diskio.h   FatFS R0.14b
  diskio.c        底层对接：SD_ReadSector / SD_WriteSector
Library/          SPL 3.5.0 外设库
Start/            启动文件、core_cm3、stm32f10x.h、system_stm32f10x.c
```

## 关键实现

- **手柄帧解析**：USART1 RXNE 中断逐字节状态机收 18 字节帧，帧头 + 前 17 字节累加校验；
  按键 0->1 边沿锁存（按一次响一次）；扳机/摇杆实时电平查询。
- **整屏翻页菜单**：每页一张 128x64 图片全屏；LT/RT 翻页时旧页滑出、新页滑入，
  **easeInOutSine（sin 曲线）缓动**，两端平滑无顿挫；底部居中显示应用名。
- **SD 卡驱动**：CMD0 → CMD8（SDHC 检测）→ ACMD41 → CMD58（高容量判断）→ CMD16；
  初始化 281kHz、运行 9MHz；扇区读写带忙等待；CSD 解析总容量。
- **FatFS 集成**：`diskio.c` 对接 `SD_ReadSector/WriteSector`；`ffconf.h` 启用
  `FF_USE_MKFS`（自动格式化 FAT32）、`FF_CODE_PAGE=437`、`FF_FS_NORTC=1`。
- **SSD1306 双缓冲**：绘制缓冲 `s_fb` + 传输缓冲 `s_tx`；`Display()` 先快照再整帧发送，
  发送期间可继续绘制不撕裂；`DisplayRange()` 支持脏矩形局部刷新；
  `ShowBitmapRegion()` 支持负坐标裁剪（翻页动画用）。
- **视频播放**：顺序 f_read 1024 字节/帧（不逐帧 lseek），仅循环回绕时定位；帧率 1~60fps 由文件名解析。

## 编译与烧录

1. Keil MDK 打开 `Project.uvprojx`
2. Build（0 Error / 0 Warning）
3. ST-Link SWD 下载

## 配置开关

| 宏 | 位置 | 说明 |
| :--- | :--- | :--- |
| `OLED_SELFTEST` | main.c | 1=OLED 自检（几何图形+字符表循环，验新屏）；0=正常模式 |
| `GAMEPAD_BAUDRATE` | gamepad.h | 串口波特率（默认 115200，与上位机一致） |
| `GAMEPAD_STICK_DEADZONE` | gamepad.h | 左摇杆死区（默认 8000，满量程 ±32768） |
| `SNAKE_TICK_MS` | gamesnake.h | 蛇移动周期（默认 200ms） |
| `DINO_TICK_MS` / `DINO_JUMP_V` | gamedino.h/.c | 小恐龙步进周期 / 跳跃初速度 |
| `MENU_ANIM_MS` | menu.c | 主页翻页动画时长（默认 200ms） |
| `SSD1306_SEGMENT_REMAP` | ssd1306.h | 0=0xA0 非重映射；1=0xA1 段重映射（默认，实测方向正确） |
| `SSD1306_COM_SCAN_INVERT` | ssd1306.h | 0=0xC0 COM 正常扫描；1=0xC8 反向扫描（默认） |
| `SSD1306_SPI_HALF_PERIOD_US` | ssd1306.h | 软件 SPI 半周期 2us≈250kHz；调大降速 |
| `SSD1306_SCK/MOSI/RES/DC/CS_*` | ssd1306.h | 软件 SPI 引脚，可任意改 |
| `FF_USE_MKFS` | FatFs/ffconf.h | 1=允许 f_mkfs 自动格式化 FAT32 |
| `FF_CODE_PAGE` | FatFs/ffconf.h | 437（ASCII 短文件名） |

## 注意事项

- **编码**：源文件为 UTF-8 **with BOM**。ARMCC V5 对无 BOM 的 UTF-8 按系统码页（GBK）
  误读，中文注释乱码、中文字符串字面量报 #870。编辑后注意保留 BOM。
- **晶振**：按 8MHz HSE 配置（PLL ×9 = 72MHz）。实际晶振不是 8MHz 时，改
  `stm32f10x.h` 的 `HSE_VALUE`，否则延时与 SPI/I2C 时序均不准。
- **供电必须共地**。LDO 悬浮地会造成 SPI/I2C 波形异常、屏幕闪烁、SD 卡读写失败。
- **换屏后先用 `OLED_SELFTEST=1` 验证**（几何图形 + 字符表），确认正常再切回 `0`。
- **SD 卡电压**：SPI 模式推荐 3.3V 电平；若模块为 5V 电平需确认 STM32 引脚耐压与逻辑兼容。
- **视频文件必须在 SD 卡 `/VIDEO/` 目录**，文件名前 2 位为帧率；图片在根目录。
- **手柄帧小端序**：`gamepad.c` 按小端解析摇杆/扳机/按键（与上位机 `struct.pack("<...")` 一致）。

## 故障排查简表

| 现象 | 排查方向 |
| :--- | :--- |
| OLED 完全无显示 | 4 线 SPI 接线（PB12/13/14/15 + PA8）；RES 复位；供电共地 |
| 所有文字左右镜像 | 改 `SSD1306_SEGMENT_REMAP` 翻转（0xA0 ↔ 0xA1） |
| 画面上下颠倒 | 改 `SSD1306_COM_SCAN_INVERT` 翻转（0xC0 ↔ 0xC8） |
| OLED 有显示但花屏 | `SSD1306_SPI_HALF_PERIOD_US` 调大降速；杜邦线过长 |
| SD: FAIL | 接线（PA4/5/6/7）；3.3V 供电共地；卡是否损坏 |
| 相册无图片 | SD 根目录是否有 .BIN；开机是否写入（首次需 SD 正常） |
| 视频列表为空 | `/VIDEO/` 目录是否存在（开机自动创建）；是否有 .BIN 文件 |
| 视频卡顿 | 确认文件名帧率（如 18BADAPP.BIN=18fps）；SD 卡速度 |
| 手柄无响应 | 上位机是否在发送；波特率 115200；PA10 接 USB-TTL TX；GND 共地 |
| 手柄方向相反/按键错位 | 上位机仪表盘核对轴/按键位号；`--axis-map` 调整轴映射 |

## 参考

- SSD1306 数据手册（Adafruit SSD1306 例程初始化序列同源）
- SD 卡物理层简化规范 v2.00（SPI 模式命令/响应）
- FatFS 官方文档（elm-chan.org，R0.14b）
- Linux `lib/fonts/font_8x16.c`（VGA 8x16 字体数据，公有领域）
