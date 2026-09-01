/**
  ******************************************************************************
  * @file    video.c
  * @brief   视频播放应用实现
  *
  *          视频分区：SD 卡根目录下的 VIDEO 子目录存放视频文件（.BIN）。
  *          相册只看根目录 .BIN，互不干扰（"做好分区"）。
  *
  *          文件格式：连续帧流，每帧 1024 字节（128x64 页格式，直接显示）。
  *          帧率约定：文件名前 2 位为数字即帧率，如 "18BADAPP.BIN" = 18fps；
  *          非数字前缀默认 18fps。
  *
  *          播放：按帧率节奏 f_read 1024 字节 -> SSD1306_ShowBitmap，
  *          双缓冲保证流畅；B 暂停/继续，X 退出，播完循环。
  ******************************************************************************
  */

#include "video.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"
#include "sd_dl.h"
#include "ff.h"
#include <stddef.h>

/*============================== 私有定义 ===============================*/

#define VIDEO_DIR       "0:/VIDEO"      /* 视频分区目录 */
#define VIDEO_PATH_PRE  "0:/VIDEO/"     /* 路径前缀 */
#define VIDEO_FRAME     1024            /* 每帧字节数（128x64 页格式） */
#define VIDEO_MAX       8               /* 最多列出的视频数 */
#define VIDEO_DEF_FPS   18              /* 默认帧率 */

/*============================== 私有变量 ===============================*/

static char    s_names[VIDEO_MAX][16];  /* 视频文件名 */
static uint8_t s_count;                 /* 视频数量 */
static uint8_t s_fps[VIDEO_MAX];        /* 各视频帧率 */

/* 播放缓冲：页格式帧（1024 字节）+ FatFS 文件对象 */
static uint8_t s_frame[VIDEO_FRAME];
static FIL     s_file;

/*============================== 文件枚举 ===============================*/

/**
  * @brief  判断是否为 .BIN 文件（不区分大小写）
  */
static uint8_t Video_IsBin(const char *name)
{
    const char *dot = NULL;

    while (*name)
    {
        if (*name == '.') dot = name;
        name++;
    }
    if (dot == NULL) return 0;

    return (dot[1] == 'B' || dot[1] == 'b') &&
           (dot[2] == 'I' || dot[2] == 'i') &&
           (dot[3] == 'N' || dot[3] == 'n') &&
           (dot[4] == '\0');
}

/**
  * @brief  从文件名解析帧率（前 2 位为数字）
  */
static uint8_t Video_ParseFps(const char *name)
{
    if (name[0] >= '0' && name[0] <= '9' &&
        name[1] >= '0' && name[1] <= '9')
    {
        uint8_t fps = (uint8_t)((name[0] - '0') * 10 + (name[1] - '0'));
        if (fps >= 1 && fps <= 60) return fps;
    }
    return VIDEO_DEF_FPS;
}

/**
  * @brief  枚举 VIDEO 目录下的视频文件
  * @retval 视频数量（0 = 无视频）
  */
static uint8_t Video_Scan(void)
{
    DIR     dir;
    FILINFO fno;
    uint8_t n = 0;

    s_count = 0;

    if (f_opendir(&dir, VIDEO_DIR) != FR_OK)
    {
        return 0;
    }

    while (n < VIDEO_MAX)
    {
        if (f_readdir(&dir, &fno) != FR_OK) break;
        if (fno.fname[0] == 0) break;               /* 目录结束 */
        if (fno.fattrib & AM_DIR) continue;         /* 跳过子目录 */
        if (!Video_IsBin(fno.fname)) continue;      /* 只要 .BIN */

        {
            uint8_t i;
            for (i = 0; i < 12 && fno.fname[i]; i++)
            {
                s_names[n][i] = fno.fname[i];
            }
            s_names[n][i] = '\0';
        }
        s_fps[n] = Video_ParseFps(fno.fname);
        n++;
    }

    f_closedir(&dir);
    s_count = n;
    return n;
}

/**
  * @brief  打开选中视频文件
  * @retval 0 成功；非 0 失败
  */
