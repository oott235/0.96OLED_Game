/**
  ******************************************************************************
  * @file    hw_config.h
  * @brief   USB 硬件配置接口（CDC 库回调）
  ******************************************************************************
  */

#ifndef __HW_CONFIG_H
#define __HW_CONFIG_H

#include "platform_config.h"
#include "usb_type.h"

#define MASS_MEMORY_START     0x04002000
#define BULK_MAX_PACKET_SIZE  0x00000040
#define LED_ON                0xF0
#define LED_OFF               0xFF

void Set_System(void);
void Set_USBClock(void);
void Enter_LowPowerMode(void);
void Leave_LowPowerMode(void);
void USB_Interrupts_Config(void);
void USB_Cable_Config(FunctionalState NewState);
void Get_SerialNum(void);
uint32_t CDC_Send_DATA(uint8_t *ptrBuffer, uint8_t Send_length);
uint32_t CDC_Receive_DATA(void);

#endif  /* __HW_CONFIG_H */
