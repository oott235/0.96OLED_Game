/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   SSD1306 OLED 驱动（128x64，4 线 SPI）
  *
  *          -----------------------------------------------
  *          屏幕引脚               STM32F103C8T6
  *          -----------------------------------------------
  *          VCC                    3.3V
  *          GND                    GND
  *          D0 (SCLK)              PB12（软件 SPI 时钟）
  *          D1 (MOSI/SDIN)         PB13（软件 SPI 数据）
  *          RES                    PB14（复位，低有效）
  *          DC (A0)                PB15（0=命令 / 1=数据）
  *          CS                     PA8（片选，低有效）
  *          -----------------------------------------------
  *
  *          注 1: 使用**软件 SPI**（GPIO 模拟，模式 0：SCK 空闲低、上升沿采样），
  *                与 SD 卡的硬件 SPI1（PA5/6/7）完全独立，互不冲突。
  *          注 2: 驱动采用**软件双缓冲**：绘制缓冲（1KB）+ 传输缓冲（1KB）。
  *                所有绘制先写绘制缓冲，SSD1306_Display() 先快照到传输缓冲
  *                再整帧刷屏；发送期间继续绘制不会产生撕裂/花屏。
  *          注 3: 若模块 SA0 需配置地址，SPI 模式下无地址概念，忽略即可。
  ******************************************************************************
  */

#ifndef __SSD1306_H
#define __SSD1306_H

#include "stm32f10x.h"

/*============================== 引脚定义 ==============================*/
#define SSD1306_SCK_GPIO     GPIOB
#define SSD1306_SCK_PIN      GPIO_Pin_12
#define SSD1306_MOSI_GPIO    GPIOB
#define SSD1306_MOSI_PIN     GPIO_Pin_13
#define SSD1306_RES_GPIO     GPIOB
#define SSD1306_RES_PIN      GPIO_Pin_14
#define SSD1306_DC_GPIO      GPIOB
#define SSD1306_DC_PIN       GPIO_Pin_15
#define SSD1306_CS_GPIO      GPIOA
#define SSD1306_CS_PIN       GPIO_Pin_8

/* 软件 SPI 半周期（微秒）：2us ≈ 250kHz；调大降速（如 5 = 100kHz） */
#define SSD1306_SPI_HALF_PERIOD_US  2

/*============================ 屏幕分辨率 ==============================*/
#define SSD1306_WIDTH        128
#define SSD1306_HEIGHT       64

/* 单色亮度：1 = 点亮，0 = 熄灭 */
#define SSD1306_COLOR_ON     1
#define SSD1306_COLOR_OFF    0

/* 显示方向控制（字符镜像 / 上下颠倒时切换）：
   SSD1306_SEGMENT_REMAP   0 = 0xA0 非重映射
                           1 = 0xA1 段重映射（本工程默认，实测方向正确）
   SSD1306_COM_SCAN_INVERT 0 = 0xC0 COM 正常扫描
                           1 = 0xC8 COM 反向扫描（本工程默认，实测方向正确）
   若换屏后出现：
     字符左右镜像 -> 翻转 SSD1306_SEGMENT_REMAP
     画面上下颠倒 -> 翻转 SSD1306_COM_SCAN_INVERT */
#define SSD1306_SEGMENT_REMAP     1
#define SSD1306_COM_SCAN_INVERT   1

/*============================= 公共接口 ===============================*/

/*-------------------------- 初始化与配置 -----------------------------*/
/**
  * @brief  初始化 OLED：GPIO（软件 SPI）、SSD1306 初始化序列、清屏
  */
void SSD1306_Init(void);

/**
  * @brief  整屏清空（写显存，需调用 SSD1306_Display() 生效）
  */
void SSD1306_Clear(void);

/**
  * @brief  将显存一次性刷到屏幕（整屏 1024 字节）
  */
void SSD1306_Display(void);

/**
  * @brief  局部刷新指定页范围（脏矩形支持，双缓冲）
  * @param  page_start, page_end: 页号 0~7（每页 = 8 像素行）
  * @note   只发送 [page_start, page_end] 页，配合脏矩形只更新变化区域
  */
void SSD1306_DisplayRange(uint8_t page_start, uint8_t page_end);

/**
  * @brief  设置对比度（0x00 ~ 0xFF）
  */
void SSD1306_SetContrast(uint8_t value);

/**
  * @brief  反转显示（1 = 反色，0 = 正常）
  */
