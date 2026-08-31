/**
  ******************************************************************************
  * @file    gamepad.h
  * @brief   无线手柄驱动（USART1 接收解析，18 字节帧）
  *
  *          手柄 -> 电脑（handle_to_serial.py）-> USB-TTL -> STM32F103C8T6
  *
  *          -----------------------------------------------
  *          USB-TTL 引脚            STM32F103C8T6
  *          -----------------------------------------------
  *          TX                     PA10（USART1_RX）
  *          RX                     PA9  （USART1_TX，可留空）
  *          GND                    GND
  *          -----------------------------------------------
  *
  *          波特率 115200 8N1（与上位机 --baud 一致）。
  *
  *          帧格式（18 字节，小端序）：
  *            偏移  长度  内容
  *            0     2     帧头 0xAA 0x55
  *            2     2     左摇杆 X   int16 (-32768~32767)
  *            4     2     左摇杆 Y   int16
  *            6     2     右摇杆 X   int16
  *            8     2     右摇杆 Y   int16
  *            10    2     LT 扳机    uint16 (0~32767)
  *            12    2     RT 扳机    uint16
  *            14    2     按键位图   uint16 (bit0=A, bit1=B, bit2=X, bit3=Y,
  *                                       bit4=LB, bit5=RB, bit6=BACK, bit7=START)
  *            16    1     十字键     bit0=上 bit1=下 bit2=左 bit3=右
  *            17    1     校验和     前 17 字节累加 & 0xFF
  *
  *          本驱动在 USART1 RXNE 中断里做帧解析，按边沿/电平提供按键查询：
  *            - Gamepad_ButtonPressed() 边沿触发（按下一次返回一次 1）
  *            - Gamepad_DPad() / Gamepad_StickDir() 实时电平
  ******************************************************************************
  */

#ifndef __GAMEPAD_H
#define __GAMEPAD_H

#include "stm32f10x.h"

/*============================== 引脚定义 ===============================*/
#define GAMEPAD_RX_GPIO     GPIOA
#define GAMEPAD_RX_PIN      GPIO_Pin_10
#define GAMEPAD_TX_GPIO     GPIOA
#define GAMEPAD_TX_PIN      GPIO_Pin_9

/*============================ 串口参数 =================================*/
#define GAMEPAD_BAUDRATE    115200

/*============================= 按键定义 =================================*/
/* 按键位图（对应帧偏移 14 的 uint16） */
#define GAMEPAD_BTN_A       (1U << 0)   /* 确认 / 开始 */
#define GAMEPAD_BTN_B       (1U << 1)   /* 暂停 */
#define GAMEPAD_BTN_X       (1U << 2)   /* 退出 */
#define GAMEPAD_BTN_Y       (1U << 3)
#define GAMEPAD_BTN_LB      (1U << 4)
#define GAMEPAD_BTN_RB      (1U << 5)
#define GAMEPAD_BTN_BACK    (1U << 6)
#define GAMEPAD_BTN_START   (1U << 7)

/* 十字键位图（对应帧偏移 16 的字节） */
#define GAMEPAD_DPAD_UP     0x01
#define GAMEPAD_DPAD_DOWN   0x02
#define GAMEPAD_DPAD_LEFT   0x04
#define GAMEPAD_DPAD_RIGHT  0x08

/* 左摇杆死区（|原始值| 小于该值视为回中，满量程 ±32768） */
#define GAMEPAD_STICK_DEADZONE  8000

/* 方向编码（与贪吃蛇 s_dir 一致）：0上 1右 2下 3左；无输入为 0xFF */
#define GAMEPAD_DIR_NONE    0xFF

/*============================= 公共接口 =================================*/

/**
  * @brief  初始化 USART1 接收手柄数据（115200 8N1，RXNE 中断）
  * @note   在 main 中调用一次；之后手柄帧到达即自动解析
  */
void Gamepad_Init(void);

/**
  * @brief  手柄是否在线（收到过有效帧）
  * @retval 1 在线，0 未连接/无数据
  */
uint8_t Gamepad_Connected(void);

/**
  * @brief  当前按键位图（实时电平）
  */
uint16_t Gamepad_GetButtons(void);

/**
  * @brief  边沿检测：某按键是否刚被按下（按下一次返回一次 1）
  * @param  btn: GAMEPAD_BTN_xxx 之一
  * @retval 1 本次调用前该键发生了一次按下
  */
uint8_t Gamepad_ButtonPressed(uint16_t btn);

/**
  * @brief  当前十字键位图（实时电平）
  */
uint8_t Gamepad_GetDPad(void);

/**
  * @brief  计算移动方向：优先十字键，其次左摇杆（带死区）
  * @retval GAMEPAD_DIR_NONE 无输入；否则 0上 1右 2下 3左
  */
uint8_t Gamepad_GetDir(void);

/**
  * @brief  读取最近一帧的原始数据（调试用）
  * @param  lx, ly:   左摇杆原始值输出
  * @param  buttons:  按键位图输出
  * @param  dpad:     十字键位图输出
  */
void Gamepad_GetFrame(int16_t *lx, int16_t *ly, uint16_t *buttons, uint8_t *dpad);

/**
  * @brief  读取 LT 扳机原始值（0 ~ 32767，未按下约 0）
  * @retval LT 扳机当前值；用于检测扳机是否按下（如 > 8000）
  */
uint16_t Gamepad_GetLT(void);

/**
  * @brief  读取 RT 扳机原始值（0 ~ 32767）
  */
uint16_t Gamepad_GetRT(void);

#endif /* __GAMEPAD_H */
