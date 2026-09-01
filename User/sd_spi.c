/**
  ******************************************************************************
  * @file    sd_spi.c
  * @brief   SD 卡 SPI 驱动实现（SPI1，模式 0）
  *
  *          上电序列（SPI 模式初始化）：
  *            1. CS=1，发送 >=74 个时钟（10 字节 0xFF）
  *            2. CMD0  -> R1 = 0x01（进入 SPI 模式）
  *            3. CMD8  -> 支持 SDHC/SDXC v2（可选）
  *            4. CMD55+ACMD41 循环直至 R1 = 0x00（退出空闲）
  *            5. CMD58 读 OCR，判断高容量卡（bit30）
  *            6. CMD16 设置块长 512
  *
  *          读写块：CMD17/CMD24 + 0xFE 数据令牌，512 字节 + 2 CRC。
  *          参考 ST 官方 stm32_eval_spi_sd.c 与 SD 简化规范 (v2.00)。
  ******************************************************************************
  */

#include "sd_spi.h"
#include "bsp_delay.h"
#include <stddef.h>   /* NULL */

/*============================== 私有定义 ===============================*/
#define SD_DUMMY         0xFF
#define SD_BLOCK_SIZE    512

/* 命令（CMDxx = 编号） */
#define SD_CMD0          0   /* 进入空闲（SPI 模式） */
#define SD_CMD8          8   /* 发送接口条件（v2 检测） */
#define SD_CMD9          9   /* 读 CSD */
#define SD_CMD16         16  /* 设置块长 */
#define SD_CMD17         17  /* 读单块 */
#define SD_CMD24         24  /* 写单块 */
#define SD_CMD55         55  /* 应用命令前缀 */
#define SD_CMD58         58  /* 读 OCR */
#define SD_ACMD41        41  /* 上电条件（HCS） */

/* 数据令牌 */
#define SD_TOKEN_READ    0xFE
#define SD_TOKEN_WRITE   0xFE

/*============================== 私有变量 ===============================*/
static uint8_t  s_sdhc;          /* 1 = 高容量卡（SDHC/SDXC，块寻址） */
static uint32_t s_sector_count;  /* 总扇区数 */

/* 诊断：ACMD41 循环最后一次 R1（0xFF=无响应/线断，0x01=卡未就绪，0x00=成功） */
uint8_t sd_acmd41_r1 = 0xFF;

/*========================== SPI 底层收发 ================================*/

/**
  * @brief  SPI1 收发一个字节（同时读回 MISO 数据）
  */
static uint8_t SD_SpiTransfer(uint8_t byte)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) { }
    SPI_I2S_SendData(SPI1, byte);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) { }
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

/**
  * @brief  写一个字节
  */
static void SD_WriteByte(uint8_t byte)
{
    (void)SD_SpiTransfer(byte);
}

/**
  * @brief  读一个字节（发送 0xFF 占位）
  */
static uint8_t SD_ReadByte(void)
{
    return SD_SpiTransfer(SD_DUMMY);
}

/*========================= 命令与响应 ===================================*/

/**
  * @brief  发送 6 字节命令帧并读取 R1 响应
  * @param  cmd: 命令编号（0-63）
  * @param  arg: 32 位参数
  * @param  crc: CRC（CMD0 需 0x95，CMD8 需 0x87，其余可 0xFF）
  * @retval R1 响应字节；超时返回 0xFF
  */
static uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t r1;
    uint32_t timeout;

    /* 命令帧 6 字节：0x40|cmd + arg(4) + crc */
    SD_WriteByte((uint8_t)(0x40 | cmd));
    SD_WriteByte((uint8_t)(arg >> 24));
    SD_WriteByte((uint8_t)(arg >> 16));
    SD_WriteByte((uint8_t)(arg >> 8));
    SD_WriteByte((uint8_t)arg);
    SD_WriteByte(crc);

    /* 等待 R1（最多 64 个 0xFF） */
    timeout = 64;
    do
    {
        r1 = SD_ReadByte();
    } while ((r1 & 0x80) && (--timeout > 0));   /* 首位为 0 才算有效响应 */

    return (timeout > 0) ? r1 : 0xFF;
}

/**
  * @brief  等待数据令牌（0xFE），超时返回 0
  */
static uint8_t SD_WaitToken(uint8_t token, uint32_t timeout)
{
    uint8_t b;

    do
    {
        b = SD_ReadByte();
        if (b == token) return 1;
    } while (--timeout > 0);
    return 0;
}

/*=========================== 初始化序列 =================================*/

/**
  * @brief  SPI1 硬件初始化（分频可切换）
  * @param  prescaler: SPI_BaudRatePrescaler_xxx（初始化用 256，运行用 8）
  */
