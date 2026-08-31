/**
  ******************************************************************************
  * @file    ssd1306.c
  * @brief   SSD1306 OLED 驱动实现（软件 I2C + 全屏显存）
  *
  *          层次结构（自底向上）：
  *            软件 I2C 位带  ->  命令/数据写  ->  初始化序列
  *            ->  显存（1KB）绘制  ->  整屏刷屏
  ******************************************************************************
  */

#include "ssd1306.h"
#include "bsp_delay.h"
#include "fonts.h"

/*============================== 私有定义 ===============================*/

/* I2C 写地址 = 7 位地址左移 1 位，最低位 0（写方向） */
#define SSD1306_I2C_WR   ((uint8_t)((SSD1306_I2C_ADDR << 1) | 0x00))

/* 控制字节：0x00 后跟命令；0x40 后跟数据 */
#define SSD1306_CTRL_CMD     0x00
#define SSD1306_CTRL_DATA    0x40

/* 显存大小：128 * 64 / 8 = 1024 字节 */
#define SSD1306_FB_SIZE   ((SSD1306_WIDTH * SSD1306_HEIGHT) / 8)

/*============================== 私有变量 ===============================*/

/* 全屏显存：s_fb[y / 8][x] 的 bit (y % 8)，bit 置 1 = 该像素点亮 */
static uint8_t s_fb[SSD1306_FB_SIZE];

/*========================== 软件 I2C 底层 ===============================*/

static void SSD1306_I2C_Delay(void)
{
    bsp_delay_us(SSD1306_I2C_HALF_PERIOD_US);
}

static void SSD1306_I2C_SCL_High(void) { GPIO_SetBits(SSD1306_SCL_GPIO, SSD1306_SCL_PIN); }
static void SSD1306_I2C_SCL_Low(void)  { GPIO_ResetBits(SSD1306_SCL_GPIO, SSD1306_SCL_PIN); }
static void SSD1306_I2C_SDA_High(void) { GPIO_SetBits(SSD1306_SDA_GPIO, SSD1306_SDA_PIN); }
static void SSD1306_I2C_SDA_Low(void)  { GPIO_ResetBits(SSD1306_SDA_GPIO, SSD1306_SDA_PIN); }

/**
  * @brief  起始条件：SCL 高时 SDA 下降沿
  */
static void SSD1306_I2C_Start(void)
{
    SSD1306_I2C_SDA_High();
    SSD1306_I2C_SCL_High();
    SSD1306_I2C_Delay();
    SSD1306_I2C_SDA_Low();
    SSD1306_I2C_Delay();
    SSD1306_I2C_SCL_Low();
    SSD1306_I2C_Delay();
}

/**
  * @brief  停止条件：SCL 高时 SDA 上升沿
  */
static void SSD1306_I2C_Stop(void)
{
    SSD1306_I2C_SDA_Low();
    SSD1306_I2C_SCL_High();
    SSD1306_I2C_Delay();
    SSD1306_I2C_SDA_High();
    SSD1306_I2C_Delay();
}

/**
  * @brief  发送一个字节（MSB 在前）
  * @note   SDA 为推挽输出，不读从机应答（SSD1306 纯写从机，无需确认）
  */
static void SSD1306_I2C_SendByte(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) { SSD1306_I2C_SDA_High(); }
        else            { SSD1306_I2C_SDA_Low(); }
        SSD1306_I2C_Delay();
        SSD1306_I2C_SCL_High();
        SSD1306_I2C_Delay();
        SSD1306_I2C_SCL_Low();
        SSD1306_I2C_Delay();
        dat <<= 1;
    }
    /* 第 9 个时钟：释放 SDA（不检测 ACK） */
    SSD1306_I2C_SDA_High();
    SSD1306_I2C_Delay();
    SSD1306_I2C_SCL_High();
    SSD1306_I2C_Delay();
    SSD1306_I2C_SCL_Low();
    SSD1306_I2C_Delay();
}

/**
  * @brief  初始化软件 I2C 引脚：SCL/SDA 推挽输出，空闲拉高
  */
static void SSD1306_I2C_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = SSD1306_SCL_PIN | SSD1306_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    SSD1306_I2C_SDA_High();
    SSD1306_I2C_SCL_High();
}

