/**
  ******************************************************************************
  * @file    gamesnake.c
  * @brief   贪吃蛇小游戏实现（无线手柄控制）
  *
  *          方向约定：
  *            0=上  1=右  2=下  3=左
  *          手柄输入（见 gamepad.h）：
  *            十字键 / 左摇杆 -> 直接设定移动方向（禁止 180° 反向）
  *            A 按键 -> 开始 / 确定（起始画面与 GAME OVER 画面）
  *            B 按键 -> 暂停 / 继续
  *            X 按键 -> 退出（返回主界面）
  ******************************************************************************
  */

#include "gamesnake.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"

/*============================== 私有定义 ===============================*/

#define SNAKE_MAX_LEN   (SNAKE_GRID_COLS * SNAKE_GRID_ROWS)

/* 蛇身起点（第 2 行，中央偏左，长度 3，向右） */
#define SNAKE_START_X   4
#define SNAKE_START_Y   2
#define SNAKE_START_LEN 3

/*============================== 私有变量 ===============================*/

static uint8_t s_snake_x[SNAKE_MAX_LEN];
static uint8_t s_snake_y[SNAKE_MAX_LEN];
static uint16_t s_len;
static uint8_t s_dir;              /* 0上 1右 2下 3左 */
static uint8_t s_food_x, s_food_y;
static uint32_t s_score;
static uint8_t s_game_over;

/*============================== 游戏逻辑 ===============================*/

/**
  * @brief  随机生成食物（不与蛇身重叠）
  */
static void SpawnFood(void)
{
    uint8_t i, overlap;
    uint8_t tries = 0;

    do
    {
        /* 用系统 tick 做简单随机源 */
        s_food_x = (uint8_t)((g_tick_ms + tries * 7) % SNAKE_GRID_COLS);
        s_food_y = (uint8_t)((g_tick_ms / 13 + tries * 3) % SNAKE_GRID_ROWS);
        tries++;

        overlap = 0;
        for (i = 0; i < s_len; i++)
        {
            if (s_snake_x[i] == s_food_x && s_snake_y[i] == s_food_y)
            {
                overlap = 1;
                break;
            }
        }
    } while (overlap && tries < 200);
}

/**
  * @brief  初始化一局
  */
static void GameInit(void)
{
    uint8_t i;

    s_len = SNAKE_START_LEN;
    s_dir = 1;                            /* 向右 */
    s_score = 0;
    s_game_over = 0;

    for (i = 0; i < s_len; i++)
    {
        s_snake_x[i] = SNAKE_START_X - i; /* 头在右，身体向左排 */
        s_snake_y[i] = SNAKE_START_Y;
    }
    SpawnFood();
}

/**
  * @brief  根据手柄方向更新移动方向（禁止 180° 反向）
  * @param  dir: 0上 1右 2下 3左
  */
static void SetDir(uint8_t dir)
{
    /* 蛇长 >1 时禁止掉头（与当前方向相反） */
    if (s_len > 1 && (uint8_t)((dir + 2) % 4) == s_dir)
    {
        return;
    }
    s_dir = dir;
}

/**
  * @brief  移动蛇一步（返回后由调用方检查 game over）
  */
static void GameStep(void)
{
    uint8_t nx, ny, i;
    uint8_t eaten = 0;
    uint8_t body_len;
    uint8_t tail_x, tail_y;

    /* 新头位置（无边界：穿出屏幕一侧，从对侧穿入） */
    nx = s_snake_x[0];
    ny = s_snake_y[0];
    switch (s_dir)
    {
        case 0: ny = (ny == 0)                  ? (uint8_t)(SNAKE_GRID_ROWS - 1) : (uint8_t)(ny - 1); break;  /* 上 -> 底部穿入 */
        case 1: nx = (nx == SNAKE_GRID_COLS - 1)? 0 : (uint8_t)(nx + 1); break;                              /* 右 -> 左侧穿入 */
        case 2: ny = (ny == SNAKE_GRID_ROWS - 1)? 0 : (uint8_t)(ny + 1); break;                              /* 下 -> 顶部穿入 */
        default:nx = (nx == 0)                  ? (uint8_t)(SNAKE_GRID_COLS - 1) : (uint8_t)(nx - 1); break;  /* 左 -> 右侧穿入 */
    }

    /* 吃到食物？ */
    if (nx == s_food_x && ny == s_food_y)
    {
        eaten = 1;
        s_score += 10;
    }

    /* 撞自己判定：跳过尾巴（尾巴即将移开），除非本步吃到食物（尾巴保留） */
    body_len = eaten ? s_len : (uint8_t)(s_len - 1);
    for (i = 0; i < body_len; i++)
    {
        if (s_snake_x[i] == nx && s_snake_y[i] == ny)
        {
            s_game_over = 1;
            return;
        }
    }

    /* 记录旧尾巴（吃食物时用于加长） */
    tail_x = s_snake_x[s_len - 1];
    tail_y = s_snake_y[s_len - 1];

    /* 身体整体前移，头放最前 */
    for (i = s_len - 1; i > 0; i--)
    {
        s_snake_x[i] = s_snake_x[i - 1];
        s_snake_y[i] = s_snake_y[i - 1];
    }
    s_snake_x[0] = nx;
    s_snake_y[0] = ny;

    if (eaten && s_len < SNAKE_MAX_LEN)
    {
        s_len++;
        s_snake_x[s_len - 1] = tail_x;   /* 尾巴保留，蛇变长 */
        s_snake_y[s_len - 1] = tail_y;
        SpawnFood();
    }
}

