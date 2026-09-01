/**
  ******************************************************************************
  * @file    sd_dl.h
  * @brief   SD 卡 TTL 下载（串口接收 -> 写入 SD 卡 /VIDEO/ 目录）
  *
  *          电脑（video_to_bin.py --send-sd）-> USB-TTL -> STM32F103C8T6
  *          -> FatFS 写入 SD 卡 /VIDEO/<文件名>.BIN
  *
  *          与手柄共用 USART1（PA10=RX / PA9=TX，115200 8N1），
  *          进入下载模式后暂时关闭手柄 RXNE 中断，独占串口；
  *          结束后恢复手柄接收。
  ******************************************************************************
  */

#ifndef __SD_DL_H
#define __SD_DL_H

#include "stm32f10x.h"

/**
  * @brief  运行 SD 卡下载应用（阻塞）
  * @note   从主菜单进入；等待电脑端开始发送，成功后返回菜单
  */
void SD_DL_Run(void);

#endif /* __SD_DL_H */
