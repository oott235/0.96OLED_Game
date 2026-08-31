/**
  ******************************************************************************
  * @file    gamepad.c
  * @brief   无线手柄驱动实现（USART1 中断接收 + 18 字节帧解析）
  *
  *          上位机 handle_to_serial.py 以 100Hz 发送帧，帧格式见 gamepad.h。
  *          解析在 USART1 RXNE 中断里完成：逐字节状态机收满 18 字节后
  *          校验和通过才更新手柄状态，避免噪声数据污染。
  *
  *          按键边沿：记录上一帧按键位图，0->1 的跳变存入"按下锁存器"，
  *          Gamepad_ButtonPressed() 查询并清除对应位，实现"按一次响一次"。
  ******************************************************************************
  */

#include "gamepad.h"

/*============================== 私有定义 ===============================*/
#define GAMEPAD_FRAME_LEN   18      /* 完整帧长度 */
#define GAMEPAD_HEADER0     0xAA    /* 帧头低字节 */
#define GAMEPAD_HEADER1     0x55    /* 帧头高字节 */

/* 帧内偏移 */
#define OFS_LX      2
#define OFS_LY      4
#define OFS_RX      6
#define OFS_RY      8
#define OFS_LT      10
#define OFS_RT      12
#define OFS_BUTTONS 14
#define OFS_DPAD    16
#define OFS_SUM     17

/* 解析状态机 */
typedef enum
{
    PARSE_SYNC,     /* 等待帧头 0xAA */
    PARSE_HEAD,     /* 已收 0xAA，等待 0x55 */
    PARSE_DATA      /* 已收帧头，正在收数据 */
} parse_state_t;

/*============================== 私有变量 ===============================*/

static volatile uint8_t  s_buf[GAMEPAD_FRAME_LEN];   /* 接收缓冲 */
static volatile uint8_t  s_idx;                      /* 已收字节数 */
static volatile uint8_t  s_state = PARSE_SYNC;

static volatile int16_t  s_lx,  s_ly,  s_rx,  s_ry;  /* 摇杆原始值 */
static volatile uint16_t s_lt,  s_rt;                /* 扳机 */
static volatile uint16_t s_buttons;                  /* 当前按键位图 */
static volatile uint8_t  s_dpad;                     /* 当前十字键 */
static volatile uint16_t s_prev_buttons;             /* 上一帧按键（边沿用） */
static volatile uint16_t s_pressed_latch;            /* 按下锁存（0->1 跳变） */
static volatile uint8_t  s_connected;                /* 收到过有效帧 */

/*============================ 中断服务函数 =============================*/

/**
  * @brief  USART1 中断：接收手柄帧
  * @note   由启动文件向量表调用，逐字节状态机解析
  */
void USART1_IRQHandler(void)
{
    uint8_t b, i, sum;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) == RESET)
    {
        return;
    }
    b = (uint8_t)USART_ReceiveData(USART1);

    switch (s_state)
    {
        case PARSE_SYNC:
            if (b == GAMEPAD_HEADER0)
            {
                s_buf[0] = b;
                s_idx = 1;
                s_state = PARSE_HEAD;
            }
            break;

        case PARSE_HEAD:
            if (b == GAMEPAD_HEADER1)
            {
                s_buf[1] = b;
                s_idx = 2;
                s_state = PARSE_DATA;
            }
            else if (b == GAMEPAD_HEADER0)
            {
                s_idx = 1;              /* 连续 0xAA：保持帧头状态 */
            }
            else
            {
                s_state = PARSE_SYNC;   /* 帧头错误，重新同步 */
            }
            break;

        case PARSE_DATA:
        default:
            s_buf[s_idx++] = b;
            if (s_idx >= GAMEPAD_FRAME_LEN)
            {
                /* 校验和：前 17 字节累加 & 0xFF */
                sum = 0;
                for (i = 0; i < OFS_SUM; i++)
                {
                    sum += s_buf[i];
                }
                if (sum == s_buf[OFS_SUM])
                {
                    /* 小端序解析 */
                    s_lx = (int16_t)(s_buf[OFS_LX] | (s_buf[OFS_LX + 1] << 8));
                    s_ly = (int16_t)(s_buf[OFS_LY] | (s_buf[OFS_LY + 1] << 8));
                    s_rx = (int16_t)(s_buf[OFS_RX] | (s_buf[OFS_RX + 1] << 8));
                    s_ry = (int16_t)(s_buf[OFS_RY] | (s_buf[OFS_RY + 1] << 8));
                    s_lt = (uint16_t)(s_buf[OFS_LT] | (s_buf[OFS_LT + 1] << 8));
                    s_rt = (uint16_t)(s_buf[OFS_RT] | (s_buf[OFS_RT + 1] << 8));
                    s_buttons = (uint16_t)(s_buf[OFS_BUTTONS] | (s_buf[OFS_BUTTONS + 1] << 8));
                    s_dpad    = s_buf[OFS_DPAD];

                    /* 边沿：仅记录 0->1 的跳变（按下事件） */
                    s_pressed_latch |= (uint16_t)(s_buttons & ~s_prev_buttons);
                    s_prev_buttons = s_buttons;

                    s_connected = 1;
                }
                s_state = PARSE_SYNC;
            }
            break;
    }
}

