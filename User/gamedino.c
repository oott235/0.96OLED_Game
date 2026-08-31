/**
  ******************************************************************************
  * @file    gamedino.c
  * @brief   小恐龙跑酷游戏实现（像素坐标，非格子）
  *
  *          布局（128x64，像素）：
  *            顶部 y=0..7    分数行 "SCORE xxxx"
  *            地面 y=56      恐龙与仙人掌的站立线
  *            恐龙           x=16 附近，重力跳跃（脚底相对地面 s_dino_y）
  *            仙人掌         从右向左滚动，随机高度/间距
  *
  *          节奏：由 SysTick（g_tick_ms）驱动，每 DINO_TICK_MS 步进一次，
  *          障碍速度 = 基础速度 + 分数/200（越玩越快）。
  ******************************************************************************
  */

#include "gamedino.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"

/*============================== 私有定义 ===============================*/

#define DINO_TICK_MS    20      /* 游戏步进周期（ms） */
#define DINO_GROUND_Y   56      /* 地面线 y（恐龙脚底与仙人掌底部） */
#define DINO_X          16      /* 恐龙左缘 x */
#define DINO_W          10      /* 恐龙碰撞盒宽 */
#define DINO_H          16      /* 恐龙碰撞盒高（脚底向上） */

#define DINO_GRAVITY    2       /* 每 tick 重力增量 */
#define DINO_JUMP_V     -14     /* 起跳初速度（向上为负） */

#define OBS_MAX         4       /* 同时最多障碍数 */
#define OBS_W           4       /* 仙人掌宽 */

/*============================== 私有变量 ===============================*/

static int16_t  s_dino_y;       /* 恐龙脚底相对地面的高度（0=地面） */
static int16_t  s_dino_vy;      /* 垂直速度 */
static uint8_t  s_jumping;      /* 1 = 在空中 */
static uint32_t s_score;        /* 分数 */
static uint8_t  s_over;         /* 1 = 游戏结束 */

typedef struct
{
    int16_t x;                  /* 左缘 x */
    uint8_t h;                  /* 高度 */
} obs_t;
static obs_t s_obs[OBS_MAX];
static uint8_t s_obs_cnt;
static int16_t s_spawn_wait;    /* 生成下一个障碍的 x 间隔倒计时 */

/*============================== 初始化 =================================*/

static void Dino_GameInit(void)
{
    uint8_t i;

    s_dino_y = 0;
    s_dino_vy = 0;
    s_jumping = 0;
    s_score = 0;
    s_over = 0;
    s_obs_cnt = 0;
    s_spawn_wait = 90;          /* 首个障碍稍后出现 */

    for (i = 0; i < OBS_MAX; i++)
    {
        s_obs[i].x = 0;
        s_obs[i].h = 0;
    }
}

/*============================== 物理与逻辑 =============================*/

/**
  * @brief  处理跳跃输入
  */
static void Dino_Input(void)
{
    /* A 或 十字键上：起跳（仅在地面） */
    if ((Gamepad_ButtonPressed(GAMEPAD_BTN_A) ||
         (Gamepad_GetDPad() & GAMEPAD_DPAD_UP)) && !s_jumping)
    {
        s_dino_vy = DINO_JUMP_V;
        s_jumping = 1;
    }
}

/**
  * @brief  更新恐龙垂直物理
  */
static void Dino_Physics(void)
{
    if (!s_jumping) return;

    s_dino_vy += DINO_GRAVITY;
    s_dino_y  += s_dino_vy;

    if (s_dino_y <= 0)          /* 落地 */
    {
        s_dino_y = 0;
        s_dino_vy = 0;
        s_jumping = 0;
    }
}

/**
  * @brief  生成新障碍（在右侧边缘外）
  */
static void Dino_Spawn(void)
{
    if (s_obs_cnt >= OBS_MAX) return;

    s_obs[s_obs_cnt].x = SSD1306_WIDTH;
    s_obs[s_obs_cnt].h = (uint8_t)(6 + (s_score % 7));   /* 高度 6..12 */
    s_obs_cnt++;

    /* 下一个间隔随机（随分数变密） */
    s_spawn_wait = (int16_t)(50 - (s_score / 400));
    if (s_spawn_wait < 30) s_spawn_wait = 30;
    s_spawn_wait += (int16_t)(s_score % 40);
}

/**
  * @brief  更新障碍位置与生成
  */
static void Dino_Obstacles(void)
{
    uint8_t i;
    int16_t speed = 3 + (int16_t)(s_score / 200);   /* 越玩越快 */
    if (speed > 8) speed = 8;

    /* 移动现有障碍 */
    for (i = 0; i < s_obs_cnt; i++)
    {
        s_obs[i].x -= speed;
    }

    /* 移除超出左侧的 */
    i = 0;
    while (i < s_obs_cnt)
    {
        if (s_obs[i].x + OBS_W < 0)
        {
            s_obs[i] = s_obs[s_obs_cnt - 1];
            s_obs_cnt--;
        }
        else
        {
            i++;
        }
    }

    /* 按间隔生成新障碍 */
    s_spawn_wait -= speed;
    if (s_spawn_wait <= 0)
    {
        Dino_Spawn();
    }
}

/**
  * @brief  碰撞检测（恐龙碰撞盒 vs 每个仙人掌）
  * @retval 1 撞到
  */