static void SD_SpiInit(uint16_t prescaler)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    /* CS = PA4 推挽输出，默认高（未选中） */
    gpio.GPIO_Pin   = SD_CS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SD_CS_GPIO, &gpio);
    GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);

    /* SCK = PA5（复用推挽），MOSI = PA7（复用推挽），MISO = PA6（浮空输入） */
    gpio.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin   = GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    /* SPI1 主机，模式 0（CPOL=0/CPHA=1 边沿），MSB 先，软件 NSS */
    SPI_StructInit(&spi);
    spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode              = SPI_Mode_Master;
    spi.SPI_DataSize          = SPI_DataSize_8b;
    spi.SPI_CPOL              = SPI_CPOL_Low;
    spi.SPI_CPHA              = SPI_CPHA_1Edge;
    spi.SPI_NSS               = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = prescaler;
    spi.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);
}

/**
  * @brief  初始化 SD 卡
  * @retval 0 成功；1=CMD0 失败，2=ACMD41 超时，3=OCR 读失败
  */
uint8_t SD_Init(void)
{
    uint8_t  r1, ocr[4], i;
    uint32_t timeout;

    s_sdhc = 0;
    s_sector_count = 0;

    /* 上电：低速 SPI + CS 高 + 至少 74 个时钟 */
    SD_SpiInit(SPI_BaudRatePrescaler_256);   /* 约 281kHz */
    bsp_delay_ms(10);

    for (i = 0; i < 10; i++)
    {
        SD_WriteByte(SD_DUMMY);
    }

    /* CMD0：进入 SPI 模式，R1 应 = 0x01 */
    GPIO_ResetBits(SD_CS_GPIO, SD_CS_PIN);   /* CS 低，选中 */
    r1 = SD_SendCmd(SD_CMD0, 0, 0x95);
    if (r1 != 0x01)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        return 1;
    }

    /* CMD8：检测 SD v2（SDHC/SDXC）。老卡无响应（R1=0x01 且无数据），
       继续尝试 ACMD41，不影响 SDSC 卡 */
    r1 = SD_SendCmd(SD_CMD8, 0x000001AA, 0x87);
    if (r1 == 0x01)
    {
        for (i = 0; i < 4; i++) (void)SD_ReadByte();   /* 丢弃 4 字节响应（含 0x1AA） */
    }

    /* CMD55 + ACMD41 初始化（退出空闲，R1=0x00）
       第一轮：HCS=1 请求高容量（SDHC/SDXC），最多 1.5s
       第二轮：HCS=0 普通容量（SDSC 老卡兼容），最多 1.5s */
    timeout = 1500;
    do
    {
        SD_SendCmd(SD_CMD55, 0, 0xFF);               /* CMD55：下一个是应用命令 */
        r1 = SD_SendCmd(SD_ACMD41, 0x40000000, 0xFF); /* ACMD41：HCS=1，请求高容量 */
        bsp_delay_ms(1);
    } while ((r1 != 0x00) && (--timeout > 0));

    if (r1 != 0x00)
    {
        /* 第一轮失败：按 SDSC 老卡再试（HCS=0），部分卡对 HCS=1 不兼容 */
        timeout = 1500;
        do
        {
            SD_SendCmd(SD_CMD55, 0, 0xFF);
            r1 = SD_SendCmd(SD_ACMD41, 0x00000000, 0xFF); /* ACMD41：HCS=0 */
            bsp_delay_ms(1);
        } while ((r1 != 0x00) && (--timeout > 0));
    }
    sd_acmd41_r1 = r1;                               /* 记录最后一次 R1 供诊断 */

    if (r1 != 0x00)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        return 2;
    }

    /* CMD58：读 OCR，bit30=1 表示高容量卡（块寻址） */
    r1 = SD_SendCmd(SD_CMD58, 0, 0xFF);
    if (r1 != 0x00)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        return 3;
    }
    for (i = 0; i < 4; i++)
    {
        ocr[i] = SD_ReadByte();
    }
    s_sdhc = (ocr[1] & 0x40) ? 1 : 0;   /* OCR[30]（字节序：ocr[0] 是最高位字节） */

    /* CMD16：块长固定 512（SDSC 需要；SDHC/SDXC 固定 512 可跳过，发了也无害） */
    SD_SendCmd(SD_CMD16, SD_BLOCK_SIZE, 0xFF);
    (void)SD_ReadByte();

    /* CS 高，结束初始化 */
    GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
    SD_WriteByte(SD_DUMMY);

    /* 提速到约 9MHz */
    SD_SpiInit(SPI_BaudRatePrescaler_8);

    /* 读 CSD 获取容量 */
    SD_ReadCSD(NULL);

    return 0;
}

/*=========================== 扇区读写 ===================================*/

/**
  * @brief  读取单个 512 字节扇区
  */
