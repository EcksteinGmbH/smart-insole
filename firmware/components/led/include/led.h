#ifndef __LED_H__
#define __LED_H__

#include "driver/gpio.h"



/**
 * LED 对象结构体定义（led.h 中）
 */
typedef struct {
    gpio_num_t pin;       // GPIO 引脚号
    int active_level;     // 有效电平（1=高亮，0=低亮）
    int state;            // 当前状态（1=亮，0=灭）
} led_t;

// 初始化LED
void led_init(led_t *led, gpio_num_t pin, int active_level);

// 控制LED亮灭
void led_on(led_t *led);
void led_off(led_t *led);
void led_toggle(led_t *led);

#endif // __LED_H__
