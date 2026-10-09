#ifndef __GPIO_WAKEUP_H__
#define __GPIO_WAKEUP_H__



#include "sdkconfig.h"



void deep_sleep_init(void);
void deep_sleep_register_gpio_wakeup(gpio_num_t pin_power, gpio_num_t pin_imu);
void deep_sleep_enter_start();
void deep_sleep_enter_stop();

#endif   // __WAKEUP_H__