/*======================= SSD1306 命令/数据写 =============================*/

/**
  * @brief  写一个命令字节
  */
static void SSD1306_WriteCmd(uint8_t cmd)
{
    SSD1306_I2C_Start();
    SSD1306_I2C_SendByte(SSD1306_I2C_WR);
    SSD1306_I2C_SendByte(SSD1306_CTRL_CMD);
    SSD1306_I2C_SendByte(cmd);
    SSD1306_I2C_Stop();
}

/**
  * @brief  写一段数据（已含控制字节，一次 I2C 事务连续发送）
  * @param  buf: 数据指针
  * @param  len: 数据长度
  */
static void SSD1306_WriteData(const uint8_t *buf, uint16_t len)
{
    SSD1306_I2C_Start();
    SSD1306_I2C_SendByte(SSD1306_I2C_WR);
    SSD1306_I2C_SendByte(SSD1306_CTRL_DATA);
    while (len--)
    {
        SSD1306_I2C_SendByte(*buf++);
    }
    SSD1306_I2C_Stop();
}

/**
  * @brief  设置显存写入起点（页寻址模式）
  * @param  page: 0 ~ 7
  * @param  col:  0 ~ 127
  */
static void SSD1306_SetPos(uint8_t page, uint8_t col)
{
    SSD1306_WriteCmd((uint8_t)(0xB0 | (page & 0x07)));          /* 页地址 */
    SSD1306_WriteCmd((uint8_t)(0x00 | (col & 0x0F)));           /* 列低 4 位 */
    SSD1306_WriteCmd((uint8_t)(0x10 | ((col >> 4) & 0x0F)));    /* 列高 4 位 */
}

/*=========================== 公共接口实现 ================================*/

/**
  * @brief  初始化 SSD1306
  */
void SSD1306_Init(void)
{
    SSD1306_I2C_Init();
    bsp_delay_ms(100);                  /* 上电稳定 */

    /* 经典 SSD1306 128x64 初始化序列 */
    SSD1306_WriteCmd(0xAE);             /* 关闭显示 */
    SSD1306_WriteCmd(0x20);             /* 内存寻址模式 */
    SSD1306_WriteCmd(0x02);             /*   页寻址模式 */
    SSD1306_WriteCmd(0xB0);             /* 页起始地址 0 */
#if (SSD1306_COM_SCAN_INVERT)
    SSD1306_WriteCmd(0xC8);             /* COM 反向扫描（从 COM63 到 COM0） */
#else
    SSD1306_WriteCmd(0xC0);             /* COM 正常扫描（从 COM0 到 COM63） */
#endif
    SSD1306_WriteCmd(0x00);             /* 低列起始地址 */
    SSD1306_WriteCmd(0x10);             /* 高列起始地址 */
    SSD1306_WriteCmd(0x40);             /* 显示起始行 0 */
    SSD1306_WriteCmd(0x81);             /* 对比度 */
    SSD1306_WriteCmd(0xCF);             /*   0xCF（默认） */
#if (SSD1306_SEGMENT_REMAP)
    SSD1306_WriteCmd(0xA1);             /* 段重映射：列 127 映射到 SEG0 */
#else
    SSD1306_WriteCmd(0xA0);             /* 非重映射：列 0 映射到 SEG0（上电默认） */
#endif
    SSD1306_WriteCmd(0xA6);             /* 正常显示（非反色） */
    SSD1306_WriteCmd(0xA8);             /* 多路复用比 */
    SSD1306_WriteCmd(0x3F);             /*   1/64 duty */
    SSD1306_WriteCmd(0xA4);             /* 从 RAM 显示 */
    SSD1306_WriteCmd(0xD3);             /* 显示偏移 */
    SSD1306_WriteCmd(0x00);             /*   0 */
    SSD1306_WriteCmd(0xD5);             /* 时钟分频/振荡频率 */
    SSD1306_WriteCmd(0x80);             /*   默认 */
    SSD1306_WriteCmd(0xD9);             /* 预充电周期 */
    SSD1306_WriteCmd(0xF1);             /*   默认 */
    SSD1306_WriteCmd(0xDA);             /* COM 引脚硬件配置 */
    SSD1306_WriteCmd(0x12);             /*   默认（128x64 常用 0x12） */
    SSD1306_WriteCmd(0xDB);             /* VCOMH 电压 */
    SSD1306_WriteCmd(0x40);             /*   默认 */
    SSD1306_WriteCmd(0x8D);             /* 电荷泵 */
    SSD1306_WriteCmd(0x14);             /*   开启 */
    SSD1306_WriteCmd(0xAF);             /* 打开显示 */

    SSD1306_Clear();
    SSD1306_Display();
}

