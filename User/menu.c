/**
  ******************************************************************************
  * @file    menu.c
  * @brief   整屏翻页菜单实现
  *
  *          每页显示一张完整 128x64 图片（全屏）。
  *          LT = 左翻（新页从左侧滑入，旧页向右滑出）
  *          RT = 右翻（新页从右侧滑入，旧页向左滑出）
  *          首尾循环，sin 缓动（easeInOutSine），200ms 完成。
  ******************************************************************************
  */

#include "menu.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"
#include <stddef.h>
#include <math.h>

/*============================== 私有定义 ===============================*/

#define MENU_PAGE_W         128     /* 页面宽 = 屏幕宽 */
#define MENU_PAGE_H         64      /* 页面高 = 屏幕高 */
#define MENU_ANIM_MS        200     /* 翻页动画时长（ms） */
#define MENU_SLIDE_DIST     128     /* 滑动距离 = 一屏宽 */

#define MENU_DIR_NONE       0
#define MENU_DIR_NEXT       1       /* RT：右翻（新页从右滑入） */
#define MENU_DIR_PREV       2       /* LT：左翻（新页从左滑入） */

/*============================== 私有变量 ===============================*/

static menu_item_t *s_items;
static uint8_t       s_count;
static uint8_t       s_sel;             /* 当前页索引 */
static uint8_t       s_target;          /* 目标页索引（动画中） */
static uint8_t       s_dir;             /* 翻页方向 */
static uint8_t       s_state;           /* 0=静止 1=翻页动画中 */
static int16_t       s_anim_offset;     /* 页面平移量（像素） */
static uint32_t      s_anim_start;      /* 动画开始 tick */

/*============================== 缓动函数 ===============================*/

/**
  * @brief  easeInOutSine 缓动：t=0..1 -> 0..1（sin 曲线，两端平滑）
  */
static float Menu_EaseInOutSine(float t)
{
    return 0.5f - 0.5f * cosf((float)3.14159265f * t);
}

/*============================== 链表操作 ===============================*/

/**
  * @brief  把数组项串成循环双向链表
  */
static void Menu_BuildList(menu_item_t *items, uint8_t count)
{
    uint8_t i;

    for (i = 0; i < count; i++)
    {
        items[i].prev = &items[(i + count - 1) % count];
        items[i].next = &items[(i + 1) % count];
    }
}

/*============================== 渲染 ===================================*/

/**
  * @brief  在指定 x 位置绘制一页（完整 128x64 图片，负坐标自动裁剪）
  * @param  idx: 页索引
  * @param  x:   页面左缘 x
  */
static void Menu_DrawPageAt(uint8_t idx, int16_t x)
{
    if (s_items[idx].page == NULL) return;

    SSD1306_ShowBitmapRegion(s_items[idx].page, MENU_PAGE_W,
                             0, 0, MENU_PAGE_W, MENU_PAGE_H,
                             x, 0, 0);
}

/**
  * @brief  重绘整屏（静止页 + 翻页过渡）
  */
static void Menu_DrawList(void)
{
    int16_t cur_x, nxt_x;

    SSD1306_Clear();

    if (s_state == 0)
    {
        /* 静止：只画当前页 */
        Menu_DrawPageAt(s_sel, 0);
    }
    else
    {
        /* 翻页：旧页从输入方向滑出，新页从对侧滑入 */
        if (s_dir == MENU_DIR_NEXT)      /* RT 右翻：旧页向左出(0->-128)，新页从右入(128->0) */
        {
            cur_x = (int16_t)(0 - s_anim_offset);           /* 0 -> -128 */
            nxt_x = (int16_t)(MENU_PAGE_W - s_anim_offset); /* 128 -> 0 */
        }
        else                             /* LT 左翻：旧页向右出(0->128)，新页从左入(-128->0) */
        {
            cur_x = (int16_t)(0 + s_anim_offset);            /* 0 -> 128 */
            nxt_x = (int16_t)(-MENU_PAGE_W + s_anim_offset); /* -128 -> 0 */
        }
        Menu_DrawPageAt(s_sel,    cur_x);
        Menu_DrawPageAt(s_target, nxt_x);
    }

    /* 底部应用名（仅名字，水平居中） */
    if (s_items[s_sel].name != NULL && s_items[s_sel].name[0])
    {
        uint8_t len = 0;
        while (s_items[s_sel].name[len] && len < 16) len++;
        SSD1306_ShowString((uint16_t)((MENU_PAGE_W - len * 8) / 2), 56, 1,
                           s_items[s_sel].name, SSD1306_COLOR_ON);
    }

    /* 整屏刷新（双缓冲脏矩形：页 0..7） */
    SSD1306_DisplayRange(0, 7);
}

/*============================== 动画更新 ===============================*/

/**
  * @brief  按 SysTick 推进翻页动画
  * @retval 1 动画进行中；0 动画结束
  */
static uint8_t Menu_UpdateAnim(void)
{
    uint32_t elapsed;
    float k;

    elapsed = g_tick_ms - s_anim_start;
    if (elapsed >= MENU_ANIM_MS)
    {
        s_sel = s_target;               /* 动画结束：确认新页 */
        s_anim_offset = 0;
        s_state = 0;
        s_dir = MENU_DIR_NONE;
        return 0;
    }

    /* sin 缓动：平移量 0 -> MENU_SLIDE_DIST */
    k = Menu_EaseInOutSine((float)elapsed / (float)MENU_ANIM_MS);
    s_anim_offset = (int16_t)((float)MENU_SLIDE_DIST * k);

    return 1;
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行整屏翻页菜单（阻塞）
  */
void Menu_Run(menu_item_t *items, uint8_t count, const char *hint)
{
    (void)hint;                          /* 提示行由 menu 内部自动拼软件名，忽略参数 */
    if (items == NULL || count == 0) return;

    s_items = items;
    s_count = count;
    s_sel = 0;
    s_target = 0;
    s_dir = MENU_DIR_NONE;
    s_state = 0;
    s_anim_offset = 0;

    Menu_BuildList(items, count);
    Menu_DrawList();

    while (1)
    {
        /* ---- 状态机：静止态可响应翻页 ---- */
        if (s_state == 0)
        {
            /* RT：右翻（新页从右滑入）；LT：左翻（新页从左滑入） */
            if (Gamepad_GetRT() > 8000)
            {
                s_dir = MENU_DIR_NEXT;
                s_target = (uint8_t)((s_sel + 1) % s_count);   /* 循环 */
                s_anim_start = g_tick_ms;
                s_state = 1;
            }
            else if (Gamepad_GetLT() > 8000)
            {
                s_dir = MENU_DIR_PREV;
                s_target = (uint8_t)((s_sel + s_count - 1) % s_count);  /* 循环 */
                s_anim_start = g_tick_ms;
                s_state = 1;
            }
        }
        else
        {
            /* 动画态：推进动画并逐帧重绘 */
            Menu_UpdateAnim();
            Menu_DrawList();
        }

        /* ---- A 键确认（静止态） ---- */
        if (s_state == 0 && Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            if (s_items[s_sel].action != NULL)
            {
                s_items[s_sel].action();        /* 进入游戏/相册；返回后回菜单 */
            }
            Menu_DrawList();
        }

        bsp_delay_ms(10);
    }
}
