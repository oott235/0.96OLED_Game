/**
  ******************************************************************************
  * @file    servo.c
  * @brief   S3010 舵机 + 超声波雷达控制实现
  *
  *          接线：
  *            PA2   -> S3010 舵机信号线（TIM2_CH3 PWM）
  *            PA0   -> HC-SR04 Trig（触发）
  *            PA1   -> HC-SR04 Echo（回波）
  *
  *          定时器：TIM2（APB1 = 36MHz）
  *            PSC = 71  -> 计数频率 500kHz（2us/计数）
  *            ARR = 9999 -> 周期 20000us = 20ms（50Hz）
  *            脉宽计数：0.5ms=250、2.5ms=1250，线性对应 0°~180°
  *
  *          角度换算：cnt = 250 + angle * (1000/180)
  *
  *          两种模式（进入时选择）：
  *            1 MANUAL    手调：LT 逆时针 / RT 顺时针，按住加速，松开清零
  *            2 AUTO      自动旋转雷达：舵机 0°~180° 往返扫描，超声波测距，
  *                        屏幕绘制极坐标雷达图像（余辉轨迹 + 扫描线）
  *
  *          注意：
  *            - 两次超声波触发间隔强制 >= 60ms（HC-SR04 手册要求），
  *              否则远距离回波会丢失、只能扫到近距离目标
  *            - 雷达倒立安装时（RADAR_MOUNT_INVERT=1）显示自动左右镜像
  ******************************************************************************
  */

#include "servo.h"
#include "ssd1306.h"
#include "gamepad.h"
#include "bsp_delay.h"
#include <stddef.h>

/*============================== 私有定义 ===============================*/

#define SERVO_GPIO      GPIOA
#define SERVO_PIN       GPIO_Pin_2      /* TIM2_CH3 */

#define ULTRA_TRIG_GPIO GPIOA
#define ULTRA_TRIG_PIN  GPIO_Pin_0      /* Trig */
#define ULTRA_ECHO_GPIO GPIOA
#define ULTRA_ECHO_PIN  GPIO_Pin_1      /* Echo */

/* TIM2 参数：APB1 36MHz */
#define SERVO_PSC       71              /* 36MHz/72 = 500kHz（2us/计数） */
#define SERVO_ARR       9999            /* 10000 计数 = 20ms = 50Hz */

/* 脉宽（计数）：0.5ms=250，2.5ms=1250 对应 0°~180° */
#define SERVO_CNT_MIN   250
#define SERVO_CNT_MAX   1250
#define SERVO_CNT_SPAN  (SERVO_CNT_MAX - SERVO_CNT_MIN)   /* 1000 */

/* 加速转动参数（每 15ms 一个控制节拍）：
   - 按住 LT/RT：速度逐节拍累加（s_speed += ACCEL），直到 MAX，越转越快
   - 松开扳机  ：速度立刻清零（回到 BASE） */
#define SERVO_SPEED_BASE    1    /* 起始速度：°/节拍 */
#define SERVO_SPEED_ACCEL   0.75    /* 每节拍加速增量：°/节拍² */
#define SERVO_SPEED_MAX     6    /* 封顶速度：°/节拍 */

/* 自动雷达扫描参数 */
#define RADAR_STEP          1    /* 每拍角度步进（°） */
#define RADAR_TICK_MS       25   /* 每拍间隔（ms） */
#define RADAR_MAX_CM        200  /* 量程：满半径对应距离（cm） */
#define RADAR_CX            64   /* 雷达圆心 X（像素） */
#define RADAR_CY            52   /* 雷达圆心 Y（像素） */
#define RADAR_R             44   /* 雷达最大半径（像素） */
#define RADAR_BLIP_N        120  /* 余辉轨迹环形缓冲个数 */

/* 雷达安装方向：1 = 倒立安装（舵机 0° 实际指向屏幕右侧，显示左右镜像） */
#define RADAR_MOUNT_INVERT  1

/* HC-SR04 两次触发最小间隔（us）：模块手册要求 >= 60ms，
   触发过快会丢失远距离回波（只返回近距离读数），必须强制等待 */
#define ULTRA_MIN_INTERVAL_US   60000UL

/*============================== 私有变量 ===============================*/

static uint16_t s_angle;                /* 当前角度 0~180 */
static uint16_t s_speed;                /* 当前角速度：°/节拍（按住累积，松开清零） */

