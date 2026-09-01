/**
  ******************************************************************************
  * @file    sd_dl.c
  * @brief   SD 卡 TTL 下载实现（串口接收 -> FatFS 写入 /VIDEO/）
  *
  *          上位机: python video_to_bin.py ... --send-sd
  *
  *          帧协议（电脑 -> STM32，USART1 115200 8N1）：
  *            [0xA5][0x5A][CMD][payload...][SUM]
  *            SUM = (CMD + 所有 payload 字节) & 0xFF
  *
  *            CMD 0x01 BEGIN : payload = 文件名(12B, 8.3 短名, 不足补 0)
  *                                        + 文件大小(4B 小端)
  *            CMD 0x02 DATA  : payload = 长度(2B 小端) + 数据(<=512B)
  *            CMD 0x03 END   : payload = 总字节数(4B 小端)
  *            CMD 0x04 ABORT : payload = 无
  *
  *          应答（STM32 -> 电脑）：0x06 = ACK，0x15 = NAK（校验错/写卡错）
  *          每收到一帧处理成功后回 ACK，电脑收到 ACK 才发下一帧（流控 + 检错）。
  *
  *          进入本模式后 USART1 RXNE 中断（手柄解析）被暂时关闭，
  *          用轮询方式独占接收；退出时恢复，手柄照常工作。
  ******************************************************************************
  */

#include "sd_dl.h"
#include "ssd1306.h"
#include "bsp_delay.h"
#include "ff.h"
#include <string.h>

/*============================== 私有定义 ===============================*/

#define DL_MAGIC0       0xA5
#define DL_MAGIC1       0x5A
#define DL_CMD_BEGIN    0x01
#define DL_CMD_DATA     0x02
#define DL_CMD_END      0x03
#define DL_CMD_ABORT    0x04
#define DL_ACK          0x06
#define DL_NAK          0x15

#define DL_DATA_MAX     512             /* 单块数据最大字节(长度字段 2B 小端) */
#define DL_BYTE_TIMEOUT 1000            /* 单字节超时 ms */
#define DL_BEGIN_TIMEOUT 600000         /* 等待 BEGIN 帧超时 ms (10 分钟, 给足电脑端操作时间) */

#define DL_DIR          "0:/VIDEO"      /* 视频分区目录 */
#define DL_PATH_PRE     "0:/VIDEO/"     /* 路径前缀 */

/*============================== 私有变量 ===============================*/

static uint8_t s_name[13];              /* 8.3 短文件名（含结尾 0） */
static uint8_t s_total[4];              /* BEGIN 里声明的文件大小(小端) */

/*============================== 底层收发 ===============================*/

/**
  * @brief  发送一个字节（等待 TXE）
  */
static void DL_Tx(uint8_t b)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    USART_SendData(USART1, b);
}

/**
  * @brief  轮询接收一个字节
  * @note   USART1 无 FIFO(仅 1 字节 RXNE): 必须忙轮询快速读走每个字节,
  *         否则 115200 下(87us/字节)未及时读取的字节会溢出丢失(ORE)。
  *         超时由 SysTick 计时, 内部不阻塞延时。
  * @retval 0 成功；1 超时
  */
static uint8_t DL_RxByte(uint8_t *b)
{
    uint32_t t0 = g_tick_ms;

    while ((uint32_t)(g_tick_ms - t0) < DL_BYTE_TIMEOUT)
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
        {
            *b = (uint8_t)USART_ReceiveData(USART1);
            return 0;
        }
        /* 忙轮询: 不延时, 及时读走每个字节, 避免溢出丢字节 */
    }
    return 1;   /* 超时 */
}

/**
  * @brief  接收一帧完整数据
  * @param  cmd:    命令字节输出
  * @param  payload: 数据缓冲（>=DL_DATA_MAX+1）
  * @param  plen:   实际 payload 长度输出
  * @retval 0 成功；1 超时；2 命令非法/长度超限；3 校验和错误
  * @note   帧头用字节级滑动同步：残留/噪声字节会被自动跳过，
  *         直到对齐 0xA5 0x5A，不会因关中断前的半个手柄帧而错位。
  */
