/**
  ******************************************************************************
  * @file    bsp_delay.c
  * @brief   时基模块实现
  ******************************************************************************
  */

#include "bsp_delay.h"

volatile uint32_t g_tick_ms = 0;

/*
 * 工程自带的 CMSIS core_cm3.h（SPL 3.5.0）未声明 DWT 调试单元，
 * 这里通过寄存器地址直接访问：
 *   DWT 基地址    = 0xE0001000
 *   DWT_CTRL      @ +0x0000（bit0 = CYCCNTENA）
 *   DWT_CYCCNT    @ +0x0004
 */
#define DWT_CTRL_REG             (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT_REG           (*(volatile uint32_t *)0xE0001004UL)
#define DWT_CTRL_CYCCNTENA_Msk   (1UL << 0)

/**
  * @brief  初始化时基
  */
void bsp_delay_init(void)
{
    /* DWT 周期计数器（Cortex-M3 调试单元），用于微秒延时 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* 使能 DWT 访问 */
    DWT_CYCCNT_REG = 0;
    DWT_CTRL_REG  |= DWT_CTRL_CYCCNTENA_Msk;          /* 启动计数      */

    /* SysTick：1ms 中断（CPU 时钟源），最低优先级 */
    SysTick->CTRL = 0;
    SysTick->LOAD = (uint32_t)(SystemCoreClock / 1000U) - 1U;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk  |
                    SysTick_CTRL_ENABLE_Msk;
    NVIC_SetPriority(SysTick_IRQn, 0x0F);
}

/**
  * @brief  阻塞延时（毫秒）
  */
void bsp_delay_ms(uint32_t ms)
{
    uint32_t start = g_tick_ms;

    while ((uint32_t)(g_tick_ms - start) < ms)
    {
        /* 等待滴答计数前进 ms 个周期 */
    }
}

/**
  * @brief  阻塞延时（微秒）
  */
void bsp_delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT_REG;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);

    while ((uint32_t)(DWT_CYCCNT_REG - start) < ticks)
    {
        /* 等待周期计数前进 ticks 个周期 */
    }
}

/**
  * @brief  读取当前微秒计数（DWT 周期计数换算，用于高精度测时）
  * @retval 自 bsp_delay_init 以来的微秒数（约 1ms 后即有意义）
  * @note   配合差值使用：(bsp_delay_us_now() - start) 为经过的微秒数
  */
uint32_t bsp_delay_us_now(void)
{
    return DWT_CYCCNT_REG / (SystemCoreClock / 1000000U);
}