/* 雷达余辉轨迹（环形缓冲，写入 s_blip_w） */
typedef struct
{
    uint16_t angle;                     /* 该点的舵机角度 */
    uint16_t cm;                        /* 该点测得的距离（0xFFFF = 空槽） */
} radar_blip_t;
static radar_blip_t s_blips[RADAR_BLIP_N];
static uint16_t s_blip_w;               /* 环形缓冲写指针 */

/* sin 查找表（Q10：值 = 1024*sin(k°)，k=0..90，ROM 91 项） */
static const uint16_t s_sin_lut[91] =
{
    0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
  178,  195,  213,  230,  248,  265,  282,  299,  316,  333,
  350,  367,  384,  400,  416,  433,  449,  465,  481,  496,
  512,  527,  543,  558,  573,  587,  602,  616,  630,  644,
  658,  672,  685,  698,  711,  724,  737,  749,  761,  773,
  784,  796,  807,  818,  828,  839,  849,  859,  868,  878,
  887,  896,  904,  912,  920,  928,  935,  943,  949,  956,
  962,  968,  974,  979,  984,  989,  994,  998, 1002, 1005,
 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024,
 1024
};

/*============================= 舵机底层 ================================*/

/**
  * @brief  初始化 TIM2_CH3 PWM（50Hz），输出到 PA2
  */
static void Servo_TimInit(void)
{
    GPIO_InitTypeDef  gpio;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* PA2 = TIM2_CH3（复用推挽） */
    gpio.GPIO_Pin   = SERVO_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SERVO_GPIO, &gpio);

    /* 时基：500kHz，周期 20ms */
    tb.TIM_Prescaler     = SERVO_PSC;
    tb.TIM_CounterMode   = TIM_CounterMode_Up;
    tb.TIM_Period        = SERVO_ARR;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &tb);

    /* CH3 PWM 模式 1，高电平有效 */
    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 0;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(TIM2, &oc);
    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);

    TIM_Cmd(TIM2, ENABLE);
}

/**
  * @brief  设置舵机角度（0~180）
  */
static void Servo_SetAngle(uint16_t angle)
{
    uint32_t cnt;

    if (angle > 180) angle = 180;
    s_angle = angle;

    cnt = SERVO_CNT_MIN + (uint32_t)angle * SERVO_CNT_SPAN / 180UL;
    TIM_SetCompare3(TIM2, (uint16_t)cnt);
}

/*============================= 超声波 ==================================*/

/**
  * @brief  初始化 HC-SR04：Trig = PA0 推挽输出，Echo = PA1 下拉输入
  * @note   Echo 空闲为低，用下拉输入比浮空更稳定（避免噪声/悬空误读）
  */
static void Ultra_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = ULTRA_TRIG_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ULTRA_TRIG_GPIO, &gpio);
    GPIO_ResetBits(ULTRA_TRIG_GPIO, ULTRA_TRIG_PIN);   /* Trig 空闲低 */

    gpio.GPIO_Pin  = ULTRA_ECHO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPD;     /* 下拉输入：空闲读低，防误触发 */
    GPIO_Init(ULTRA_ECHO_GPIO, &gpio);
}

/**
  * @brief  测距一次（HC-SR04）
  * @retval 距离（cm，0~400）；0xFFFF = 无回波/超时
  * @note   声速往返约 58us/cm：高电平时间(us) / 58 = cm
  */