void SSD1306_InvertDisplay(uint8_t invert);

/*----------------------------- 图形绘制 -------------------------------*/
/**
  * @brief  绘制单像素（写显存）
  * @param  on: SSD1306_COLOR_ON / SSD1306_COLOR_OFF
  */
void SSD1306_DrawPixel(uint16_t x, uint16_t y, uint8_t on);

/**
  * @brief  绘制直线（Bresenham 算法）
  */
void SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);

/**
  * @brief  绘制矩形边框
  */
void SSD1306_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);

/**
  * @brief  填充矩形区域（含边界）
  */
void SSD1306_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on);

/**
  * @brief  绘制圆形边框
  * @param  cx,cy: 圆心坐标，r: 半径
  */
void SSD1306_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t on);

/**
  * @brief  绘制实心圆
  */
void SSD1306_FillCircle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t on);

/*----------------------------- 文本显示 -------------------------------*/
/* size 约定: 1 -> 8x8 字体；2 -> 8x16；>=3 -> 8x16 按 size/2 倍放大 */

/**
  * @brief  显示单个 ASCII 字符（0x20~0x7E，其余显示为 '?'）
  * @param  x,y:  左上角坐标（像素）
  * @param  ch:   字符
  * @param  size: 字体尺寸（见上方约定）
  * @param  on:   前景亮度（1 亮 / 0 灭）
  */
void SSD1306_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size, uint8_t on);

/**
  * @brief  显示字符串，支持 '\n' 换行（回到起始列，行距 = 字体高度）
  * @param  str: 以 '\0' 结尾的字符串
  */
void SSD1306_ShowString(uint16_t x, uint16_t y, uint8_t size, const char *str, uint8_t on);

/**
  * @brief  显示无符号整数，定长并前导补零
  * @param  num: 数值
  * @param  len: 总位数（<=10）
  */
void SSD1306_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t on);

/**
  * @brief  显示有符号 32 位整数（变长，无前导零，兼容 INT32_MIN）
  */
void SSD1306_ShowInt32(uint16_t x, uint16_t y, int32_t value, uint8_t size, uint8_t on);

/**
  * @brief  显示浮点数，固定小数位 + 固定总宽度（右对齐补空格）
  * @param  value: 数值
  * @param  dec:   小数位数
  * @param  width: 显示总宽度（含符号/小数点，不足补空格，用于数值对齐）
  *                为 0 时不补空格（变长）
  */
void SSD1306_ShowFloat(uint16_t x, uint16_t y, float value, uint8_t dec, uint8_t width,
                       uint8_t size, uint8_t on);

/**
  * @brief  显示十六进制数，定长并前导补零（大写 A~F）
  * @param  value: 数值
  * @param  len:   十六进制位数（<=8）
  */
void SSD1306_ShowHex(uint16_t x, uint16_t y, uint32_t value, uint8_t len, uint8_t size, uint8_t on);

/*----------------------------- 位图显示 -------------------------------*/
/**
  * @brief  显示整幅位图（128x64，直接覆盖显存并刷屏）
  * @param  bmp: 1024 字节位图数据，布局与显存一致（页格式）：
  *              bmp[page*128 + x] 的 bit (y%8) 表示像素 (x, y)
  * @note   适合显示从 SD 卡读回的图片数据；绘制前会清屏
  */
void SSD1306_ShowBitmap(const uint8_t *bmp);

/**
  * @brief  从页格式位图拷贝一个区域到显存（支持负坐标裁剪、反色）
  * @param  bmp:    源位图（页格式：bmp[(y/8)*bmp_w + x] 的 bit (y%8)）
  * @param  bmp_w:  源位图宽度（每行字节数 = bmp_w）
  * @param  src_x, src_y: 源区域左上角
  * @param  w, h:   区域宽高
  * @param  dst_x, dst_y: 目标位置（像素，可负——越界部分自动裁剪）
  * @param  invert: 1=反色绘制（用于选中高亮）
  * @note   用于图标滑动等场景，目标超出屏幕的部分自动裁剪
  */
void SSD1306_ShowBitmapRegion(const uint8_t *bmp, uint16_t bmp_w,
                              uint16_t src_x, uint16_t src_y,
                              uint16_t w, uint16_t h,
                              int16_t dst_x, int16_t dst_y,
                              uint8_t invert);

#endif /* __SSD1306_H */