/*============================== 界面绘制 ===============================*/

/**
  * @brief  绘制一帧（分数行 + 蛇 + 食物）
  */
static void GameDraw(void)
{
    uint16_t i;

    SSD1306_Clear();

    /* 分数行 */
    SSD1306_ShowString(0, 0, 1, "SCORE", SSD1306_COLOR_ON);
    SSD1306_ShowNum(40, 0, s_score, 4, 1, SSD1306_COLOR_ON);

    /* 食物（画小方块） */
    SSD1306_FillRect((uint16_t)s_food_x * SNAKE_CELL_PX,
                     (uint16_t)(s_food_y + 1) * SNAKE_CELL_PX,
                     (uint16_t)s_food_x * SNAKE_CELL_PX + SNAKE_CELL_PX - 1,
                     (uint16_t)(s_food_y + 1) * SNAKE_CELL_PX + SNAKE_CELL_PX - 1,
                     SSD1306_COLOR_ON);

    /* 蛇身（空心方块便于区分头尾） */
    for (i = 0; i < s_len; i++)
    {
        uint16_t px = (uint16_t)s_snake_x[i] * SNAKE_CELL_PX;
        uint16_t py = (uint16_t)(s_snake_y[i] + 1) * SNAKE_CELL_PX;
        if (i == 0)
        {
            SSD1306_FillRect(px, py, px + SNAKE_CELL_PX - 1, py + SNAKE_CELL_PX - 1, SSD1306_COLOR_ON);
        }
        else
        {
            SSD1306_DrawRect(px, py, px + SNAKE_CELL_PX - 1, py + SNAKE_CELL_PX - 1, SSD1306_COLOR_ON);
        }
    }

    SSD1306_Display();
}

/**
  * @brief  显示 GAME OVER 画面
  */
static void GameOverDraw(void)
{
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "GAME OVER", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "SCORE:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(48, 16, s_score, 4, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 32, 1, "A:retry X:exit", SSD1306_COLOR_ON);
    SSD1306_Display();
}

/**
  * @brief  显示开始画面（等待 A 开始 / X 退出）
  * @retval 1 开始游戏；0 退出
  */
static uint8_t StartScreen(void)
{
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "SNAKE GAME", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "D-pad/stick move", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 24, 1, "A:start X:exit", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 32, 1, "B:pause", SSD1306_COLOR_ON);
    SSD1306_Display();

    while (1)
    {
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A)) return 1;   /* A 开始 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X)) return 0;   /* X 退出 */
        bsp_delay_ms(20);
    }
}

/**
  * @brief  暂停画面：阻塞等待 B 继续或 X 退出
  * @retval 1 继续；0 退出
  */
static uint8_t PauseScreen(void)
{
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "PAUSED", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "B:resume", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 24, 1, "X:exit", SSD1306_COLOR_ON);
    SSD1306_Display();

    while (1)
    {
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_B)) return 1;   /* B 继续 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X)) return 0;   /* X 退出 */
        bsp_delay_ms(20);
    }
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行贪吃蛇游戏（阻塞，直到 X 退出）
  */
void Snake_Run(void)
{
    uint32_t last_tick;
    uint8_t dir;

    GameInit();

    /* 起始画面：A 开始 / X 退出 */
    if (!StartScreen())
    {
        SSD1306_Clear();
        SSD1306_Display();
        return;
    }

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
            if (!PauseScreen())
            {
                break;                  /* 暂停中 X 退出 */
            }
            last_tick = g_tick_ms;      /* 恢复后重新计时 */
            GameDraw();
        }

        /* 手柄方向（十字键 / 左摇杆） */
        dir = Gamepad_GetDir();
        if (dir != GAMEPAD_DIR_NONE)
        {
            SetDir(dir);
        }

        /* 定期移动 */
        if ((uint32_t)(g_tick_ms - last_tick) >= SNAKE_TICK_MS)
        {
            last_tick = g_tick_ms;

            if (!s_game_over)
            {
                GameStep();
            }

            if (s_game_over)
            {
                GameOverDraw();
                while (1)
                {
                    if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))   /* A 重开一局 */
                    {
                        GameInit();
                        last_tick = g_tick_ms;
                        break;
                    }
                    if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))   /* X 退出 */
                    {
                        SSD1306_Clear();
                        SSD1306_Display();
                        return;
                    }
                    bsp_delay_ms(20);
                }
            }

            GameDraw();
        }

        bsp_delay_ms(10);
    }

    /* 退出游戏：清屏，交还主循环重绘 */
    SSD1306_Clear();
    SSD1306_Display();
}