static uint8_t DL_RxFrame(uint8_t *cmd, uint8_t *payload, uint16_t *plen)
{
    uint8_t b, sum;
    uint16_t i, len = 0;

    /* 帧头：滑动同步找 0xA5 0x5A */
    for (;;)
    {
        if (DL_RxByte(&b) != 0) return 1;       /* 超时 */
        if (b != DL_MAGIC0) continue;           /* 丢弃噪声，继续找 0xA5 */
        if (DL_RxByte(&b) != 0) return 1;       /* 等 0x5A */
        if (b == DL_MAGIC1) break;              /* 帧头就位 */
        if (b == DL_MAGIC0) continue;           /* 连续 0xA5：重等下一字节 */
        /* 不是 0x5A：回到找 0xA5 的状态继续 */
    }

    /* 命令 */
    if (DL_RxByte(&b) != 0) return 1;
    *cmd = b;
    sum = b;

    /* 按命令取 payload 长度 */
    if (*cmd == DL_CMD_BEGIN)      len = 12 + 4;          /* 文件名 + 大小 */
    else if (*cmd == DL_CMD_DATA)
    {
        if (DL_RxByte(&b) != 0) return 1;   /* 长度低字节 */
        len = b;
        sum += b;
        if (DL_RxByte(&b) != 0) return 1;   /* 长度高字节 */
        len |= (uint16_t)(b << 8);
        sum += b;
        if (len == 0 || len > DL_DATA_MAX) return 2;
    }
    else if (*cmd == DL_CMD_END)   len = 4;
    else if (*cmd == DL_CMD_ABORT) len = 0;
    else return 2;

    /* payload */
    for (i = 0; i < len; i++)
    {
        if (DL_RxByte(&b) != 0) return 1;
        payload[i] = b;
        sum += b;
    }
    *plen = len;

    /* 校验和 */
    if (DL_RxByte(&b) != 0) return 1;
    if (b != (sum & 0xFF)) return 3;

    return 0;
}

/*============================= 公共接口 =============================*/

/**
  * @brief  运行 SD 卡下载应用（阻塞）
  */
