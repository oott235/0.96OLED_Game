/**
  ******************************************************************************
  * @file    signal.c
  * @brief   信号发生器实现（USB CDC 虚拟串口 / GPIO 方波输出）
  *
  *          波形生成：256 点波形表（12 位数值），正弦/三角/方波/锯齿，
  *          供 USB 模式发送给 vofa+ 显示。
  *
  *          输出模式：
  *            1 USB    micro USB 口直连电脑，USB CDC 虚拟串口 + vofa+
  *                     （JustFloat 协议）实时显示波形
  *            2 GPIO   PA4 推挽输出方波（GPIO 翻转，无需 DAC/TIM），
  *                     频率/占空比可调
  *
  *          说明：本方案不依赖 DAC/DMA/TIM 等 APB1 外设（部分板子
  *         APB1 域异常时 DAC 不可用），方波直接用 GPIO 翻转产生。
  ******************************************************************************
  */

#include "signal.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "usb_conf.h"
#include "hw_config.h"

/* CDC 发送完成标志（usb_endp.c 的 EP1_IN_Callback 置位） */
extern volatile uint32_t packet_sent;

/*============================== 私有定义 ===============================*/

#define WAVE_N          256     /* 波形表点数 */

/* GPIO 输出引脚 PA4 */
#define SIG_GPIO        GPIOA
#define SIG_PIN         GPIO_Pin_4

/* 波形类型 */
#define WAVE_SIN        0
#define WAVE_TRI        1
#define WAVE_SQR        2
#define WAVE_SAW        3
#define WAVE_COUNT      4

/* 参数项 */
#define PARAM_FREQ      0
#define PARAM_AMP       1
#define PARAM_DUTY      2
#define PARAM_COUNT     3

/* 频率范围（Hz）—— GPIO 翻转方案：上限 50kHz（受翻转速度限制） */
#define FREQ_MIN        10
#define FREQ_MAX        50000UL

/* JustFloat 帧尾 */
static const uint8_t s_vofa_tail[4] = { 0x00, 0x00, 0x80, 0x7F };

/* 正弦查找表（Q11：值 = 2048*sin(2πi/256)，i=0..255） */
static const int16_t s_sin_lut[WAVE_N] =
{
     0,   50,  100,  151,  201,  251,  301,  350,  400,  449,  498,  546,  595,  642,  690,  737,
   784,  830,  876,  921,  965, 1009, 1053, 1096, 1138, 1179, 1220, 1260, 1299, 1338, 1375, 1412,
  1448, 1483, 1517, 1551, 1583, 1615, 1645, 1674, 1703, 1730, 1757, 1782, 1806, 1829, 1851, 1872,
  1892, 1911, 1928, 1945, 1960, 1974, 1987, 1998, 2009, 2018, 2026, 2033, 2038, 2042, 2046, 2047,
  2048, 2047, 2046, 2042, 2038, 2033, 2026, 2018, 2009, 1998, 1987, 1974, 1960, 1945, 1928, 1911,
  1892, 1872, 1851, 1829, 1806, 1782, 1757, 1730, 1703, 1674, 1645, 1615, 1583, 1551, 1517, 1483,
  1448, 1412, 1375, 1338, 1299, 1260, 1220, 1179, 1138, 1096, 1053, 1009,  965,  921,  876,  830,
   784,  737,  690,  642,  595,  546,  498,  449,  400,  350,  301,  251,  201,  151,  100,   50,
     0,  -50, -100, -151, -201, -251, -301, -350, -400, -449, -498, -546, -595, -642, -690, -737,
  -784, -830, -876, -921, -965,-1009,-1053,-1096,-1138,-1179,-1220,-1260,-1299,-1338,-1375,-1412,
 -1448,-1483,-1517,-1551,-1583,-1615,-1645,-1674,-1703,-1730,-1757,-1782,-1806,-1829,-1851,-1872,
 -1892,-1911,-1928,-1945,-1960,-1974,-1987,-1998,-2009,-2018,-2026,-2033,-2038,-2042,-2046,-2047,
 -2048,-2047,-2046,-2042,-2038,-2033,-2026,-2018,-2009,-1998,-1987,-1974,-1960,-1945,-1928,-1911,
 -1892,-1872,-1851,-1829,-1806,-1782,-1757,-1730,-1703,-1674,-1645,-1615,-1583,-1551,-1517,-1483,
 -1448,-1412,-1375,-1338,-1299,-1260,-1220,-1179,-1138,-1096,-1053,-1009, -965, -921, -876, -830,
  -784, -737, -690, -642, -595, -546, -498, -449, -400, -350, -301, -251, -201, -151, -100,  -50
};