static uint8_t Video_Open(uint8_t idx)
{
    char    path[24];
    uint8_t i = 0, j = 0;
    FRESULT fr;

    /* 拼路径 "0:/VIDEO/NAME.BIN" */
    {
        static const char pre[] = VIDEO_PATH_PRE;
        while (pre[i] && i < 19) { path[i] = pre[i]; i++; }
    }
    while (s_names[idx][j] && i < 23) { path[i++] = s_names[idx][j++]; }
    path[i] = '\0';

    fr = f_open(&s_file, path, FA_READ);
    return (fr == FR_OK) ? 0 : 1;
}

/**
  * @brief  读取并显示一帧
  * @param  frame_no: 帧序号（从 0 开始；顺序播放时文件指针已连续）
  * @retval 0 成功；非 0 失败（文件结束/错误）
  */
static uint8_t Video_ShowFrame(uint32_t frame_no)
{
    UINT br;
    FRESULT fr;

    /* 顺序播放：每次 f_read 后文件指针已指向下一帧，无需 lseek；
       仅当需要跳帧/回绕时才用 lseek（循环播放回到 0 帧时） */
    if (frame_no == 0)
    {
        if (f_lseek(&s_file, 0) != FR_OK) return 1;
    }

    fr = f_read(&s_file, s_frame, VIDEO_FRAME, &br);
    if (fr != FR_OK || br != VIDEO_FRAME) return 1;

    SSD1306_ShowBitmap(s_frame);        /* 页格式直接整帧显示 */
    return 0;
}

/*============================== 列表界面 ===============================*/

/**
  * @brief  绘制视频列表（视频在上，DL 下载项固定在最底行）
  * @param  sel: 当前选中行号（0..s_count-1 = 视频，s_count = DL）
  */
static void Video_DrawList(uint8_t sel)
{
    uint8_t i, row;

    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 1, "VIDEO LIST", SSD1306_COLOR_ON);

    /* 视频列表（第 0 行起） */
    for (i = 0; i < s_count && i < 5; i++)
    {
        row = i;
        {
            uint16_t y = (uint16_t)(10 + row * 10);
            SSD1306_ShowString(0, y, 1, s_names[i], SSD1306_COLOR_ON);
            if (sel == row)
            {
                SSD1306_ShowString(120, y, 1, ">", SSD1306_COLOR_ON);
            }
        }
    }

    /* DL 下载项：永久固定最底行 */
    row = s_count;
    if (row > 5) row = 5;               /* 视频太多时 DL 固定在可见区最后一行 */
    {
        uint16_t y = (uint16_t)(10 + row * 10);
        SSD1306_ShowString(0, y, 1, "DL DOWNLOAD", SSD1306_COLOR_ON);
        if (sel == row)
        {
            SSD1306_ShowString(120, y, 1, ">", SSD1306_COLOR_ON);
        }
    }

    SSD1306_ShowString(0, 56, 1, "LT/RT A:ok Y:del", SSD1306_COLOR_ON);
    SSD1306_Display();
}

/**
  * @brief  弹出删除确认界面
  * @param  fname: 要删除的视频文件名
  * @retval 1 确认删除；0 取消
  */
static uint8_t Video_ConfirmDelete(const char *fname)
{
    char path[24];
    uint8_t i = 0, j = 0;
    uint8_t confirm = 0;

    /* 拼路径 "0:/VIDEO/NAME.BIN" */
    {
        static const char pre[] = VIDEO_PATH_PRE;
        while (pre[i] && i < 19) { path[i] = pre[i]; i++; }
    }
    while (fname[j] && i < 23) { path[i++] = fname[j++]; }
    path[i] = '\0';

    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "DELETE?", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, fname, SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 32, 1, "A:yes  X:no", SSD1306_COLOR_ON);
    SSD1306_Display();

    while (1)
    {
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            if (f_unlink(path) == FR_OK)
            {
                confirm = 1;
            }
            break;
        }
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;                      /* 取消 */
        }
        bsp_delay_ms(20);
    }

    if (confirm)
    {
        /* 显示删除成功 */
        SSD1306_Clear();
        SSD1306_ShowString(0, 0, 2, "DELETED", SSD1306_COLOR_ON);
        SSD1306_Display();
        bsp_delay_ms(800);
    }
    return confirm;
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行视频应用（阻塞）
  */