/**
  * @brief  清空显存
  */
void SSD1306_Clear(void)
{
    uint16_t i;

    for (i = 0; i < SSD1306_FB_SIZE; i++)
    {
        s_fb[i] = 0x00;
    }
}

/**
  * @brief  整屏刷显存：逐页写入（页寻址模式）
  * @note   每页 128 字节，共 8 页；页面内按列从左到右
  */
void SSD1306_Display(void)
{
    uint8_t page;

    for (page = 0; page < 8; page++)
    {
        SSD1306_SetPos(page, 0);
        SSD1306_WriteData(&s_fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
    }
}

/**
  * @brief  设置对比度
  */
void SSD1306_SetContrast(uint8_t value)
{
    SSD1306_WriteCmd(0x81);
    SSD1306_WriteCmd(value);
}

/**
  * @brief  反转显示
  */
void SSD1306_InvertDisplay(uint8_t invert)
{
    SSD1306_WriteCmd(invert ? 0xA7 : 0xA6);
}

/**
  * @brief  绘制单像素（写显存）
  */
void SSD1306_DrawPixel(uint16_t x, uint16_t y, uint8_t on)
{
    uint16_t idx;

    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;

    idx = (y / 8) * SSD1306_WIDTH + x;
    if (on)
    {
        s_fb[idx] |=  (uint8_t)(1u << (y % 8));
    }
    else
    {
        s_fb[idx] &= (uint8_t)~(1u << (y % 8));
    }
}

/**
  * @brief  绘制直线（Bresenham 算法）
  */
void SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    int16_t dx, dy, sx, sy, err, e2;

    dx = (x1 > x0) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    dy = (y1 > y0) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;)
    {
        SSD1306_DrawPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 = (uint16_t)(x0 + sx); }
        if (e2 <  dx) { err += dx; y0 = (uint16_t)(y0 + sy); }
    }
}

/**
  * @brief  绘制矩形边框
  */
void SSD1306_DrawRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    SSD1306_DrawLine(x0, y0, x1, y0, on);
    SSD1306_DrawLine(x1, y0, x1, y1, on);
    SSD1306_DrawLine(x1, y1, x0, y1, on);
    SSD1306_DrawLine(x0, y1, x0, y0, on);
}

/**
  * @brief  填充矩形区域（含边界）
  */
void SSD1306_FillRect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t on)
{
    uint16_t x, y;

    if (x0 > x1) { uint16_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { uint16_t t = y0; y0 = y1; y1 = t; }

    for (y = y0; y <= y1; y++)
    {
        for (x = x0; x <= x1; x++)
        {
            SSD1306_DrawPixel(x, y, on);
        }
    }
}

/**
  * @brief  绘制圆形边框（中点画圆法）
  */
void SSD1306_DrawCircle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t on)
{
    int16_t x = 0;
    int16_t y = (int16_t)r;
    int16_t d = 3 - 2 * (int16_t)r;

    while (x <= y)
    {
        SSD1306_DrawPixel((uint16_t)(cx + x), (uint16_t)(cy + y), on);
        SSD1306_DrawPixel((uint16_t)(cx - x), (uint16_t)(cy + y), on);
        SSD1306_DrawPixel((uint16_t)(cx + x), (uint16_t)(cy - y), on);
        SSD1306_DrawPixel((uint16_t)(cx - x), (uint16_t)(cy - y), on);
        SSD1306_DrawPixel((uint16_t)(cx + y), (uint16_t)(cy + x), on);
        SSD1306_DrawPixel((uint16_t)(cx - y), (uint16_t)(cy + x), on);
        SSD1306_DrawPixel((uint16_t)(cx + y), (uint16_t)(cy - x), on);
        SSD1306_DrawPixel((uint16_t)(cx - y), (uint16_t)(cy - x), on);

        if (d < 0) { d = d + 4 * x + 6; }
        else       { d = d + 4 * (x - y) + 10; y--; }
        x++;
    }
}

