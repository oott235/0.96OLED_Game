/**
  ******************************************************************************
  * @file    hw_config.c
  * @brief   USB 硬件配置（蓝丸板 STM32F103C8T6 适配版）
  *
  *          说明：工程主时钟已由 system_stm32f10x.c 配置为 72MHz（PLL 9x），
  *          这里只补 USB 相关：时钟源选择（PLL/1.5 = 48MHz）、PA11/PA12
  *          USB 引脚、PA15 D+ 上拉控制、USB 中断、CDC 收发。
  ******************************************************************************
  */

#include "hw_config.h"
#include "usb_lib.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "stm32f10x_it.h"

/* 私有变量 ---------------------------------------------------------------- */
ErrorStatus HSEStartUpStatus;
EXTI_InitTypeDef EXTI_InitStructure;

/* CDC 收发缓冲与标志（供 usb_endp.c / signal.c 使用）
   volatile 修饰与 usb_endp.c 的 extern 声明保持一致 */
volatile uint32_t packet_sent = 1;
volatile uint32_t packet_receive = 1;
volatile uint8_t Receive_Buffer[VIRTUAL_COM_PORT_DATA_SIZE];
/* Receive_length 由 usb_endp.c 定义（非 volatile），这里仅声明引用 */
extern uint32_t Receive_length;

/* 串口行编码（CDC 参数，实际波特率由 USB 固定，仅供主机识别） */
extern LINE_CODING linecoding;

/*==========================================================================*/

/**
  * @brief  系统初始化（USB 相关）
  * @note   1. F103 的 PA11/PA12 由 USB 外设直接接管，不需要配置 GPIO，
  *            配置成 AF 反而可能干扰枚举——这里不碰它们。
  *         2. 蓝丸板 D+（PA12）依赖板载 1.5kΩ 上拉电阻（USB 规范要求），
  *            无外部 disconnect 电路，USB_Cable_Config 空实现即可。
  *         3. 主时钟 72MHz 已由 SystemInit() 完成。
  */
void Set_System(void)
{
    /* EXTI 线 18（USB 内部连接）上升沿触发，用于唤醒 */
    EXTI_ClearITPendingBit(EXTI_Line18);
    EXTI_InitStructure.EXTI_Line = EXTI_Line18;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
}

/**
  * @brief  配置 USB 时钟（PLL 72MHz / 1.5 = 48MHz）
  */
void Set_USBClock(void)
{
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_1Div5);   /* 72/1.5 = 48MHz */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
}

/**
  * @brief  进入低功耗（挂起）
  */
void Enter_LowPowerMode(void)
{
    bDeviceState = SUSPENDED;
}

/**
  * @brief  退出低功耗（恢复时钟）
  */
void Leave_LowPowerMode(void)
{
    DEVICE_INFO *pInfo = &Device_Info;

    if (pInfo->Current_Configuration != 0)
    {
        bDeviceState = CONFIGURED;
    }
    else
    {
        bDeviceState = ATTACHED;
    }
}

/**
  * @brief  配置 USB 中断（USB 低优先级 + 唤醒）
  * @note   不调用 NVIC_PriorityGroupConfig！工程其他部分（bsp_delay 的
  *         SysTick）已用默认分组设置优先级，改变分组会让 SysTick 优先级
  *         失效导致 g_tick_ms 停走、主循环延时卡死。
  *         USB 中断用最低优先级（0x0F），即使 USB 中断风暴也不会饿死 SysTick。
  */
void USB_Interrupts_Config(void)
{
    /* 与 bsp_delay 保持一致：直接写优先级寄存器，不改变分组 */
    NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0x0F);
    NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);

    NVIC_SetPriority(USBWakeUp_IRQn, 0x0F);
    NVIC_EnableIRQ(USBWakeUp_IRQn);
}

/**
  * @brief  USB 线缆连接/断开
  * @note   蓝丸板 D+ 已有板载 1.5kΩ 上拉电阻（USB 规范要求），
  *         无外部 disconnect 电路，这里空实现即可。
  */
void USB_Cable_Config(FunctionalState NewState)
{
    (void)NewState;   /* 依赖板载上拉，无需软件控制 */
}

/**
  * @brief  生成序列号字符串描述符
  * @note   F103 中容量（C8T6）可能没有 96-bit 唯一 ID 寄存器，
  *         直接读 0x1FFFF7E8 有 HardFault 风险，改用固定序列号最安全。
  */
void Get_SerialNum(void)
{
    /* 固定序列号：每字节转 Unicode（"VCOM0001" -> 14 字节内容） */
    static const char s_serial[] = "VCOM0001";
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        Virtual_Com_Port_StringSerial[2 + i * 2]     = (uint8_t)s_serial[i];
        Virtual_Com_Port_StringSerial[2 + i * 2 + 1] = 0;
    }
}

/**
  * @brief  CDC 发送数据（最多 64 字节，含满包 64）
  * @retval 1 成功；0 数据过长
  */
uint32_t CDC_Send_DATA(uint8_t *ptrBuffer, uint8_t Send_length)
{
    if (Send_length <= VIRTUAL_COM_PORT_DATA_SIZE)
    {
        packet_sent = 0;
        UserToPMABufferCopy((unsigned char *)ptrBuffer, ENDP1_TXADDR, Send_length);
        SetEPTxCount(ENDP1, Send_length);
        SetEPTxValid(ENDP1);
    }
    else
    {
        return 0;
    }
    return 1;
}

/**
  * @brief  CDC 接收使能
  */
uint32_t CDC_Receive_DATA(void)
{
    packet_receive = 0;
    SetEPRxValid(ENDP3);
    return 1;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