void Video_Run(void)
{
    uint8_t playing = 0;
    uint8_t paused = 0;
    uint32_t frame = 0;
    uint32_t last_tick = 0;
    uint32_t frame_ms;
    uint32_t total_frames;
    uint8_t  sel = 0;           /* 0..s_count-1 = 视频，s_count = DL */

    /* 列出视频 */
    Video_Scan();
    Video_DrawList(sel);

    while (1)
    {
        /* ---- 列表状态：LT/RT 移光标，A 播放/下载，Y 删除视频 ---- */
        if (!playing)
        {
            uint8_t rows = (uint8_t)(s_count + 1);   /* 视频 + DL */

            if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON)
            {
                sel = (uint8_t)((sel + rows - 1) % rows);
                Video_DrawList(sel);
            }
            else if (Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
            {
                sel = (uint8_t)((sel + 1) % rows);
                Video_DrawList(sel);
            }

            /* A：DL 进入下载；视频播放 */
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
            {
                if (sel == s_count)
                {
                    /* DL 下载：进入串口下载模式（video_to_bin.py --send-sd） */
                    SD_DL_Run();
                    Video_Scan();               /* 下载后可能新增了视频 */
                    if (sel > s_count) sel = s_count;
                    Video_DrawList(sel);
                }
                else
                {
                    if (Video_Open(sel) != 0)
                    {
                        SSD1306_Clear();
                        SSD1306_ShowString(0, 0, 1, "OPEN FAIL", SSD1306_COLOR_ON);
                        SSD1306_Display();
                    }
                    else
                    {
                        total_frames = (uint32_t)(f_size(&s_file) / VIDEO_FRAME);
                        frame_ms = 1000UL / s_fps[sel];
                        frame = 0;
                        paused = 0;
                        playing = 1;
                        last_tick = g_tick_ms;
                        (void)Video_ShowFrame(0);
                    }
                }
            }

            /* Y：删除选中视频（DL 项不可删） */
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_Y) && sel < s_count)
            {
                if (Video_ConfirmDelete(s_names[sel]))
                {
                    Video_Scan();               /* 删除后重新扫描 */
                    if (sel > s_count) sel = (uint8_t)(s_count ? s_count : 0);
                    Video_DrawList(sel);
                }
                else
                {
                    Video_DrawList(sel);        /* 取消：重绘列表 */
                }
            }

            if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
            {
                break;                      /* 返回主菜单 */
            }
        }
        /* ---- 播放状态：按帧率出帧，B 暂停，X 退出，Y 删除 ---- */
        else
        {
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_B))
            {
                paused = !paused;
                if (paused)
                {
                    SSD1306_ShowString(0, 56, 1, "PAUSED  B:go X:exit", SSD1306_COLOR_ON);
                    SSD1306_DisplayRange(7, 7);
                }
                last_tick = g_tick_ms;
            }

            if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
            {
                f_close(&s_file);
                playing = 0;
                Video_DrawList(sel);
                continue;
            }

            /* Y：播放中删除当前视频 */
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_Y))
            {
                f_close(&s_file);
                playing = 0;
                if (Video_ConfirmDelete(s_names[sel]))
                {
                    Video_Scan();
                    if (sel > s_count) sel = (uint8_t)(s_count ? s_count : 0);
                }
                Video_DrawList(sel);
                continue;
            }

            if (!paused && (uint32_t)(g_tick_ms - last_tick) >= frame_ms)
            {
                last_tick = g_tick_ms;
                frame++;
                if (frame >= total_frames)
                {
                    frame = 0;              /* 循环播放 */
                }
                if (Video_ShowFrame(frame) != 0)
                {
                    f_close(&s_file);
                    playing = 0;
                    Video_DrawList(sel);
                }
            }
        }

        bsp_delay_ms(10);
    }

    SSD1306_Clear();
    SSD1306_Display();
}