/**
  * @brief  绘制实心圆（逐点判定）
  */
void SSD1306_FillCircle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t on)
{
    int16_t x, y;

    for (y = -(int16_t)r; y <= (int16_t)r; y++)
    {
        for (x = -(int16_t)r; x <= (int16_t)r; x++)
        {
            if ((int32_t)x * x + (int32_t)y * y <= (int32_t)r * r)
            {
                SSD1306_DrawPixel((uint16_t)(cx + x), (uint16_t)(cy + y), on);
            }
        }
    }
}

/*=========================== 公共接口：文本显示 =========================*/

/* 字体信息（私有） */
typedef struct
{
    const uint8_t *data;    /* 字形数据首地址 */
    uint8_t        width;   /* 字形宽度（像素） */
    uint8_t        height;  /* 字形高度（像素） */
    uint8_t        scale;   /* 放大倍数 */
    uint8_t        lsb_first;  /* 位序：1=bit0 为最左像素；0=bit7 为最左像素 */
} ssd1306_font_t;

/**
  * @brief  根据 size 获取字体信息
  * @note   两种字库位序不同：
  *         font_8x16（VGA ROM）是 MSB-first，bit7 = 最左像素；
  *         font_8x8（font8x8_basic）是 LSB-first，bit0 = 最左像素。
  *         解析时必须按各自位序取位，否则 8x8 字符会左右镜像。
  */
static void SSD1306_GetFont(uint8_t size, ssd1306_font_t *font)
{
    if (size == 1)
    {
        font->data      = &font_8x8[0][0];
        font->width     = FONT_8X8_WIDTH;
        font->height    = FONT_8X8_HEIGHT;
        font->scale     = 1;
        font->lsb_first = 1;        /* 8x8：bit0 为最左像素 */
    }
    else
    {
        font->data      = &font_8x16[0][0];
        font->width     = FONT_8X16_WIDTH;
        font->height    = FONT_8X16_HEIGHT;
        font->scale     = size / 2;
        if (font->scale == 0) font->scale = 1;
        font->lsb_first = 0;        /* 8x16：bit7 为最左像素 */
    }
}

/**
  * @brief  获取字符前进宽度
  */
static uint16_t SSD1306_FontAdvance(uint8_t size)
{
    ssd1306_font_t font;

    SSD1306_GetFont(size, &font);
    return (uint16_t)font.width * font.scale;
}

/**
  * @brief  显示单个 ASCII 字符（逐像素写入显存）
  */
void SSD1306_ShowChar(uint16_t x, uint16_t y, char ch, uint8_t size, uint8_t on)
{
    ssd1306_font_t font;
    const uint8_t *glyph;
    uint16_t fw, fh, row, col, sx, sy;

    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
    if (ch < 0x20 || ch > 0x7E) ch = '?';

    SSD1306_GetFont(size, &font);
    fw = font.width * font.scale;
    fh = font.height * font.scale;
    if ((uint32_t)x + fw > SSD1306_WIDTH || (uint32_t)y + fh > SSD1306_HEIGHT) return;

    glyph = font.data + ((uint8_t)ch - 0x20) * font.height;

    for (row = 0; row < font.height; row++)
    {
        for (sy = 0; sy < font.scale; sy++)
        {
            for (col = 0; col < font.width; col++)
            {
                uint8_t bit;
                if (font.lsb_first)
                {
                    bit = (uint8_t)((glyph[row] >> col) & 0x01);          /* 8x8：bit0 在左 */
                }
                else
                {
                    bit = (uint8_t)((glyph[row] >> (7 - col)) & 0x01);    /* 8x16：bit7 在左 */
                }
                for (sx = 0; sx < font.scale; sx++)
                {
                    SSD1306_DrawPixel(x + col * font.scale + sx,
                                      y + row * font.scale + sy,
                                      bit ? on : SSD1306_COLOR_OFF);
                }
            }
        }
    }
}