static uint16_t Ultra_MeasureCm(void)
{
    uint32_t us0, us1;
    static uint32_t s_last_trig_us = 0;      /* 上次触发时刻（us） */

    /* 强制两次触发间隔 >= 60ms（HC-SR04 手册要求）：
       否则模块来不及完成远距离回声周期，远回波会丢失，只返回近距离读数 */
    while ((bsp_delay_us_now() - s_last_trig_us) < ULTRA_MIN_INTERVAL_US)
    {
        /* 等待间隔满足 */
    }
    s_last_trig_us = bsp_delay_us_now();

    /* 触发：Trig 拉高 >=10us（取 20us 余量，兼容部分克隆模块） */
    GPIO_SetBits(ULTRA_TRIG_GPIO, ULTRA_TRIG_PIN);
    bsp_delay_us(20);
    GPIO_ResetBits(ULTRA_TRIG_GPIO, ULTRA_TRIG_PIN);

    /* 等待 Echo 变高（超时 50ms = 无回波） */
    us0 = bsp_delay_us_now();
    while (GPIO_ReadInputDataBit(ULTRA_ECHO_GPIO, ULTRA_ECHO_PIN) == 0)
    {
        if ((bsp_delay_us_now() - us0) > 50000UL) return 0xFFFF;
    }

    /* 测量 Echo 高电平时间 */
    us0 = bsp_delay_us_now();
    while (GPIO_ReadInputDataBit(ULTRA_ECHO_GPIO, ULTRA_ECHO_PIN) != 0)
    {
        if ((bsp_delay_us_now() - us0) > 50000UL) break;   /* 超时按最远 */
    }
    us1 = bsp_delay_us_now() - us0;

    if (us1 < 200UL) return 0;           /* <3.4cm 视为贴脸/无效 */
    us1 /= 58UL;                         /* us -> cm */
    if (us1 > 400UL) us1 = 400UL;
    return (uint16_t)us1;
}

/*============================= 雷达绘制 ================================*/

/**
  * @brief  sin（角度 0~180，Q10）
  */
static int16_t Radar_Sin(uint16_t deg)
{
    if (deg <= 90) return (int16_t)s_sin_lut[deg];
    return (int16_t)s_sin_lut[180 - deg];
}

/**
  * @brief  cos（角度 0~180，Q10）
  */
static int16_t Radar_Cos(uint16_t deg)
{
    if (deg <= 90) return (int16_t)s_sin_lut[90 - deg];
    return -(int16_t)s_sin_lut[deg - 90];
}

/**
  * @brief  角度+距离 -> 屏幕坐标
  * @param  deg: 舵机角度 0~180（0=左，90=正前，180=右）
  * @param  cm:  距离（cm）
  * @param  px, py: 输出像素坐标
  * @retval 1 在屏内；0 超出量程/屏幕
  * @note   倒立安装（RADAR_MOUNT_INVERT=1）时做左右镜像：屏幕上 0° 在右、180° 在左
  */
static uint8_t Radar_Polar(uint16_t deg, uint16_t cm, int16_t *px, int16_t *py)
{
    int32_t r, dx, dy;

    if (cm > RADAR_MAX_CM) return 0;     /* 超量程不画 */

#if RADAR_MOUNT_INVERT
    deg = (uint16_t)(180 - deg);         /* 倒立安装：左右镜像 */
#endif

    r  = (int32_t)cm * RADAR_R / RADAR_MAX_CM;
    dx = r * Radar_Cos(deg) / 1024;      /* cos：0°=1 向左，180°=-1 向右 */
    dy = r * Radar_Sin(deg) / 1024;      /* sin：90°=1 向上 */
    *px = (int16_t)(RADAR_CX - dx);
    *py = (int16_t)(RADAR_CY - dy);

    return (*px >= 0 && *px < 128 && *py >= 0 && *py < 64);
}

/**
  * @brief  写入一个回波点到余辉缓冲
  */
static void Radar_Store(uint16_t deg, uint16_t cm)
{
    s_blips[s_blip_w].angle = deg;
    s_blips[s_blip_w].cm    = cm;
    s_blip_w = (s_blip_w + 1) % RADAR_BLIP_N;
}

/**
  * @brief  绘制整幅雷达图像（清屏后：量程圈 + 刻度线 + 余辉 + 扫描线 + 文本）
  * @param  deg: 当前扫描角度
  * @param  cm:  本次测得距离
  */