void SD_DL_Run(void)
{
    uint8_t  cmd;
    uint8_t  payload[DL_DATA_MAX + 1];
    uint16_t plen;
    uint8_t  r;
    char     path[24];
    FRESULT  fr;
    FIL      f;
    UINT     bw;
    uint32_t declared, written, i;
    uint32_t t0;

    /* 初始化界面 */
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 1, "SD WRITE /VIDEO", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 16, 1, "DL v2 WAIT PC...", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 32, 1, "run: video_to_bin", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 48, 1, "--send-sd", SSD1306_COLOR_ON);
    SSD1306_Display();

    /* 暂时关闭手柄 RXNE 中断，独占 USART1 轮询接收 */
    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);

    /* 清掉接收缓冲残留(关中断前可能已收到半个手柄帧), 避免污染帧头同步 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
    {
        (void)USART_ReceiveData(USART1);
    }
    USART_ClearFlag(USART1, USART_FLAG_ORE);   /* 清溢出标志 */
    USART_ClearITPendingBit(USART1, USART_IT_RXNE);

    /* ---- 等待 BEGIN 帧（超时返回） ----
       心跳: 收到任何电脑字节前, 每 500ms 从 PA9 发 0x55,
       电脑端 `--listen` 能看到 0x55 即证明 PA9->RX 接线通 + 固件是新版。 */
    {
        uint8_t  beat_on = 1;
        uint32_t t_beat = g_tick_ms;
        t0 = g_tick_ms;
        while ((uint32_t)(g_tick_ms - t0) < DL_BEGIN_TIMEOUT)
        {
            if (beat_on && (uint32_t)(g_tick_ms - t_beat) >= 500)
            {
                t_beat = g_tick_ms;
                DL_Tx(0x55);
            }
            r = DL_RxFrame(&cmd, payload, &plen);
            if (r != 1) beat_on = 0;    /* 收到过任何字节: 停心跳, 避免干扰 ACK */
            if (r == 0 && cmd == DL_CMD_BEGIN) break;
            /* 未知帧/校验错/帧头错：回 NAK 让电脑重发(不能静默, 否则电脑一直等 ACK) */
            if (r == 0 || r == 2 || r == 3) DL_Tx(DL_NAK);
        }
    }
    if (r != 0 || cmd != DL_CMD_BEGIN)
    {
        goto dl_timeout;
    }

    /* BEGIN：取出 8.3 文件名 + 声明大小 */
    for (i = 0; i < 12 && payload[i] != 0; i++) s_name[i] = payload[i];
    s_name[i] = '\0';
    for (i = 0; i < 4; i++) s_total[i] = payload[12 + i];
    declared = (uint32_t)s_total[0] | ((uint32_t)s_total[1] << 8) |
               ((uint32_t)s_total[2] << 16) | ((uint32_t)s_total[3] << 24);

    /* 拼路径 "0:/VIDEO/NAME.BIN" */
    i = 0;
    {
        static const char pre[] = DL_PATH_PRE;
        while (pre[i] && i < 19) { path[i] = pre[i]; i++; }
    }
    {
        uint8_t j = 0;
        while (s_name[j] && i < 23) { path[i++] = s_name[j++]; }
    }
    path[i] = '\0';

    /* 显示文件名 */
    SSD1306_ShowString(0, 16, 1, "FILE:", SSD1306_COLOR_ON);
    SSD1306_ShowString(40, 16, 1, (const char *)s_name, SSD1306_COLOR_ON);
    SSD1306_Display();

    /* 打开文件（覆盖写） */
    fr = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        DL_Tx(DL_NAK);
        SSD1306_Clear();
        SSD1306_ShowString(0, 0, 2, "SD OPEN FAIL", SSD1306_COLOR_ON);
        SSD1306_Display();
        goto dl_end_wait;
    }
    DL_Tx(DL_ACK);      /* BEGIN 成功 */

    /* ---- 收数据帧 ---- */
    written = 0;
    while (1)
    {
        r = DL_RxFrame(&cmd, payload, &plen);
        if (r == 1)     /* 超时：认为电脑已断开 */
        {
            DL_Tx(DL_NAK);
            f_close(&f);
            f_unlink(path);         /* 删除半截文件 */
            goto dl_fail;
        }
        if (r == 3)
        {
            DL_Tx(DL_NAK);          /* 校验错：让电脑重发 */
            continue;
        }
        if (r != 0)
        {
            DL_Tx(DL_NAK);
            continue;
        }

        if (cmd == DL_CMD_DATA)
        {
            fr = f_write(&f, payload, plen, &bw);
            if (fr != FR_OK || bw != plen)
            {
                DL_Tx(DL_NAK);
                f_close(&f);
                f_unlink(path);
                goto dl_fail;
            }
            written += plen;
            DL_Tx(DL_ACK);
            /* 注意: 传输中不要刷 OLED(软件 SPI 阻塞 5~10ms 会丢串口字节),
               进度看电脑端即可, 结束统一显示结果 */
        }
        else if (cmd == DL_CMD_END)
        {
            /* 校验总字节数 */
            if (written == declared)
            {
                fr = f_sync(&f);
                f_close(&f);
                DL_Tx(DL_ACK);
                SSD1306_Clear();
                SSD1306_ShowString(0, 0, 2, "DONE!", SSD1306_COLOR_ON);
                SSD1306_ShowString(0, 24, 1, (const char *)s_name, SSD1306_COLOR_ON);
                SSD1306_ShowNum(0, 40, written / 1024UL, 5, 1, SSD1306_COLOR_ON);
                SSD1306_ShowString(48, 40, 1, "KB", SSD1306_COLOR_ON);
                SSD1306_Display();
                goto dl_end_wait;
            }
            else
            {
                DL_Tx(DL_NAK);
                f_close(&f);
                f_unlink(path);
                goto dl_fail;
            }
        }
        else if (cmd == DL_CMD_ABORT)
        {
            f_close(&f);
            f_unlink(path);
            DL_Tx(DL_ACK);
            SSD1306_Clear();
            SSD1306_ShowString(0, 0, 2, "ABORT", SSD1306_COLOR_ON);
            SSD1306_Display();
            goto dl_end_wait;
        }
        else
        {
            DL_Tx(DL_NAK);
        }
    }

dl_fail:
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "FAIL", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 24, 1, "check wiring", SSD1306_COLOR_ON);
    SSD1306_Display();
    goto dl_end_wait;

dl_timeout:
    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 2, "TIMEOUT", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 24, 1, "no PC data", SSD1306_COLOR_ON);
    SSD1306_Display();
    goto dl_end_wait;

dl_end_wait:
    /* 恢复手柄接收 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* 停留 2 秒让用户看到结果，然后返回菜单 */
    t0 = g_tick_ms;
    while ((uint32_t)(g_tick_ms - t0) < 2000)
    {
        bsp_delay_ms(10);
    }
    SSD1306_Clear();
    SSD1306_Display();
}