/**
  * @brief  显示字符串（'\n' 换行，回到起始列）
  */
void SSD1306_ShowString(uint16_t x, uint16_t y, uint8_t size, const char *str, uint8_t on)
{
    uint16_t x0 = x;
    uint16_t adv = SSD1306_FontAdvance(size);
    uint16_t line_h;
    ssd1306_font_t font;

    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;

    SSD1306_GetFont(size, &font);
    line_h = font.height * font.scale;

    while (*str)
    {
        if (*str == '\n')
        {
            y += line_h;
            x  = x0;
            str++;
            continue;
        }
        SSD1306_ShowChar(x, y, *str, size, on);
        x += adv;
        if ((uint32_t)x + adv > SSD1306_WIDTH) break;
        str++;
    }
}

/**
  * @brief  显示无符号整数（定长，前导补零）
  */
void SSD1306_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t on)
{
    char buf[11];
    uint8_t i;

    if (len > 10) len = 10;
    for (i = len; i > 0; i--)
    {
        buf[i - 1] = (char)('0' + num % 10);
        num /= 10;
    }
    buf[len] = '\0';
    SSD1306_ShowString(x, y, size, buf, on);
}

/**
  * @brief  显示有符号 32 位整数（变长）
  */
void SSD1306_ShowInt32(uint16_t x, uint16_t y, int32_t value, uint8_t size, uint8_t on)
{
    char buf[12];
    char *p = buf;
    uint32_t v;
    char tmp[11];
    uint8_t n = 0;

    if (value < 0) { *p++ = '-'; v = (uint32_t)(-(value + 1)) + 1; }
    else           { v = (uint32_t)value; }

    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = tmp[--n];
    *p = '\0';

    SSD1306_ShowString(x, y, size, buf, on);
}

/**
  * @brief  显示浮点数（固定小数位，四舍五入；width 补空格右对齐）
  */
void SSD1306_ShowFloat(uint16_t x, uint16_t y, float value, uint8_t dec, uint8_t width,
                       uint8_t size, uint8_t on)
{
    char buf[16];
    char *p = buf;
    char tmp[11];
    uint32_t p10 = 1, ip, dp, div;
    uint8_t i, n = 0, pad;

    if (value < 0) { *p++ = '-'; value = -value; }
    if (value > 9999999.0f) value = 9999999.0f;

    for (i = 0; i < dec; i++) p10 *= 10;

    ip = (uint32_t)value;
    dp = (uint32_t)((value - (float)ip) * (float)p10 + 0.5f);
    if (dp >= p10) { dp = 0; ip++; }

    do { tmp[n++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
    while (n) *p++ = tmp[--n];

    if (dec)
    {
        *p++ = '.';
        div = p10 / 10;
        for (i = 0; i < dec; i++)
        {
            *p++ = (char)('0' + (dp / div) % 10);
            div /= 10;
        }
    }
    *p = '\0';

    /* 不足 width 时补空格（右对齐），width=0 时不补 */
    if (width)
    {
        uint8_t len = (uint8_t)(p - buf);
        if (len < width)
        {
            char pad_tmp[16];
            for (i = 0; i < len; i++) pad_tmp[i] = buf[i];
            pad = width - len;
            for (i = 0; i < pad; i++) buf[i] = ' ';
            for (i = 0; i < len; i++) buf[pad + i] = pad_tmp[i];
            buf[width] = '\0';
        }
    }

    SSD1306_ShowString(x, y, size, buf, on);
}

/**
  * @brief  显示十六进制数（定长，前导补零，大写）
  */
void SSD1306_ShowHex(uint16_t x, uint16_t y, uint32_t value, uint8_t len, uint8_t size, uint8_t on)
{
    char buf[9];
    static const char hex[] = "0123456789ABCDEF";
    uint8_t i;

    if (len > 8) len = 8;
    for (i = 0; i < len; i++)
    {
        buf[len - 1 - i] = hex[value & 0x0F];
        value >>= 4;
    }
    buf[len] = '\0';
    SSD1306_ShowString(x, y, size, buf, on);
}
