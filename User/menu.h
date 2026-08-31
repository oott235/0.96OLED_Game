/**
  ******************************************************************************
  * @file    menu.h
  * @brief   整屏翻页菜单（双向链表 + 状态机 + sin 缓动动画 + 脏矩形）
  *
  *          设计：
  *            - 数据：菜单项用**双向链表**（menu_item_t，prev/next），循环翻页
  *            - 页面：每项一张**完整 128x64 页格式图片**（全屏显示，不裁剪）
  *            - 逻辑：状态机（静止 / 翻页动画中），LT 左翻 / RT 右翻，A 确认
  *            - 动画：SysTick 驱动 + easeInOutSine（sin 曲线）整屏滑动翻页
  *            - 渲染：双缓冲 + 脏矩形（整屏页 0..7）
  ******************************************************************************
  */

#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

/*========================= 菜单项（双向链表节点） ========================*/
typedef struct menu_item
{
    const char *name;               /* 名称（保留，页面模式不显示） */
    void (*action)(void);           /* 确认进入后执行的回调（A 键） */
    const uint8_t *page;            /* 128x64 页格式整幅图片（全屏页） */
    struct menu_item *prev;         /* 前一个节点 */
    struct menu_item *next;         /* 后一个节点 */
} menu_item_t;

/*============================= 公共接口 =================================*/

/**
  * @brief  运行整屏翻页菜单（阻塞，LT 左翻 / RT 右翻，循环，A 确认）
  * @param  items: 菜单项数组（每个必须有 page 指向 128x64 页格式位图）
  * @param  count: 菜单项数量
  * @param  hint:  底部提示文字（可为空字符串）
  */
void Menu_Run(menu_item_t *items, uint8_t count, const char *hint);

#endif /* __MENU_H */