/*============================== 私有变量 ===============================*/

static uint16_t s_wave[WAVE_N];         /* 波形表 */
static uint8_t  s_wave_type = WAVE_SIN; /* 当前波形类型 */
static uint32_t s_freq = 1000;          /* 输出频率（Hz） */
static uint8_t  s_amp  = 100;           /* 幅度百分比 0~100（USB 显示用） */
static uint8_t  s_duty = 50;            /* 方波占空比 5~95（GPIO 输出用） */
static uint8_t  s_param = PARAM_FREQ;   /* 当前调节参数项 */

/*============================= 波形生成 =================================*/

/**
  * @brief  按当前波形类型/幅度/占空比重建波形表
  */
static void Wave_Rebuild(void)
{
    uint16_t i;
    int32_t  mid = 2048;
    int32_t  amp = (int32_t)s_amp * 2048 / 100;
    if (amp < 16) amp = 16;

    for (i = 0; i < WAVE_N; i++)
    {
        int32_t v;

        switch (s_wave_type)
        {
        case WAVE_SIN:
            v = mid + amp * (int32_t)s_sin_lut[i] / 2048;
            break;

        case WAVE_TRI:
            if (i < 64)
                v = mid - amp + 2 * amp * (int32_t)i / 64;
            else if (i < 192)
                v = mid + amp - 2 * amp * (int32_t)(i - 64) / 128;
            else
                v = mid - amp + 2 * amp * (int32_t)(i - 192) / 64;
            break;

        case WAVE_SQR:
            v = (i < (uint16_t)((uint32_t)s_duty * WAVE_N / 100)) ?
                (mid + amp) : (mid - amp);
            break;

        case WAVE_SAW:
        default:
            v = mid - amp + 2 * amp * (int32_t)i / (WAVE_N - 1);
            break;
        }

        if (v < 0)    v = 0;
        if (v > 4095) v = 4095;
        s_wave[i] = (uint16_t)v;
    }
}

/**
  * @brief  频率步进（按量级自适应）
  */
static void Freq_Step(int8_t dir)
{
    uint32_t step;

    if (s_freq < 100)      step = 1;
    else if (s_freq < 1000) step = 10;
    else if (s_freq < 10000) step = 100;
    else if (s_freq < 30000) step = 1000;
    else                    step = 5000;

    if (dir > 0)
    {
        if (s_freq + step <= FREQ_MAX) s_freq += step;
        else                           s_freq = FREQ_MAX;
    }
    else
    {
        if (s_freq > step) s_freq -= step;
        else               s_freq = FREQ_MIN;
    }
}

/*============================= 模式 2：GPIO 方波 ========================*/

/**
  * @brief  初始化 PA4 为推挽输出（GPIO 方波）
  */
static void Signal_GpioInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC->APB2ENR |= ((uint32_t)0x00000004);   /* GPIOA 时钟 */

    gpio.GPIO_Pin   = SIG_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SIG_GPIO, &gpio);
    GPIO_ResetBits(SIG_GPIO, SIG_PIN);
}

/**
  * @brief  模式 2：GPIO PWM 波形输出（阻塞，X 退出）
  * @note   PA4 输出高频 PWM（载波约 25kHz），占空比按波形表逐点调制，
  *         经外部 RC 低通滤波后即为平滑模拟波形；示波器可看 PWM 包络。
  *         支持正弦/三角/方波/锯齿，频率/幅度/占空比可调。
  *         PWM 载波：PWM_PERIOD_US = 40us（25kHz），占空比分辨率约 2.5%
  */
