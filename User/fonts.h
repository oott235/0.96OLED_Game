/**
  ******************************************************************************
  * @file    fonts.h
  * @brief   ASCII bitmap font tables for the ILI9341 LCD driver.
  *
  *          font_8x16 : 8x16 VGA ROM font (public domain IBM VGA ROM bitmap),
  *                      mirrored in the Linux kernel lib/fonts/font_8x16.c.
  *          font_8x8  : 8x8 font (public domain, based on IBM VGA fonts),
  *                      font8x8_basic by Daniel Hepper <daniel@hepper.net>.
  *
  *          Both tables are indexed by [ch - 0x20] and cover ASCII 0x20..0x7E
  *          (95 printable characters). Row 0 is the top scan line.
  *
  *          IMPORTANT - bit order differs between the two tables:
  *            font_8x16 (IBM VGA ROM) is MSB-first:  bit 7 = left-most pixel.
  *            font_8x8  (font8x8_basic) is LSB-first: bit 0 = left-most pixel.
  *          Drivers must honour each font's own bit order when rendering,
  *          otherwise 8x8 glyphs appear mirrored left-right.
  ******************************************************************************
  */

#ifndef __FONTS_H
#define __FONTS_H

#include "stm32f10x.h"

/* 8x16 font: 95 glyphs, 16 rows each */
extern const uint8_t font_8x16[95][16];
#define FONT_8X16_WIDTH   8
#define FONT_8X16_HEIGHT  16

/* 8x8 font: 95 glyphs, 8 rows each */
extern const uint8_t font_8x8[95][8];
#define FONT_8X8_WIDTH    8
#define FONT_8X8_HEIGHT   8

#endif /* __FONTS_H */
