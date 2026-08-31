/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs (C)ChaN, 2019                     */
/* 适配：SD 卡（SPI，STM32F103C8T6）                                      */
/*-----------------------------------------------------------------------*/

#include "ff.h"         /* Obtains integer types */
#include "diskio.h"     /* Declarations of disk functions */
#include "sd_spi.h"     /* SD 卡 SPI 驱动 */

/* 物理驱动器编号：SD 卡映射到 0 */
#define DEV_SD  0

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive number to identify the drive */
)
{
	DSTATUS stat = 0;

	if (pdrv != DEV_SD)
	{
		return STA_NOINIT;
	}

	/* SD 卡无法热插拔检测（无卡检测引脚），
	   初始化成功后一直视为就绪；SD_Init 失败在 disk_initialize 中返回 NOINIT */
	return stat;
}


/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv		/* Physical drive number to identify the drive */
)
{
	if (pdrv != DEV_SD)
	{
		return STA_NOINIT;
	}

	if (SD_Init() != 0)
	{
		return STA_NOINIT;
	}

	return 0;
}


/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive number to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv != DEV_SD)
	{
		return RES_PARERR;
	}

	while (count--)
	{
		if (SD_ReadSector((uint32_t)sector, buff) != 0)
		{
			return RES_ERROR;
		}
		sector++;
		buff += 512;
	}

	return RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive number to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (pdrv != DEV_SD)
	{
		return RES_PARERR;
	}

	while (count--)
	{
		if (SD_WriteSector((uint32_t)sector, buff) != 0)
		{
			return RES_ERROR;
		}
		sector++;
		buff += 512;
	}

	return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive number (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res = RES_OK;

	if (pdrv != DEV_SD)
	{
		return RES_PARERR;
	}

	switch (cmd)
	{
		case CTRL_SYNC:          /* 同步：写操作已等待忙释放，直接成功 */
			res = RES_OK;
			break;

		case GET_SECTOR_COUNT:   /* 获取介质容量（f_mkfs 需要） */
			*(DWORD *)buff = SD_GetSectorCount();
			if (*(DWORD *)buff == 0)
			{
				res = RES_ERROR;
			}
			break;

		case GET_SECTOR_SIZE:    /* 获取扇区大小（FF_MAX_SS != FF_MIN_SS 时需要） */
			*(WORD *)buff = 512;
			break;

		case GET_BLOCK_SIZE:     /* 获取擦除块大小（f_mkfs 需要） */
			*(DWORD *)buff = 1;  /* 按扇区擦除 */
			break;

		default:
			res = RES_PARERR;
			break;
	}

	return res;
}
