/**
  ******************************************************************************
  * @file    platform_config.h
  * @brief   平台配置（蓝丸板 STM32F103C8T6 USB CDC 适配）
  *
  *          USB 引脚：
  *            PA11 -> USB_DM（D-）
  *            PA12 -> USB_DP（D+，板载 1.5k 上拉到 3.3V）
  *
  *          USB_DISCONNECT：用 PA15 模拟上拉控制（开漏输出，通过板载
  *          上拉电阻实现 D+ 上拉），PowerOn 时拉低/输出高电平模拟连接。
  *          若板子 D+ 已有固定 1.5k 上拉，此引脚仅作占位，不影响功能。
  ******************************************************************************
  */

#ifndef __PLATFORM_CONFIG_H
#define __PLATFORM_CONFIG_H

#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_pwr.h"
#include "misc.h"

/* 唯一设备 ID 寄存器地址（F103） */
#define ID1          (0x1FFFF7E8)
#define ID2          (0x1FFFF7EC)
#define ID3          (0x1FFFF7F0)

/* USB D+ 上拉控制引脚：PA15（蓝丸板做法，开漏 + 板上拉） */
#define USB_DISCONNECT                      GPIOA
#define USB_DISCONNECT_PIN                  GPIO_Pin_15
#define RCC_APB2Periph_GPIO_DISCONNECT      RCC_APB2Periph_GPIOA

#endif /* __PLATFORM_CONFIG_H */