static uint8_t Dino_Collide(void)
{
    uint8_t i;
    int16_t d_top = DINO_GROUND_Y - DINO_H + s_dino_y;   /* 恐龙顶部 y */
    int16_t d_bot = DINO_GROUND_Y + s_dino_y;            /* 恐龙底部 y */

    for (i = 0; i < s_obs_cnt; i++)
    {
        int16_t o_top = DINO_GROUND_Y - s_obs[i].h;
        int16_t o_bot = DINO_GROUND_Y;

        if (s_obs[i].x < DINO_X + DINO_W &&     /* 水平重叠 */
            s_obs[i].x + OBS_W > DINO_X &&
            d_bot > o_top &&                    /* 垂直重叠 */
            d_top < o_bot)
        {
            return 1;
        }
    }
    return 0;
}

/*============================== 绘制 ===================================*/

/**
  * @brief  画一只小恐龙（像素组合，带跑动腿）
  */
static void Dino_DrawDino(void)
{
    int16_t foot = DINO_GROUND_Y - s_dino_y;    /* 脚底 y */
    int16_t top  = foot - 12;                   /* 身体顶 y */
    uint8_t leg  = (uint8_t)((s_score / 6) & 1); /* 跑步动画相位 */

    /* 身体 */
    SSD1306_FillRect(DINO_X, top + 4, DINO_X + 5, top + 11, SSD1306_COLOR_ON);
    /* 头 */
    SSD1306_FillRect(DINO_X + 4, top, DINO_X + 8, top + 5, SSD1306_COLOR_ON);
    /* 眼睛 */
    SSD1306_DrawPixel(DINO_X + 7, top + 1, SSD1306_COLOR_OFF);
    /* 尾巴 */
    SSD1306_DrawPixel(DINO_X - 1, top + 6, SSD1306_COLOR_ON);
    /* 腿（交替前后） */
    if (leg)
    {
        SSD1306_DrawPixel(DINO_X,     foot - 1, SSD1306_COLOR_ON);
        SSD1306_DrawPixel(DINO_X + 5, foot - 2, SSD1306_COLOR_ON);
    }
    else
    {
        SSD1306_DrawPixel(DINO_X,     foot - 2, SSD1306_COLOR_ON);
        SSD1306_DrawPixel(DINO_X + 5, foot - 1, SSD1306_COLOR_ON);
    }
}

/**
  * @brief  画一株仙人掌
  */
static void Dino_DrawObs(const obs_t *o)
{
    int16_t top = DINO_GROUND_Y - o->h;

    SSD1306_FillRect((uint16_t)o->x, (uint16_t)top,
                     (uint16_t)(o->x + OBS_W - 1), (uint16_t)(DINO_GROUND_Y - 1),
                     SSD1306_COLOR_ON);
}

/**
  * @brief  绘制一帧
  */
static void Dino_Draw(void)
{
    uint8_t i;

    SSD1306_Clear();

    /* 分数行 */
    SSD1306_ShowString(0, 0, 1, "SCORE", SSD1306_COLOR_ON);
    SSD1306_ShowNum(40, 0, s_score, 5, 1, SSD1306_COLOR_ON);

    /* 地面线 */
    SSD1306_DrawLine(0, DINO_GROUND_Y, SSD1306_WIDTH - 1, DINO_GROUND_Y, SSD1306_COLOR_ON);

    /* 恐龙 */
    Dino_DrawDino();

    /* 仙人掌 */
    for (i = 0; i < s_obs_cnt; i++)
    {
        Dino_DrawObs(&s_obs[i]);
    }

    SSD1306_Display();
}

/**
  * @brief  显示 GAME OVER 画面
  */
static void Dino_DrawGameOver(void)
{
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "GAME OVER", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "SCORE:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(48, 16, s_score, 5, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 32, 1, "A:retry X:exit", SSD1306_COLOR_ON);
    SSD1306_Display();
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行小恐龙游戏（阻塞）
  */
void Dino_Run(void)
{
    uint32_t last_tick;

    Dino_GameInit();
    Dino_Draw();
    last_tick = g_tick_ms;

    while (1)
    {
        /* X 退出 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        /* B 暂停 / 继续 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_B))
        {
            SSD1306_Clear();
            SSD1306_ShowString(0, 0, 2, "PAUSED", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 16, 1, "B:go X:exit", SSD1306_COLOR_ON);
            SSD1306_Display();
            while (1)
            {
                if (Gamepad_ButtonPressed(GAMEPAD_BTN_B)) break;   /* 继续 */
                if (Gamepad_ButtonPressed(GAMEPAD_BTN_X)) break;   /* 退出 */
                bsp_delay_ms(20);
            }
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_X)) break;
            last_tick = g_tick_ms;
            Dino_Draw();
        }

        /* 节奏步进 */
        if ((uint32_t)(g_tick_ms - last_tick) >= DINO_TICK_MS)
        {
            last_tick = g_tick_ms;

            if (!s_over)
            {
                Dino_Input();
                Dino_Physics();
                Dino_Obstacles();
                if (Dino_Collide())
                {
                    s_over = 1;
                }
                else
                {
                    s_score++;
                }
                Dino_Draw();
            }
        }

        /* 游戏结束：A 重开 / X 退出 */
        if (s_over)
        {
            Dino_DrawGameOver();
            while (1)
            {
                if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
                {
                    Dino_GameInit();
                    last_tick = g_tick_ms;
                    Dino_Draw();
                    break;
                }
                if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
                {
                    SSD1306_Clear();
                    SSD1306_Display();
                    return;
                }
                bsp_delay_ms(20);
            }
        }

        bsp_delay_ms(10);
    }

    /* 退出：清屏交还软件列表 */
    SSD1306_Clear();
    SSD1306_Display();
}