uint8_t SD_ReadSector(uint32_t sector, uint8_t *buf)
{
    uint32_t i;
    uint8_t r1;

    if (buf == NULL) return 1;

    GPIO_ResetBits(SD_CS_GPIO, SD_CS_PIN);

    /* 地址：SDSC 用字节地址（扇区 x512），SDHC 用块地址 */
    r1 = SD_SendCmd(SD_CMD17, s_sdhc ? sector : (sector << 9), 0xFF);
    if (r1 != 0x00)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 1;
    }

    /* 等待 0xFE 数据令牌 */
    if (!SD_WaitToken(SD_TOKEN_READ, 200000))
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 2;
    }

    /* 读 512 字节 + 2 CRC */
    for (i = 0; i < SD_BLOCK_SIZE; i++)
    {
        buf[i] = SD_ReadByte();
    }
    (void)SD_ReadByte();
    (void)SD_ReadByte();

    GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
    SD_WriteByte(SD_DUMMY);
    return 0;
}

/**
  * @brief  写入单个 512 字节扇区
  */
uint8_t SD_WriteSector(uint32_t sector, const uint8_t *buf)
{
    uint32_t i, timeout;
    uint8_t r1, resp;

    if (buf == NULL) return 1;

    GPIO_ResetBits(SD_CS_GPIO, SD_CS_PIN);

    r1 = SD_SendCmd(SD_CMD24, s_sdhc ? sector : (sector << 9), 0xFF);
    if (r1 != 0x00)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 1;
    }

    /* 数据块：令牌 0xFE + 512 字节 + 2 CRC */
    SD_WriteByte(SD_TOKEN_WRITE);
    for (i = 0; i < SD_BLOCK_SIZE; i++)
    {
        SD_WriteByte(buf[i]);
    }
    SD_WriteByte(0xFF);
    SD_WriteByte(0xFF);

    /* 数据响应：xxx0 0101 = 写入成功 */
    timeout = 200000;
    do
    {
        resp = SD_ReadByte();
    } while ((resp == 0xFF) && (--timeout > 0));

    if ((resp & 0x1F) != 0x05)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 2;
    }

    /* 等待忙释放（SD 内部编程，读回 0x00 直到 0xFF） */
    timeout = 2000000;
    do
    {
        resp = SD_ReadByte();
    } while ((resp == 0x00) && (--timeout > 0));

    GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
    SD_WriteByte(SD_DUMMY);
    return 0;
}

/*=========================== 卡信息读取 =================================*/

/**
  * @brief  读取 CSD 寄存器（CMD9），并顺便解析容量
  * @param  csd: 输出缓冲（16 字节），可为 NULL 只更新容量
  * @retval 0 成功；非 0 失败
  */
uint8_t SD_ReadCSD(uint8_t *csd)
{
    uint8_t  tab[16], r1;
    uint32_t i, c_size, c_size_mult, read_bl_len;

    GPIO_ResetBits(SD_CS_GPIO, SD_CS_PIN);

    r1 = SD_SendCmd(SD_CMD9, 0, 0xFF);
    if (r1 != 0x00)
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 1;
    }

    if (!SD_WaitToken(SD_TOKEN_READ, 200000))
    {
        GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
        SD_WriteByte(SD_DUMMY);
        return 2;
    }

    for (i = 0; i < 16; i++)
    {
        tab[i] = SD_ReadByte();
    }
    (void)SD_ReadByte();
    (void)SD_ReadByte();

    GPIO_SetBits(SD_CS_GPIO, SD_CS_PIN);
    SD_WriteByte(SD_DUMMY);

    if (csd) for (i = 0; i < 16; i++) csd[i] = tab[i];

    /* 解析容量（512 字节/扇区） */
    if ((tab[0] & 0xC0) == 0x00)        /* CSD 版本 1：SDSC */
    {
        c_size       = ((uint32_t)(tab[6] & 0x03) << 10) |
                       ((uint32_t)tab[7] << 2) |
                       ((uint32_t)(tab[8] & 0xC0) >> 6);
        c_size_mult  = ((uint32_t)(tab[9] & 0x03) << 1) |
                       ((uint32_t)(tab[10] & 0x80) >> 7);
        read_bl_len  = (uint32_t)(tab[5] & 0x0F);
        s_sector_count = (c_size + 1) * (1UL << (c_size_mult + 2)) * (1UL << read_bl_len) / SD_BLOCK_SIZE;
    }
    else                                /* CSD 版本 2：SDHC/SDXC */
    {
        c_size = ((uint32_t)(tab[7] & 0x3F) << 16) |
                 ((uint32_t)tab[8] << 8) |
                 (uint32_t)tab[9];
        s_sector_count = (c_size + 1) * 1024UL;   /* 每块 512B，共 (c_size+1)*512KB */
    }

    return 0;
}

/**
  * @brief  获取 SD 卡总扇区数
  */
uint32_t SD_GetSectorCount(void)
{
    return s_sector_count;
}
