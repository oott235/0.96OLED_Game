/**
  ******************************************************************************
  * @file    signal.h
  * @brief   信号发生器应用（USB CDC 虚拟串口 / GPIO 方波输出）
  *
  *          接线：
  *            PA4   -> 方波输出（GPIO 推挽，0/3.3V，无需 DAC/TIM）
  *            micro USB 口 -> 直连电脑（USB CDC 虚拟串口，模式 1）
  *            GND   -> 与查看设备共地
  *
  *          支持波形（USB 模式）：正弦 SIN / 三角 TRI / 方波 SQR / 锯齿 SAW
  *
  *          操作（进入后先选输出模式）：
  *            1 USB   micro USB 口直连电脑，USB CDC 虚拟串口 + vofa+
  *                    （JustFloat 协议）实时显示波形
  *            2 GPIO  PA4 输出方波（GPIO 翻转，频率/占空比可调）
  *
  *          波形界面按键：
  *            LT/RT        切换波形类型（SIN/TRI/SQR/SAW）
  *            A            循环切换参数项（F 频率 / A 幅度 / D 占空比）
  *            左/右十字键   调节当前参数值
  *            X            返回模式选择，再按 X 返回主页
  *
  *          说明：GPIO 方波模式不依赖 DAC/DMA/TIM 等 APB1 外设
  *         （APB1 域异常的板子 DAC 不可用时仍可用方波输出）。
  ******************************************************************************
  */

#ifndef __SIGNAL_H
#define __SIGNAL_H

#include "stm32f10x.h"

/**
  * @brief  运行信号发生器应用（阻塞，X 退出）
  */
void Signal_Run(void);

#endif /* __SIGNAL_H */
