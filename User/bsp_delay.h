/**
  ******************************************************************************
  * @file    bsp_delay.h
  * @brief   时基模块：SysTick 1ms 滴答计数 + DWT 微秒延时
  *
  *          使用前提：系统时钟 SystemCoreClock = 72MHz（SystemInit 配置）
  *          使用流程：上电后先调用 bsp_delay_init()，之后即可使用延时函数
  ******************************************************************************
  */

#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include "stm32f10x.h"

/* 全局 1ms 滴答计数（在 stm32f10x_it.c 的 SysTick_Handler 中自增） */
extern volatile uint32_t g_tick_ms;

/**
  * @brief  初始化时基：SysTick 1ms 中断 + DWT 周期计数器
  * @note   必须在调用任何延时函数之前执行一次
  */
void bsp_delay_init(void);

/**
  * @brief  阻塞延时（毫秒）
  * @param  ms: 延时长度，0 立即返回
  */
void bsp_delay_ms(uint32_t ms);

/**
  * @brief  阻塞延时（微秒），基于 DWT 周期计数，短延时准确
  * @param  us: 延时长度
  */
void bsp_delay_us(uint32_t us);

#endif /* __BSP_DELAY_H */
