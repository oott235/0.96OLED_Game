/**
  ******************************************************************************
  * @file    sd_spi.h
  * @brief   SD 卡 SPI 驱动（STM32F103C8T6 SPI1）
  *
  *          -----------------------------------------------
  *          SD 卡模块引脚           STM32F103C8T6
  *          -----------------------------------------------
  *          CS (片选)               PA4（GPIO 推挽输出）
  *          MOSI (主出从入)          PA7（SPI1_MOSI）
  *          MISO (主入从出)          PA6（SPI1_MISO）
  *          SCK (时钟)               PA5（SPI1_SCK）
  *          VCC                     3.3V
  *          GND                     GND
  *          -----------------------------------------------
  *
  *          通信：SPI 模式 0（CPOL=0/CPHA=1 边沿），MSB 在前。
  *          初始化阶段 SPI 分频 256（约 281kHz，符合 SD 上电 <400kHz 要求），
  *          初始化成功后切换到分频 8（约 9MHz）提升读写速度。
  *
  *          支持 SDSC / SDHC / SDXC（FAT32 所需），块大小固定 512 字节。
  *          底层供 FatFS 的 diskio.c 调用。
  ******************************************************************************
  */

#ifndef __SD_SPI_H
#define __SD_SPI_H

#include "stm32f10x.h"

/*============================== 引脚定义 ===============================*/
#define SD_CS_GPIO      GPIOA
#define SD_CS_PIN       GPIO_Pin_4

/* SPI1: SCK=PA5, MISO=PA6, MOSI=PA7（默认映射，无需重映射） */

/*============================= 公共接口 =================================*/

/**
  * @brief  初始化 SD 卡（SPI1 + 卡上电序列）
  * @retval 0 成功；非 0 失败（1=CMD0 失败，2=ACMD41 超时，3=未就绪）
  */
uint8_t SD_Init(void);

/**
  * @brief  读取单个 512 字节扇区
  * @param  sector: LBA 扇区号
  * @param  buf:    输出缓冲（>=512 字节）
  * @retval 0 成功；非 0 失败
  */
uint8_t SD_ReadSector(uint32_t sector, uint8_t *buf);

/**
  * @brief  写入单个 512 字节扇区
  * @param  sector: LBA 扇区号
  * @param  buf:    输入缓冲（512 字节）
  * @retval 0 成功；非 0 失败
  */
uint8_t SD_WriteSector(uint32_t sector, const uint8_t *buf);

/**
  * @brief  获取 SD 卡总扇区数（从 CSD 解析）
  * @retval 扇区数（512 字节/扇区）；读取失败返回 0
  */
uint32_t SD_GetSectorCount(void);

/**
  * @brief  诊断：读取 CSD 寄存器原始 16 字节
  * @param  csd: 输出缓冲（16 字节）
  * @retval 0 成功；非 0 失败
  */
uint8_t SD_ReadCSD(uint8_t *csd);

#endif /* __SD_SPI_H */