static void Signal_GpioRun(void)
{
    uint8_t draw = 1;
    uint32_t last_ui_ms = 0;
    static const char *const names[WAVE_COUNT] = { "SIN", "TRI", "SQR", "SAW" };

    Signal_GpioInit();

    while (1)
    {
        uint16_t idx;
        uint32_t duty_pct, hi_us, lo_us, n_cycle;

        /* 波形周期内的采样点数：每个 PWM 周期输出一个波形采样点
           采样率 = 25kHz，故每波形周期点数 n_cycle = 25000 / freq */
        n_cycle = 25000UL / s_freq;
        if (n_cycle < 8)  n_cycle = 8;   /* 最少 8 点，保证波形形状 */
        if (n_cycle > 250) n_cycle = 250;

        /* 输出一个完整波形周期（逐点 PWM 调制） */
        for (idx = 0; idx < n_cycle; idx++)
        {
            /* 当前采样点在 256 点波形表中的位置 */
            uint16_t tbl = (uint16_t)((uint32_t)idx * WAVE_N / n_cycle);

            /* 占空比 = 波形值比例（0~100%） */
            duty_pct = (uint32_t)s_wave[tbl] * 100UL / 4096UL;
            if (duty_pct > 99) duty_pct = 99;
            hi_us = 40UL * duty_pct / 100UL;
            lo_us = 40UL - hi_us;

            /* 输出一个 PWM 周期 */
            GPIO_SetBits(SIG_GPIO, SIG_PIN);
            bsp_delay_us(hi_us);
            GPIO_ResetBits(SIG_GPIO, SIG_PIN);
            bsp_delay_us(lo_us);
        }

        /* ---- 按键处理（每 5ms 一次） ---- */
        if ((uint32_t)(g_tick_ms - last_ui_ms) >= 5)
        {
            last_ui_ms = g_tick_ms;

            /* A：切换参数项 */
            if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
            {
                s_param = (uint8_t)((s_param + 1) % PARAM_COUNT);
                draw = 1;
            }

            /* LT/RT：切换波形类型 */
            if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON)
            {
                s_wave_type = (s_wave_type + WAVE_COUNT - 1) % WAVE_COUNT;
                Wave_Rebuild();
                draw = 1;
            }
            else if (Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
            {
                s_wave_type = (s_wave_type + 1) % WAVE_COUNT;
                Wave_Rebuild();
                draw = 1;
            }

            /* 左右：调节参数 */
            if (Gamepad_GetDPad() & GAMEPAD_DPAD_LEFT)
            {
                if (s_param == PARAM_FREQ)      Freq_Step(-1);
                else if (s_param == PARAM_AMP)  { if (s_amp > 5)  s_amp -= 5;  Wave_Rebuild(); }
                else                            { if (s_duty > 5) s_duty -= 5; Wave_Rebuild(); }
                draw = 1;
            }
            else if (Gamepad_GetDPad() & GAMEPAD_DPAD_RIGHT)
            {
                if (s_param == PARAM_FREQ)      Freq_Step(+1);
                else if (s_param == PARAM_AMP)  { if (s_amp < 100) s_amp += 5; Wave_Rebuild(); }
                else                            { if (s_duty < 95) s_duty += 5; Wave_Rebuild(); }
                draw = 1;
            }

            /* 每 250ms 刷新界面 */
            if ((g_tick_ms & 0xFF) < 20) draw = 1;

            if (draw)
            {
                SSD1306_Clear();
                SSD1306_ShowString(0, 0, 1, "SIGNAL GEN", SSD1306_COLOR_ON);
                SSD1306_ShowString(88, 0, 1, "GPIO", SSD1306_COLOR_ON);
                SSD1306_ShowString(0, 16, 1, names[s_wave_type], SSD1306_COLOR_ON);
                SSD1306_ShowString(40, 16, 1, "PWM", SSD1306_COLOR_ON);

                SSD1306_ShowString(0, 28, 1, "F:", SSD1306_COLOR_ON);
                SSD1306_ShowNum(12, 28, s_freq, 5, 1, SSD1306_COLOR_ON);
                SSD1306_ShowString(48, 28, 1, "Hz", SSD1306_COLOR_ON);

                SSD1306_ShowString(0, 36, 1, "A:", SSD1306_COLOR_ON);
                SSD1306_ShowNum(12, 36, s_amp, 3, 1, SSD1306_COLOR_ON);
                SSD1306_ShowString(40, 36, 1, "D:", SSD1306_COLOR_ON);
                SSD1306_ShowNum(52, 36, s_duty, 3, 1, SSD1306_COLOR_ON);
                SSD1306_ShowString(76, 36, 1, "%", SSD1306_COLOR_ON);

                if (s_param == PARAM_FREQ) SSD1306_ShowString(44, 44, 1, "^", SSD1306_COLOR_ON);
                if (s_param == PARAM_AMP)  SSD1306_ShowString(8, 44, 1, "^", SSD1306_COLOR_ON);
                if (s_param == PARAM_DUTY) SSD1306_ShowString(48, 44, 1, "^", SSD1306_COLOR_ON);

                SSD1306_ShowString(0, 56, 1, "LT/RT wave", SSD1306_COLOR_ON);
                SSD1306_ShowString(64, 56, 1, "LR par X:ex", SSD1306_COLOR_ON);
                SSD1306_Display();
                draw = 0;
            }
        }

        /* X：退出 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }
    }

    GPIO_ResetBits(SIG_GPIO, SIG_PIN);
}
/*============================= 模式 1：USB CDC ========================*/

/* 发送诊断计数（显示用） */
static uint32_t s_usb_sent = 0;

/**
  * @brief  初始化 USB CDC 虚拟串口（micro USB 口直连电脑）
  */
static void Signal_UsbInit(void)
{
    fSuspendEnabled = FALSE;   /* 禁用挂起低功耗，防止进 STOP 模式睡死 */

    SSD1306_Clear();
    SSD1306_ShowString(0, 0, 1, "USB INIT", SSD1306_COLOR_ON);
    SSD1306_Display();

    Set_System();
    Set_USBClock();
    USB_Interrupts_Config();
    USB_Init();

    SSD1306_ShowString(0, 16, 1, "USB INIT OK", SSD1306_COLOR_ON);
    SSD1306_Display();
    bsp_delay_ms(500);
}

/**
  * @brief  通过 USB CDC 发送一包 JustFloat 数据
  */
static void Signal_VofaPacket(const uint8_t *buf, uint8_t len)
{
    uint32_t t0;

    if (bDeviceState != CONFIGURED) return;

    t0 = g_tick_ms;
    while (packet_sent == 0)
    {
        if ((uint32_t)(g_tick_ms - t0) > 50) return;
    }
    if (CDC_Send_DATA((uint8_t *)buf, len))
    {
        s_usb_sent++;
    }
}

/*============================= 界面绘制 ================================*/

/**
  * @brief  绘制 USB 模式波形界面
  */
static void Signal_DrawUsb(void)
{
    static const char *const names[WAVE_COUNT] = { "SIN", "TRI", "SQR", "SAW" };
    uint16_t x;
    uint8_t  i;

    SSD1306_Clear();

    SSD1306_ShowString(0, 0, 1, "SIGNAL GEN ", SSD1306_COLOR_ON);
    SSD1306_ShowString(88, 0, 1, "USB ", SSD1306_COLOR_ON);

    for (i = 0; i < WAVE_COUNT; i++)
    {
        uint16_t sx = (uint16_t)(8 + i * 28);
        SSD1306_ShowString(sx, 8, 1, names[i], SSD1306_COLOR_ON);
        if (i == s_wave_type)
        {
            SSD1306_DrawRect((uint16_t)(sx - 1), 7, (uint16_t)(sx + 23), 16, SSD1306_COLOR_ON);
        }
    }

    SSD1306_ShowString(0, 18, 1, "F:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(12, 18, s_freq, 5, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(48, 18, 1, "Hz", SSD1306_COLOR_ON);

    SSD1306_ShowString(64, 18, 1, "A:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(76, 18, s_amp, 3, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(100, 18, 1, "%", SSD1306_COLOR_ON);

    SSD1306_ShowString(0, 26, 1, "D:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(12, 26, s_duty, 3, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(36, 26, 1, "%", SSD1306_COLOR_ON);

    if (s_param == PARAM_FREQ) SSD1306_ShowString(44, 26, 1, "^", SSD1306_COLOR_ON);
    if (s_param == PARAM_AMP)  SSD1306_ShowString(96, 26, 1, "^", SSD1306_COLOR_ON);
    if (s_param == PARAM_DUTY) SSD1306_ShowString(32, 34, 1, "^", SSD1306_COLOR_ON);

    /* 波形预览 */
    for (x = 0; x < 64; x++)
    {
        uint16_t idx = (uint16_t)(x * 2);
        int32_t  v   = (int32_t)s_wave[idx] * 22 / 4096;
        uint16_t y   = (uint16_t)(58 - v);
        if (y > 63) y = 63;
        SSD1306_DrawPixel(x * 2,     y, SSD1306_COLOR_ON);
        SSD1306_DrawPixel(x * 2 + 1, y, SSD1306_COLOR_ON);
    }

    SSD1306_ShowString(0, 56, 1, "TX:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(24, 56, s_usb_sent % 100000, 5, 1, SSD1306_COLOR_ON);
    SSD1306_ShowString(64, 56, 1, "ST:", SSD1306_COLOR_ON);
    SSD1306_ShowNum(80, 56, (uint32_t)bDeviceState, 1, 1, SSD1306_COLOR_ON);

    SSD1306_Display();
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行信号发生器应用（阻塞，X 退出）
  */
void Signal_Run(void)
{
    uint8_t mode = 0;                   /* 0=未选，1=USB，2=GPIO */
    uint8_t sel = 0;                    /* 模式选择：0=USB，1=GPIO */
    uint8_t draw = 1;

    /* 模式选择界面 */
    while (mode == 0)
    {
        if (draw)
        {
            SSD1306_Clear();
            SSD1306_ShowString(0, 0, 1, "SIGNAL GEN", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 16, 1, "1 USB -> PC", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 32, 1, "2 GPIO -> PA4", SSD1306_COLOR_ON);
            SSD1306_ShowString(120, (uint16_t)(16 + sel * 16), 1, ">", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 56, 1, "LT/RT sel A:OK", SSD1306_COLOR_ON);
            SSD1306_Display();
            draw = 0;
        }

        if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON || Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
        {
            sel = (sel == 0) ? 1 : 0;
            draw = 1;
        }

        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            mode = (uint8_t)(sel + 1);
        }

        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            SSD1306_Clear();
            SSD1306_Display();
            return;
        }

        bsp_delay_ms(30);
    }

    Wave_Rebuild();

    if (mode == 1)
    {
        /* ---- 模式 1：USB CDC ---- */
        uint8_t draw_usb = 1;

        Signal_UsbInit();

        while (1)
        {
            uint16_t i;

            if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
            {
                s_param = (uint8_t)((s_param + 1) % PARAM_COUNT);
                draw_usb = 1;
            }

            if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON)
            {
                s_wave_type = (s_wave_type + WAVE_COUNT - 1) % WAVE_COUNT;
                Wave_Rebuild();
                draw_usb = 1;
            }
            else if (Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
            {
                s_wave_type = (s_wave_type + 1) % WAVE_COUNT;
                Wave_Rebuild();
                draw_usb = 1;
            }

            if (Gamepad_GetDPad() & GAMEPAD_DPAD_LEFT)
            {
                if (s_param == PARAM_FREQ)      Freq_Step(-1);
                else if (s_param == PARAM_AMP)  { if (s_amp > 5)  s_amp -= 5;  Wave_Rebuild(); }
                else                            { if (s_duty > 5) s_duty -= 5; Wave_Rebuild(); }
                draw_usb = 1;
            }
            else if (Gamepad_GetDPad() & GAMEPAD_DPAD_RIGHT)
            {
                if (s_param == PARAM_FREQ)      Freq_Step(+1);
                else if (s_param == PARAM_AMP)  { if (s_amp < 100) s_amp += 5; Wave_Rebuild(); }
                else                            { if (s_duty < 95) s_duty += 5; Wave_Rebuild(); }
                draw_usb = 1;
            }

            if (draw_usb)
            {
                Signal_DrawUsb();
                draw_usb = 0;
            }

            /* 8 帧 JustFloat 拼成 64 字节整包发送 */
            {
                uint8_t pkt[64];
                uint8_t k, n = 0;

                for (i = 0; i < WAVE_N; i += 8)
                {
                    union { float f; uint32_t u; } conv;
                    uint32_t u;

                    conv.f = (float)s_wave[i] * 3.3f / 4096.0f;
                    u = conv.u;

                    pkt[n++] = (uint8_t)(u & 0xFF);
                    pkt[n++] = (uint8_t)((u >> 8) & 0xFF);
                    pkt[n++] = (uint8_t)((u >> 16) & 0xFF);
                    pkt[n++] = (uint8_t)((u >> 24) & 0xFF);
                    for (k = 0; k < 4; k++) pkt[n++] = s_vofa_tail[k];
                }
                Signal_VofaPacket(pkt, n);
            }

            if ((g_tick_ms & 0xFF) < 20) draw_usb = 1;   /* 刷新 TX 计数 */

            if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
            {
                break;
            }

            bsp_delay_ms(20);
        }

        /* 退出：关 USB 中断 + 掉电 USB */
        NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
        NVIC_DisableIRQ(USBWakeUp_IRQn);
        _SetCNTR(CNTR_FRES | CNTR_PDWN);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, DISABLE);
    }
    else
    {
        /* ---- 模式 2：GPIO 方波 ---- */
        Signal_GpioRun();
    }

    SSD1306_Clear();
    SSD1306_Display();
}
