/**
  ******************************************************************************
  * @file    main.c
  * @brief   0.96 寸 SSD1306 OLED + 无线手柄贪吃蛇
  *
  *          流程：开机画面 -> 手柄初始化 -> 主循环等待手柄消息
  *                （实时显示手柄连接状态，A 键进入贪吃蛇）
  *
  *          屏幕布局（128x64，8x8 字体，一行 16 字符）：
  *            行0 y= 0  STM32F103C8T6      （标题，8x16 字体占两行）
  *            行2 y=16  0.96 OLED SSD1306  （机型标注行）
  *            行3 y=24  PAD: OK / PAD: WAIT （手柄连接状态，实时刷新）
  *            行6 y=48  A:SNAKE            （操作提示：A 键进入游戏）
  *
  *          注意：PA13/PA14 保留给 SWD 调试，本程序未使用。
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "bsp_delay.h"
#include "ssd1306.h"
#include "gamesnake.h"
#include "gamepad.h"

/*
 * OLED 自检开关：
 *   1 = OLED 自检模式（显存几何图形 + 字符表循环，用于验证新屏）
 *   0 = 正常模式（无线手柄贪吃蛇）  <- 正常使用
 */
#define OLED_SELFTEST   0

#if OLED_SELFTEST

/*============================ OLED 自检模式 ==============================*/
/* 显存几何图形 + 字符表，全程仅依赖 ssd1306 驱动 */

static void oled_selftest(void)
{
    uint16_t x;

    for (;;)
    {
        /* 1) 边框 + 对角线 */
        SSD1306_Clear();
        SSD1306_DrawRect(0, 0, SSD1306_WIDTH - 1, SSD1306_HEIGHT - 1, SSD1306_COLOR_ON);
        SSD1306_DrawLine(0, 0, SSD1306_WIDTH - 1, SSD1306_HEIGHT - 1, SSD1306_COLOR_ON);
        SSD1306_DrawLine(SSD1306_WIDTH - 1, 0, 0, SSD1306_HEIGHT - 1, SSD1306_COLOR_ON);
        SSD1306_Display();
        bsp_delay_ms(1500);

        /* 2) 实心圆 + 圆环 */
        SSD1306_Clear();
        SSD1306_FillCircle(32, 32, 20, SSD1306_COLOR_ON);
        SSD1306_DrawCircle(96, 32, 24, SSD1306_COLOR_ON);
        SSD1306_Display();
        bsp_delay_ms(1500);

        /* 3) 字符表：size 1 与 size 2 各一屏 */
        SSD1306_Clear();
        SSD1306_ShowString(0, 0, 2, "0123456789", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 16, 1, "ABCDEFGHIJKLMNOP", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 24, 1, "abcdefghijklmno", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 32, 1, "!\"#$%&'()*+,-./", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 40, 1, ":;<=>?@[\\]^_`", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 48, 1, "{|}~ 8x8 OK", SSD1306_COLOR_ON);
        SSD1306_Display();
        bsp_delay_ms(2000);

        /* 4) 水平扫描线（逐列点亮） */
        SSD1306_Clear();
        for (x = 0; x < SSD1306_WIDTH; x++)
        {
            SSD1306_DrawPixel(x, 32, SSD1306_COLOR_ON);
            if ((x & 0x07) == 0) SSD1306_Display();   /* 每 8 列刷一次，看扫描过程 */
        }
        bsp_delay_ms(1000);
    }
}

#else   /* OLED_SELFTEST */

/*=========================== 正常模式（手柄贪吃蛇） =======================*/

/**
  * @brief  绘制主界面（标题 / 机型 / 手柄连接状态 / 操作提示）
  * @param  pad_ok: 1 = 已收到手柄帧，0 = 未连接
  */
static void draw_main_screen(uint8_t pad_ok)
{
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "STM32F103C8T6", SSD1306_COLOR_ON);   /* 8x16，占 0..15 */
    SSD1306_ShowString(0, 16, 1, "0.96 OLED SSD1306", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 24, 1, pad_ok ? "PAD: OK    " : "PAD: WAIT  ", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 48, 1, "A:SNAKE", SSD1306_COLOR_ON);
    SSD1306_Display();
}

#endif  /* OLED_SELFTEST */

int main(void)
{
    bsp_delay_init();

#if OLED_SELFTEST
    SSD1306_Init();
    oled_selftest();                    /* 自检：不会返回 */
#else
    SSD1306_Init();
    Gamepad_Init();

    draw_main_screen(Gamepad_Connected());

    /* 主循环：从"是否收到手柄消息"开始工作 */
    while (1)
    {
        /* 手柄连接状态实时刷新（收到过有效帧即 OK） */
        draw_main_screen(Gamepad_Connected());

        /* A 键进入贪吃蛇游戏；退出后重绘主界面 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            Snake_Run();
            draw_main_screen(Gamepad_Connected());
        }

        bsp_delay_ms(50);
    }
#endif  /* OLED_SELFTEST */
}
