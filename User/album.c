/**
  ******************************************************************************
  * @file    album.c
  * @brief   相册实现：扫描 SD 卡根目录 .BIN 图片并浏览
  *
  *          流程：
  *            1. 枚举根目录，收集扩展名为 .BIN 的文件名（最多 MAX_IMG 张）
  *            2. 显示第一张（读 1024 字节 -> SSD1306_ShowBitmap）
  *            3. LT/RT 切换上一张/下一张；A/X 返回菜单
  *
  *          图片要求：1024 字节页格式（favicon 写入 SD 的格式即此）
  ******************************************************************************
  */

#include "album.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"
#include "ff.h"
#include <stddef.h>

/*============================== 私有定义 ===============================*/

#define ALBUM_MAX_IMG    8       /* 最多支持的图片数量 */
#define ALBUM_IMG_SIZE   1024    /* 每张图片字节数（128x64/8） */

/*============================== 私有变量 ===============================*/

static char    s_names[ALBUM_MAX_IMG][16];   /* 图片文件名（短名，<=12 字符） */
static uint8_t s_count;                      /* 找到的图片数 */
static uint8_t s_index;                      /* 当前显示索引 */
static uint8_t s_buf[ALBUM_IMG_SIZE];        /* 图片数据缓冲 */

/*============================== 文件枚举 ===============================*/

/**
  * @brief  判断文件名是否为 .BIN（不区分大小写）
  */
static uint8_t Album_IsBin(const char *name)
{
    /* 找到最后一个 '.' */
    const char *dot = NULL;
    while (*name)
    {
        if (*name == '.') dot = name;
        name++;
    }
    if (dot == NULL) return 0;

    /* 比较扩展名（短名如 FAVICON.BIN） */
    return (dot[1] == 'B' || dot[1] == 'b') &&
           (dot[2] == 'I' || dot[2] == 'i') &&
           (dot[3] == 'N' || dot[3] == 'n') &&
           (dot[4] == '\0');
}

/**
  * @brief  枚举 SD 卡根目录，收集 .BIN 图片文件名
  * @retval 图片数量（0 = 无图片）
  */
static uint8_t Album_Scan(void)
{
    DIR     dir;
    FILINFO fno;
    uint8_t n = 0;

    s_count = 0;

    if (f_opendir(&dir, "0:/") != FR_OK)
    {
        return 0;
    }

    while (n < ALBUM_MAX_IMG)
    {
        if (f_readdir(&dir, &fno) != FR_OK) break;
        if (fno.fname[0] == 0) break;               /* 目录结束 */
        if (fno.fattrib & AM_DIR) continue;         /* 跳过子目录 */
        if (!Album_IsBin(fno.fname)) continue;      /* 只要 .BIN */

        /* 复制文件名（短名 8.3，最多 12 字符含点） */
        uint8_t i;
        for (i = 0; i < 12 && fno.fname[i]; i++)
        {
            s_names[n][i] = fno.fname[i];
        }
        s_names[n][i] = '\0';
        n++;
    }

    f_closedir(&dir);
    s_count = n;
    return n;
}

/**
  * @brief  从 SD 卡读取一张图片
  * @param  idx: 图片索引
  * @retval 0 成功；非 0 失败
  */
static uint8_t Album_Load(uint8_t idx)
{
    FIL     f;
    UINT    br;
    FRESULT fr;
    char    path[20];

    if (idx >= s_count) return 1;

    /* 拼路径 "0:/NAME.BIN" */
    path[0] = '0'; path[1] = ':'; path[2] = '/'; path[3] = '\0';
    /* 用 strcat 需 string.h；这里手写拼接避免依赖 */
    {
        uint8_t i = 3, j = 0;
        while (s_names[idx][j] && i < 19)
        {
            path[i++] = s_names[idx][j++];
        }
        path[i] = '\0';
    }

    fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) return 1;

    fr = f_read(&f, s_buf, ALBUM_IMG_SIZE, &br);
    f_close(&f);

    if (fr != FR_OK || br != ALBUM_IMG_SIZE) return 1;
    return 0;
}

/**
  * @brief  显示当前索引的图片
  */
static void Album_Show(void)
{
    if (Album_Load(s_index) != 0)
    {
        SSD1306_Clear();
        SSD1306_ShowString(0, 0, 1, "LOAD FAIL", SSD1306_COLOR_ON);
        SSD1306_Display();
        return;
    }
    SSD1306_ShowBitmap(s_buf);
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行相册（阻塞，直到 A/X 返回）
  */
void Album_Run(void)
{
    uint8_t show_scan_msg = 0;

    if (Album_Scan() == 0)
    {
        SSD1306_Clear();
        SSD1306_ShowString(0, 0, 2, "NO IMAGE", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 16, 1, "put .BIN files", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 24, 1, "on SD card", SSD1306_COLOR_ON);
        SSD1306_ShowString(0, 40, 1, "A/X to back", SSD1306_COLOR_ON);
        SSD1306_Display();
    }
    else
    {
        s_index = 0;
        Album_Show();
        show_scan_msg = 1;
    }

    while (1)
    {
        /* A/X 返回菜单 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A) ||
            Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        if (s_count > 0)
        {
            /* RT：下一张；LT：上一张（扳机按下判定） */
            if (Gamepad_GetRT() > 8000)
            {
                s_index = (uint8_t)((s_index + 1) % s_count);
                Album_Show();
            }
            else if (Gamepad_GetLT() > 8000)
            {
                s_index = (uint8_t)((s_index + s_count - 1) % s_count);
                Album_Show();
            }

            /* 无图片时显示的扫描提示不参与切换 */
            (void)show_scan_msg;
        }

        bsp_delay_ms(30);
    }

    /* 返回前清屏，交还菜单重绘 */
    SSD1306_Clear();
    SSD1306_Display();
}
