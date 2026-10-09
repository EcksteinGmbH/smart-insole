#ifndef __APP_LED_H__
#define __APP_LED_H__

#include "driver/gpio.h"
#include "led.h"

extern led_t led_r;
extern led_t led_g;



void app_led_init(void);
void app_led_charging_loop();

#endif