/*============================= 公共接口 =================================*/

/**
  * @brief  初始化 USART1：115200 8N1，RXNE 中断
  */
void Gamepad_Init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = TX（推挽复用），PA10 = RX（浮空输入） */
    gpio.GPIO_Pin   = GAMEPAD_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GAMEPAD_TX_GPIO, &gpio);

    gpio.GPIO_Pin   = GAMEPAD_RX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GAMEPAD_RX_GPIO, &gpio);

    /* USART1：115200 8N1 */
    USART_StructInit(&usart);
    usart.USART_BaudRate = GAMEPAD_BAUDRATE;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits   = USART_StopBits_1;
    usart.USART_Parity     = USART_Parity_No;
    usart.USART_Mode       = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    /* RXNE 中断（优先级高于 SysTick 的 0x0F） */
    nvic.NVIC_IRQChannel = USART1_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 0;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART1, ENABLE);

    /* 复位解析状态 */
    s_state = PARSE_SYNC;
    s_idx = 0;
    s_connected = 0;
    s_prev_buttons = 0;
    s_pressed_latch = 0;
}

/**
  * @brief  手柄是否在线
  */
uint8_t Gamepad_Connected(void)
{
    return s_connected;
}

/**
  * @brief  当前按键位图（实时电平）
  */
uint16_t Gamepad_GetButtons(void)
{
    return s_buttons;
}

/**
  * @brief  边沿检测：某按键是否刚被按下（查询后清除，按一次响一次）
  */
uint8_t Gamepad_ButtonPressed(uint16_t btn)
{
    uint8_t pressed;

    /* 关中断保护锁存器读-改-写 */
    __disable_irq();
    pressed = (s_pressed_latch & btn) ? 1 : 0;
    s_pressed_latch &= (uint16_t)~btn;
    __enable_irq();

    return pressed;
}

/**
  * @brief  当前十字键位图（实时电平）
  */
uint8_t Gamepad_GetDPad(void)
{
    return s_dpad;
}

/**
  * @brief  计算移动方向：优先十字键，其次左摇杆（带死区）
  * @retval GAMEPAD_DIR_NONE 无输入；否则 0上 1右 2下 3左
  */
uint8_t Gamepad_GetDir(void)
{
    /* 十字键优先 */
    if (s_dpad & GAMEPAD_DPAD_UP)    return 0;
    if (s_dpad & GAMEPAD_DPAD_DOWN)  return 2;
    if (s_dpad & GAMEPAD_DPAD_LEFT)  return 3;
    if (s_dpad & GAMEPAD_DPAD_RIGHT) return 1;

    /* 左摇杆（SDL：Y 负为前/上） */
    if (s_ly < -GAMEPAD_STICK_DEADZONE) return 0;
    if (s_ly >  GAMEPAD_STICK_DEADZONE) return 2;
    if (s_lx < -GAMEPAD_STICK_DEADZONE) return 3;
    if (s_lx >  GAMEPAD_STICK_DEADZONE) return 1;

    return GAMEPAD_DIR_NONE;
}

/**
  * @brief  读取最近一帧原始数据（调试用）
  */
void Gamepad_GetFrame(int16_t *lx, int16_t *ly, uint16_t *buttons, uint8_t *dpad)
{
    if (lx)      *lx = s_lx;
    if (ly)      *ly = s_ly;
    if (buttons) *buttons = s_buttons;
    if (dpad)    *dpad = s_dpad;
}

/**
  * @brief  读取 LT 扳机原始值（0 ~ 32767）
  */
uint16_t Gamepad_GetLT(void)
{
    return s_lt;
}

/**
  * @brief  读取 RT 扳机原始值（0 ~ 32767）
  */
uint16_t Gamepad_GetRT(void)
{
    return s_rt;
}
