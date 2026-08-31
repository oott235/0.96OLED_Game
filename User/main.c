/**
  ******************************************************************************
  * @file    main.c
  * @brief   0.96 寸 SSD1306 OLED（SPI）+ 无线手柄 + SD 卡 + 滚动菜单
  *
  *          开机流程：
  *            OLED 初始化 -> 手柄初始化 -> SD 卡初始化（FAT32 自动格式化）
  *            -> favicon 写入 SD（首次）-> 进入滚动菜单
  *
  *          菜单项：
  *            GAME   贪吃蛇（A/B/X 控制，见 gamesnake.h）
  *            ALBUM  相册（浏览 SD 卡 .BIN 图片，LT/RT 切换）
  *
  *          菜单操作：LT/RT 滚动选择，A 确认进入，X 返回
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "bsp_delay.h"
#include "ssd1306.h"
#include "gamesnake.h"
#include "gamedino.h"
#include "gamepad.h"
#include "sd_spi.h"
#include "ff.h"
#include "images.h"
#include "menu.h"
#include "album.h"
#include "video.h"
#include <stddef.h>   /* NULL */

/*
 * OLED 自检开关：
 *   1 = OLED 自检模式（显存几何图形 + 字符表循环，用于验证新屏）
 *   0 = 正常模式（滚动菜单）  <- 正常使用
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

/*=========================== 正常模式（滚动菜单） ========================*/

/* SD 卡文件系统对象（FatFS 挂载用） */
static FATFS s_sd_fs;
static uint8_t s_sd_ok;      /* 1 = SD 卡挂载成功 */

/* f_mkfs 工作缓冲（格式化用，需 >= 2*FF_MAX_SS；放全局避免爆栈） */
static uint8_t s_mkfs_work[2048];

/* SD 卡上的图片文件名（短文件名，FAT32） */
#define IMG_SNAKE_FILE   "0:SNAKE.BIN"
#define IMG_WHALE_FILE   "0:WHALE.BIN"
#define IMG_SCREEN_FILE  "0:SCREEN.BIN"

/* 页格式图片缓冲（SSD1306 显存格式，1024 字节） */
static uint8_t s_img_page[1024];

/* 128x64 页格式整幅页面缓冲（主页翻页菜单用，各 1024 字节） */
static uint8_t s_page_game[1024];
static uint8_t s_page_album[1024];
static uint8_t s_page_video[1024];

/**
  * @brief  初始化 SD 卡并挂载 FAT32 文件系统
  * @retval 0 成功；1=SD 初始化失败，2=挂载失败，3=格式化失败，4=格式化后挂载失败
  * @note   挂载失败（未格式化/非 FAT）时自动调用 f_mkfs 格式化为 FAT32，
  *         再重新挂载——无需电脑读卡器，单片机直接格式化 SD 卡。
  */
static uint8_t sd_fs_init(void)
{
    FRESULT fr;
    MKFS_PARM opt;

    if (SD_Init() != 0)
    {
        return 1;
    }

    /* 挂载 FAT32（立刻读取引导扇区） */
    fr = f_mount(&s_sd_fs, "0:", 1);
    if (fr == FR_OK)
    {
        return 0;
    }

    /* ---- 挂载失败：自动格式化为 FAT32 ---- */
    f_mount(NULL, "0:", 0);              /* 先卸载 */

    opt.fmt     = FM_FAT32;              /* 固定 FAT32 */
    opt.n_fat   = 0;                     /* 0 = 自动 */
    opt.align   = 0;                     /* 0 = 自动对齐 */
    opt.n_root  = 0;                     /* 0 = 自动 */
    opt.au_size = 0;                     /* 0 = 按容量自动选簇大小 */

    fr = f_mkfs("0:", &opt, s_mkfs_work, sizeof(s_mkfs_work));
    if (fr != FR_OK)
    {
        return 3;
    }

    /* 重新挂载 */
    fr = f_mount(&s_sd_fs, "0:", 1);
    if (fr != FR_OK)
    {
        return 4;
    }

    return 0;
}

/**
  * @brief  将水平逐行位图（images.h）转换成 SSD1306 页格式
  * @param  src: 水平格式源数据（1024 字节，bit7 = 最左）
  * @note   目标页格式：s_img_page[page*128 + x] 的 bit (y%8) 表示像素 (x,y)。
  */
static void img_to_page(const uint8_t *src)
{
    uint16_t i;
    uint8_t  y, x;

    for (i = 0; i < 1024; i++) s_img_page[i] = 0x00;

    for (y = 0; y < IMG_H; y++)
    {
        for (x = 0; x < IMG_W; x++)
        {
            uint8_t b = src[y * (IMG_W / 8) + (x / 8)];
            if (b & (0x80 >> (x % 8)))          /* bit7 = 最左 */
            {
                s_img_page[(y / 8) * SSD1306_WIDTH + x] |= (uint8_t)(1u << (y % 8));
            }
        }
    }
}

/**
  * @brief  把一张内置图片写入 SD 卡（若文件不存在）
  * @param  fname: 目标文件名（如 "0:SNAKE.BIN"）
  * @param  src:   水平格式源数据
  * @retval 0 成功（含已存在）；非 0 失败
  */
static uint8_t img_write_file(const char *fname, const uint8_t *src)
{
    FIL     f;
    UINT    bw;
    FILINFO finfo;
    FRESULT fr;

    /* 文件已存在则跳过 */
    if (f_stat(fname, &finfo) == FR_OK)
    {
        return 0;
    }

    img_to_page(src);

    fr = f_open(&f, fname, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        return 1;
    }
    fr = f_write(&f, s_img_page, 1024, &bw);
    f_close(&f);

    return ((fr == FR_OK) && (bw == 1024)) ? 0 : 2;
}

