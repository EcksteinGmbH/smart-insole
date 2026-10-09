/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-10-05     Administrator       the first version
 */
#ifndef APPLICATIONS_COMPONENTS_FILTER_RC_H_
#define APPLICATIONS_COMPONENTS_FILTER_RC_H_

#include "stdio.h"

typedef struct {
    float a;
    float prev_output;
} FilterLowPassRC_t;


typedef struct
{
    float alpha;        // 平滑系数 (0 ~ 1)，越小越平滑
    float prev_output;  // 上一次输出
    int initialized;    // 标记是否已初始化
} FilterEWMA_t;


void FilterLowPassRC_init(FilterLowPassRC_t* filter, float cutoff_freq, float sample_rate) ;
float FilterLowPassRC_input(FilterLowPassRC_t* filter, float input_sample);// 更新滤波器（处理新的输入样本）


void FilterEWMA_init(FilterEWMA_t *filter, float alpha);
float FilterEWMA_input(FilterEWMA_t *filter, float input);


#endif /* APPLICATIONS_COMPONENTS_FILTER_RC_H_ */
