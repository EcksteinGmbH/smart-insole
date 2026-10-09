/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-10-05     Administrator       the first version
 */
#include "filter_rc.h"
// 初始化滤波器
void FilterLowPassRC_init(FilterLowPassRC_t* filter, float cutoff_freq, float sample_rate) {
    // 计算滤波系数 a
    // 对于一阶IIR低通滤波器，a = 1 / (1 + RC * 采样率)
    // 但由于我们没有直接的R和C值，我们需要从截止频率反推出等效的RC值
    // fc = 1 / (2 * PI * RC) => RC = 1 / (2 * PI * fc)
    // 然后 a = 1 / (1 + (1 / (2 * PI * fc)) * 采样率)
    // 简化后 a = 1 / (1 + 采样率 / (2 * PI * fc))
    float RC = 1.0f / (2.0f * 3.14159265358979323846f * cutoff_freq);
    filter->a = 1.0f / (1.0f + (sample_rate * RC));
    filter->prev_output = 0.0f; // 初始化上一次的输出值为0
}

// 更新滤波器（处理新的输入样本）
float FilterLowPassRC_input(FilterLowPassRC_t* filter, float input_sample) {
    // 一阶IIR低通滤波器的更新公式：
    // y[n] = a * x[n] + (1 - a) * y[n-1]
    float output_sample = filter->a * input_sample + (1.0f - filter->a) * filter->prev_output;
    filter->prev_output = output_sample; // 更新上一次的输出值
    return output_sample;
}



/**
 * 初始化滤波器
 * @param filter   滤波器对象
 * @param alpha    平滑系数 (0 ~ 1)
 */
void FilterEWMA_init(FilterEWMA_t *filter, float alpha)
{
    filter->alpha = alpha;
    filter->prev_output = 0.0f;
    filter->initialized = 0;
}

/**
 * 更新滤波器
 * @param filter   滤波器对象
 * @param input    新的输入样本
 * @return         平滑后的输出
 */
float FilterEWMA_input(FilterEWMA_t *filter, float input)
{
    float output;
    if (!filter->initialized)
    {
        // 第一次输入时直接赋值
        output = input;
        filter->initialized = 1;
    }
    else
    {
        output = filter->alpha * input + (1.0f - filter->alpha) * filter->prev_output;
    }
    filter->prev_output = output;
    return output;
}
//
///**
// * 示例：对一组数据应用 EWMA 滤波
// */
//int main(void)
//{
//    EWMAFilter filter;
//    ewma_init(&filter, 0.2f);  // 平滑系数 α=0.2
//
//    float data[] = {10, 12, 15, 20, 18, 16, 17, 19, 25};
//    int len = sizeof(data) / sizeof(data[0]);
//
//    printf("Input\tEWMA Output\n");
//    for (int i = 0; i < len; i++)
//    {
//        float y = ewma_update(&filter, data[i]);
//        printf("%5.2f\t%8.3f\n", data[i], y);
//    }
//
//    return 0;
//}
//```