/**
  * @brief  开机时把三张内置图片写入 SD 卡（仅首次）
  */
static void img_write_all(void)
{
    (void)img_write_file(IMG_SNAKE_FILE,  s_img_snake);
    (void)img_write_file(IMG_WHALE_FILE,  s_img_whale);
    (void)img_write_file(IMG_SCREEN_FILE, s_img_screen);
}

/*======================== 软件选择列表（GAME 应用内） =====================*/

/* 可执行软件项：编号 + 名称 + 入口函数 */
typedef struct
{
    const char *name;               /* 显示名（如 "1.SNAKE"） */
    void (*action)(void);           /* 启动该软件 */
} app_item_t;

/* 软件列表（增加新游戏时在这里追加即可） */
static const app_item_t s_app_list[] =
{
    { "1.SNAKE",  Snake_Run },
    { "2.DINO",   Dino_Run },
};

#define APP_COUNT   (sizeof(s_app_list) / sizeof(s_app_list[0]))

/**
  * @brief  GAME 应用入口：文本软件选择列表（编号 + 行尾箭头 + 上下选择）
  * @note   不显示图片：每行 "1.SNAKE" 格式，选中行最右边显示 '>'，
  *         十字键上/下（或左摇杆）移动选择，A 启动，X 返回主菜单。
  */
static void App_Game(void)
{
    uint8_t sel = 0;
    uint8_t draw = 1;

    while (1)
    {
        if (draw)
        {
            uint8_t i;

            SSD1306_Clear();
            SSD1306_ShowString(0, 0, 1, "SOFTWARE", SSD1306_COLOR_ON);

            /* 列表：每行 "1.NAME" + 行尾箭头（8x8 字体，行高 10） */
            for (i = 0; i < APP_COUNT && i < 6; i++)
            {
                uint16_t y = (uint16_t)(10 + i * 10);
                SSD1306_ShowString(0, y, 1, s_app_list[i].name, SSD1306_COLOR_ON);
                if (i == sel)
                {
                    SSD1306_ShowString(120, y, 1, ">", SSD1306_COLOR_ON);  /* 行尾箭头 */
                }
            }

            SSD1306_ShowString(0, 56, 1, "LT/RT sel A:run", SSD1306_COLOR_ON);
            SSD1306_Display();
            draw = 0;
        }

        /* LT/RT 移动光标（边沿触发，避免连滚）：
           LT = 上一个软件（光标上移），RT = 下一个软件（光标下移） */
        if (Gamepad_GetLT() > 8000)
        {
            sel = (uint8_t)((sel + APP_COUNT - 1) % APP_COUNT);
            draw = 1;
        }
        else if (Gamepad_GetRT() > 8000)
        {
            sel = (uint8_t)((sel + 1) % APP_COUNT);
            draw = 1;
        }

        /* A：启动选中软件 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            if (s_app_list[sel].action != NULL)
            {
                s_app_list[sel].action();       /* 进入贪吃蛇；退出后回来 */
            }
            draw = 1;
        }

        /* X：返回主菜单 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        bsp_delay_ms(30);
    }

    SSD1306_Clear();
    SSD1306_Display();
}

#endif  /* OLED_SELFTEST */

int main(void)
{
    uint16_t i;
    bsp_delay_init();

#if OLED_SELFTEST
    SSD1306_Init();
    oled_selftest();                    /* 自检：不会返回 */
#else
    SSD1306_Init();
    Gamepad_Init();

    /* SD 卡初始化 + FAT32 挂载（显示进度） */
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "SD CARD INIT", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "wait...", SSD1306_COLOR_ON);
    SSD1306_Display();
    s_sd_ok = (sd_fs_init() == 0);

    /* SD 正常时把三张内置图片写入卡（仅首次），并创建视频分区目录 */
    if (s_sd_ok)
    {
        img_write_all();
        f_mkdir("0:/VIDEO");            /* 视频分区（已存在则返回 FR_EXIST，可忽略） */
    }

    /* 生成整幅页格式页面：GAME=蛇图，ALBUM=鲸鱼女孩图，VIDEO=cmd 终端图 */
    img_to_page(s_img_snake);
    for (i = 0; i < 1024; i++) s_page_game[i] = s_img_page[i];
    img_to_page(s_img_whale);
    for (i = 0; i < 1024; i++) s_page_album[i] = s_img_page[i];
    img_to_page(s_img_cmd);
    for (i = 0; i < 1024; i++) s_page_video[i] = s_img_page[i];

    /* 菜单项（双向链表由 menu 内部串联；page 为 128x64 页格式整幅图） */
    static menu_item_t s_menu_items[] =
    {
        { "GAME",  App_Game,  s_page_game,  NULL, NULL },
        { "ALBUM", Album_Run, s_page_album, NULL, NULL },
        { "VIDEO", Video_Run, s_page_video, NULL, NULL },
    };

    /* 进入整屏翻页菜单（阻塞，LT 左翻 / RT 右翻，A 确认） */
    Menu_Run(s_menu_items, (uint8_t)(sizeof(s_menu_items) / sizeof(s_menu_items[0])),
             "LT/RT  A:OK");

    /* 菜单退出后（当前不会退出） */
    while (1) { }
#endif  /* OLED_SELFTEST */
}