static void Radar_Draw(uint16_t deg, uint16_t cm)
{
    uint16_t i, a;
    int16_t  px, py;
    uint16_t n;

    SSD1306_Clear();

    /* 顶部文本：角度 + 距离（倒立安装时角度随镜像显示） */
    SSD1306_ShowString(0, 0, 1, "A:", SSD1306_COLOR_ON);
#if RADAR_MOUNT_INVERT
    SSD1306_ShowNum(12, 0, (uint16_t)(180 - deg), 3, 1, SSD1306_COLOR_ON);
#else
    SSD1306_ShowNum(12, 0, deg, 3, 1, SSD1306_COLOR_ON);
#endif
    SSD1306_ShowString(36, 0, 1, "D:", SSD1306_COLOR_ON);
    if (cm <= 400) SSD1306_ShowNum(48, 0, cm, 3, 1, SSD1306_COLOR_ON);
    else           SSD1306_ShowString(48, 0, 1, "---", SSD1306_COLOR_ON);
    SSD1306_ShowString(72, 0, 1, "cm", SSD1306_COLOR_ON);

    /* 量程圈（1/3、2/3、满） */
    for (i = 0; i < 3; i++)
    {
        for (a = 0; a <= 180; a += 2)
        {
            if (Radar_Polar(a, (uint16_t)(RADAR_MAX_CM * (i + 1) / 3), &px, &py))
            {
                SSD1306_DrawPixel((uint16_t)px, (uint16_t)py, SSD1306_COLOR_ON);
            }
        }
    }

    /* 刻度线：每 30° 一条（半径 0.85 满量程） */
    for (a = 0; a <= 180; a += 30)
    {
        int16_t x1 = RADAR_CX, y1 = RADAR_CY, x2, y2;
        if (Radar_Polar(a, RADAR_MAX_CM * 85 / 100, &x2, &y2))
        {
            SSD1306_DrawLine((uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2,
                             SSD1306_COLOR_ON);
        }
    }

    /* 余辉轨迹：环形缓冲全部有效点（加粗：3x3 实心块，越界裁剪） */
    for (n = 0; n < RADAR_BLIP_N; n++)
    {
        if (s_blips[n].cm != 0xFFFF)
        {
            if (Radar_Polar(s_blips[n].angle, s_blips[n].cm, &px, &py))
            {
                int16_t bx0 = (int16_t)(px - 1), by0 = (int16_t)(py - 1);
                int16_t bx1 = (int16_t)(px + 1), by1 = (int16_t)(py + 1);
                if (bx0 < 0) bx0 = 0;
                if (by0 < 0) by0 = 0;
                if (bx1 > 127) bx1 = 127;
                if (by1 > 63)  by1 = 63;
                SSD1306_FillRect((uint16_t)bx0, (uint16_t)by0,
                                 (uint16_t)bx1, (uint16_t)by1,
                                 SSD1306_COLOR_ON);
            }
        }
    }

    /* 扫描线：当前角度，满半径 */
    if (Radar_Polar(deg, RADAR_MAX_CM, &px, &py))
    {
        SSD1306_DrawLine(RADAR_CX, RADAR_CY, (uint16_t)px, (uint16_t)py,
                         SSD1306_COLOR_ON);
    }

    /* 圆心 */
    SSD1306_DrawPixel(RADAR_CX, RADAR_CY, SSD1306_COLOR_ON);

    SSD1306_Display();
}

/*============================= 模式 1：手调 ==============================*/

/**
  * @brief  绘制手调角度界面
  */
static void Servo_Draw(void)
{
    SSD1306_Clear();

    SSD1306_ShowString(0, 0, 1, "S3010 SERVO", SSD1306_COLOR_ON);
    SSD1306_ShowString(0, 8, 1, "LT:CCW RT:CW", SSD1306_COLOR_ON);

    /* 大号角度数字（size 4 = 8x16 两倍放大 = 16x32） */
    SSD1306_ShowNum(16, 20, s_angle, 3, 4, SSD1306_COLOR_ON);
    SSD1306_ShowString(96, 30, 1, "deg", SSD1306_COLOR_ON);

    /* 底部进度条（按角度比例） */
    SSD1306_DrawRect(8, 52, 120, 59, SSD1306_COLOR_ON);
    {
        uint16_t w = (uint16_t)(112UL * s_angle / 180UL);
        if (w > 0) SSD1306_FillRect(9, 53, (uint16_t)(9 + w - 1), 58, SSD1306_COLOR_ON);
    }

    SSD1306_Display();
}

/**
  * @brief  模式 1：手动控制（LT/RT，按住加速，松开清零，X 退出）
  */
static void Servo_Manual(void)
{
    Servo_SetAngle(90);                 /* 初始 90°（中位） */
    s_speed = SERVO_SPEED_BASE;         /* 初始速度 */
    Servo_Draw();

    while (1)
    {
        uint8_t moving = 0;

        /* LT：逆时针；RT：顺时针 */
        if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON)
        {
            moving = 1;
            if (s_speed < SERVO_SPEED_MAX) s_speed += SERVO_SPEED_ACCEL;  /* 加速 */
            if (s_angle > 0)
            {
                if (s_angle > s_speed)
                    Servo_SetAngle((uint16_t)(s_angle - s_speed));
                else
                    Servo_SetAngle(0);                       /* 到 0° 停住 */
            }
            Servo_Draw();
        }
        else if (Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
        {
            moving = 1;
            if (s_speed < SERVO_SPEED_MAX) s_speed += SERVO_SPEED_ACCEL;  /* 加速 */
            if (s_angle < 180)
            {
                if ((uint16_t)(180 - s_angle) > s_speed)
                    Servo_SetAngle((uint16_t)(s_angle + s_speed));
                else
                    Servo_SetAngle(180);                     /* 到 180° 停住 */
            }
            Servo_Draw();
        }

        /* 松开扳机：清空累积速度，回到初始速度 */
        if (moving == 0)
        {
            s_speed = SERVO_SPEED_BASE;
        }

        /* X：退出返回主页 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        bsp_delay_ms(15);               /* 每 15ms 一步：速度逐级累积，越转越快 */
    }
}

/*============================= 模式 2：自动雷达 ==========================*/

/**
  * @brief  模式 2：自动旋转雷达（0°~180° 往返扫描 + 超声波测距 + 雷达图像）
  */
static void Servo_AutoRadar(void)
{
    uint16_t deg = 0;
    int8_t   dir = 1;                   /* +1 正向扫描，-1 反向 */
    uint16_t i;
    uint16_t cm;

    /* 清空余辉缓冲 */
    for (i = 0; i < RADAR_BLIP_N; i++)
    {
        s_blips[i].cm = 0xFFFF;
    }
    s_blip_w = 0;

    Servo_SetAngle(0);

    while (1)
    {
        /* 走到目标角度并测距 */
        Servo_SetAngle(deg);
        bsp_delay_ms(RADAR_TICK_MS);    /* 等舵机到位 + 超声稳定 */
        cm = Ultra_MeasureCm();
        if (cm != 0xFFFF)
        {
            Radar_Store(deg, cm);
        }

        Radar_Draw(deg, cm);

        /* X：退出 */
        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        /* 角度步进 + 端点换向 */
        if (dir > 0)
        {
            if (deg >= 180) { deg = 180; dir = -1; }
            else            { deg += RADAR_STEP; if (deg > 180) deg = 180; }
        }
        else
        {
            if (deg <= 0) { deg = 0; dir = 1; }
            else          { deg -= RADAR_STEP; if (deg > 180) deg = 0; }
        }
    }
}

/*============================= 公共接口 =================================*/

/**
  * @brief  运行舵机/雷达应用（阻塞，X 退出）
  */
void Servo_Run(void)
{
    uint8_t sel = 0;                    /* 0 = 1 MANUAL，1 = 2 AUTO */
    uint8_t draw = 1;

    Servo_TimInit();
    Ultra_Init();

    /* 模式选择界面 */
    while (1)
    {
        if (draw)
        {
            SSD1306_Clear();
            SSD1306_ShowString(0, 0, 1, "RADAR MODE", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 16, 1, "1 MANUAL", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 32, 1, "2 AUTO", SSD1306_COLOR_ON);
            SSD1306_ShowString(120, (uint16_t)(16 + sel * 16), 1, ">", SSD1306_COLOR_ON);
            SSD1306_ShowString(0, 56, 1, "LT/RT sel A:OK", SSD1306_COLOR_ON);
            SSD1306_Display();
            draw = 0;
        }

        if (Gamepad_GetLT() > GAMEPAD_TRIGGER_ON)
        {
            sel = (sel == 0) ? 1 : 0;
            draw = 1;
        }
        else if (Gamepad_GetRT() > GAMEPAD_TRIGGER_ON)
        {
            sel = (sel == 0) ? 1 : 0;
            draw = 1;
        }

        if (Gamepad_ButtonPressed(GAMEPAD_BTN_A))
        {
            if (sel == 0) Servo_Manual();
            else          Servo_AutoRadar();
            draw = 1;
        }

        if (Gamepad_ButtonPressed(GAMEPAD_BTN_X))
        {
            break;
        }

        bsp_delay_ms(30);
    }

    SSD1306_Clear();
    SSD1306_Display();
}
