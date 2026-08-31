/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   SSD1306 OLED 驱动（128x64，I2C 四线）
  *
  *          -----------------------------------------------
  *          屏幕引脚               STM32F103C8T6
  *          -----------------------------------------------
  *          VCC                    3.3V
  *          GND                    GND
  *          SCL                    PB6（软件 I2C 时钟）
  *          SDA                    PB7（软件 I2C 数据）
  *          -----------------------------------------------
  *
  *          注 1: 使用软件 I2C（GPIO 模拟）而非硬件 I2C1。
  *                STM32F103 硬件 I2C 存在众所周知的 busy 位卡死问题，
  *                软件模拟对 SSD1306 这种纯写从机更稳定，且引脚可任意改。
  *                默认 400kHz（快模式），杜邦线/长线不稳时可把
  *                SSD1306_I2C_HALF_PERIOD_US 调大降速。
  *          注 2: 驱动采用全屏显存（128x64/8 = 1KB，SRAM 20KB 足够）。
  *                所有绘制先写显存，再调用 SSD1306_Display() 一次性刷屏。
  *          注 3: I2C 地址 7 位为 0x3C（SA0=GND，常见默认）；若模块 SA0
  *                接 VCC，地址为 0x3D，改 SSD1306_I2C_ADDR 即可。
  ******************************************************************************
  */

#ifndef __SSD1306_H
#define __SSD1306_H

#include "stm32f10x.h"

/*============================== 引脚定义 ==============================*/
#define SSD1306_SCL_GPIO     GPIOB
#define SSD1306_SCL_PIN      GPIO_Pin_6
#define SSD1306_SDA_GPIO     GPIOB
#define SSD1306_SDA_PIN      GPIO_Pin_7

/* I2C 从机地址（7 位）与单周期半时长。
   SSD1306 I2C 上限 400kHz：半周期 2us = 250kHz（默认，杜邦线稳）；
   半周期 1us = 500kHz 略超规格，短焊线场景可试；调大降速（如 5 = 100kHz） */
#define SSD1306_I2C_ADDR            0x3C    /* SA0=GND；SA0=VCC 时为 0x3D */
#define SSD1306_I2C_HALF_PERIOD_US  2       /* 2us => 250kHz；调大降速（如 5 = 100kHz） */

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
  * @brief  初始化 OLED：GPIO（软件 I2C）、SSD1306 初始化序列、清屏
  */
void SSD1306_Init(void);

/**
  * @brief  整屏清空（写显存，需调用 SSD1306_Display() 生效）
  */
void SSD1306_Clear(void);

/**
  * @brief  将显存一次性刷到屏幕（整屏 1024 字节，I2C 连续写）
  */
void SSD1306_Display(void);

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

#endif /* __SSD1306_H */
