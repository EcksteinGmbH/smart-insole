/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

#include <stdio.h>
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "bat.h"
#include "app_led.h"
#include "app_wakeup.h"
#include "bsp.h"
#include "app_bat.h"
#include "app_lsm6d.h"


#define TAG "wakeup"



typedef struct sleep_bits
{
    uint8_t bat_low_bit : 1;
    uint8_t power_bit : 1;
    uint8_t imu_bit : 1;
} sleep_bits_t;

typedef union
{
    sleep_bits_t bits;
    uint8_t all_bits;
} sleep_en_t;

typedef enum
{
    SLEEP_BIT_BAT = 0,
    SLEEP_BIT_POWER = 1,
    SLEEP_BIT_IMU = 2
} sleep_bit_pos_t;

bool enter_sleep_flag = false;
sleep_en_t sleep_bits = {0};

void deep_sleep_update();
void deep_sleep_enter_loop();
bool is_power_online(void);
void sleep_bit_write(sleep_en_t *bit, sleep_bit_pos_t bit_pos, uint8_t value);

void deep_sleep_register_gpio_wakeup(gpio_num_t pin_power, gpio_num_t pin_imu)
{
    uint64_t mask = 0;
    if (pin_power != GPIO_NUM_NC)
    {
        mask |= BIT(pin_power);
    }
    if (pin_imu != GPIO_NUM_NC)
    {
        mask |= BIT(pin_imu);
    }

    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(mask, BAT_CHARGE_WAKEUP_LEVEL));

    ESP_LOGI(TAG, "Enabling GPIO wakeup on pins GPIO%d, mask:0x%08llx\n", BAT_CHARGE_PIN,mask);
}



//1. 充电状态不可能进入深度睡眠
//2. 未充电状态，电池电压低于0%，绿灯闪烁3次后灭，100ms亮，100ms灭，进入深度睡眠，且只能通过充电或者复位唤醒
//3. 未充电状态，按键长按进入深度睡眠，红灯快速闪烁3次后灭，100ms亮，100ms灭，且只能通过充电或者复位唤醒

static void deep_sleep_task(void *args)
{
    uint32_t last_time = 0;
    uint8_t trigger_count = 0;
    static uint8_t state = 0;

    while (1)
    {
        deep_sleep_update();
        deep_sleep_enter_loop();

        switch (state)
        {
        case 0:
            if(sleep_bits.bits.power_bit == 1 && sleep_bits.bits.imu_bit == 1 )
            {
                last_time = millis();
                state = 1;
                trigger_count = 0;
                ESP_LOGI(TAG, "[imu bit] [ready] ,[power:%d,imu:%d], rpl_mg:%4.2f, rpl_mdps:%4.2f",
                     sleep_bits.bits.power_bit,sleep_bits.bits.imu_bit,
                     rpl_get(&imu[0].rpl_mg), rpl_get(&imu[0].rpl_mdps));
            }
            if (sleep_bits.bits.power_bit == 1 && sleep_bits.bits.bat_low_bit == 1)
            {
                last_time = millis();
                state = 2;
                trigger_count = 0;
                ESP_LOGI(TAG, "[bat_low bit] [ready], percent:%4.2f, is_charging:%d",bat.percent, bat.is_charging);
            }

            break;
        case 1://等待imu位和电源位都为0, 且imu传感器数据稳定
            if (millis() - last_time >= 1000)
            {
                last_time = millis();
                if (sleep_bits.bits.power_bit == 0 || sleep_bits.bits.imu_bit == 0 )
                {
                    state = 0;
                    ESP_LOGI(TAG, "[imu bit] [exit] ,[power:%d,imu:%d] rpl_mg:%4.2f, rpl_mdps:%4.2f",
                         sleep_bits.bits.power_bit,sleep_bits.bits.imu_bit,
                        rpl_get(&imu[0].rpl_mg), rpl_get(&imu[0].rpl_mdps));
                }
                trigger_count++;
                if(trigger_count >= imu[0].acc_sleep_after_s)
                {
                    state = 2;
                    deep_sleep_register_gpio_wakeup(BAT_CHARGE_PIN, GPIO_NUM_NC);
                    // 不注册IMU0_INIT1_PIN, 因为IMU0_INIT1_PIN 会在IMU初始化时注册
                    //deep_sleep_register_gpio_wakeup(BAT_CHARGE_PIN, IMU0_INIT1_PIN);
                    deep_sleep_enter_start();
                }
            }
        case 2:
            if(sleep_bits.bits.power_bit == 0 || sleep_bits.bits.imu_bit == 0)
            {
                deep_sleep_enter_stop();
                state = 0;
            }
            break;
        case 3://等待电源位和电池低位都为0
            if (millis() - last_time >= 1000)
            {
                last_time = millis();

                if (sleep_bits.bits.power_bit == 0 || sleep_bits.bits.bat_low_bit == 0 )
                {
                    state = 0;
                    ESP_LOGI(TAG, "[bat_low bit] [exit] ,[power:%d,bat_low:%d] percent:%4.2f, is_charging:%d",
                         sleep_bits.bits.power_bit,sleep_bits.bits.bat_low_bit,
                        bat.percent, bat.is_charging);
                }
                trigger_count++;
                ESP_LOGI(TAG, "sleep by power&bat count: %d, bat: %0.3f, power: %d\n",
                     trigger_count, bat.percent, bat.is_charging);
                if (trigger_count >= 10)
                {
                    state = 4;
                    ESP_LOGI(TAG, "Entering deep sleep\n");
                    // enter deep sleep
                    deep_sleep_register_gpio_wakeup(BAT_CHARGE_PIN, GPIO_NUM_NC);
                    deep_sleep_enter_start();
                }
            }
            break;
        case 4:
            if(sleep_bits.bits.power_bit == 0)
            {
                deep_sleep_enter_stop();
                state = 0;
            }
            break;
        
        default:
            state = 0;
            break;
        }
        vTaskDelay(10);
    }
}

void deep_sleep_init(void)
{
    xTaskCreate(deep_sleep_task, "Deep Sleep Task", 2 * 1024, NULL, PRIORITY_DEEP_SLEEP_TASK, NULL);

 // 禁用不需要的唤醒源
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    // 只启用GPIO唤醒源
    deep_sleep_register_gpio_wakeup(BAT_CHARGE_PIN, GPIO_NUM_NC);
}

static uint8_t deep_sleep_state = 0;

void deep_sleep_enter_start()
{
    enter_sleep_flag = true;
}
void deep_sleep_enter_stop()
{
    enter_sleep_flag = false;
    deep_sleep_state = 0;
}
void deep_sleep_enter_loop()
{
    static uint8_t state2_cnt = 0;
    static uint32_t state2_last = 0;
    
    switch (deep_sleep_state)
    {
    case 0:
        if (enter_sleep_flag)
        {
            deep_sleep_state = 1;
            led_off(&led_r);
            // state2_last = xTaskGetTickCount();
            state2_last = millis();
            state2_cnt = 0;
        }
        break;
    case 1://再次检查电源位和imu位是否为0
        if(millis() - state2_last > 1000)
        {
            state2_last = millis();
            state2_cnt++;
            ESP_LOGI(TAG, "sleep by power & imu count: %d, power: %d, imu: %d, bat_low: %d",
                     state2_cnt, sleep_bits.bits.power_bit, 
                     sleep_bits.bits.imu_bit, 
                     sleep_bits.bits.bat_low_bit);
            if (state2_cnt >= 3)
            {
                deep_sleep_state = 2;
                state2_cnt = 0;
            }
        }
        break;
    case 2:
        if (millis() - state2_last > 100)
        {
            state2_last = millis();
            led_toggle(&led_r);
            state2_cnt++;
            if (state2_cnt >= 6)
            {
                ESP_LOGI(TAG, "Deep sleep now... ... ... ...");
                led_off(&led_g);
                led_off(&led_r);
                deep_sleep_state = 0;
                
                // 进入深度睡眠
                esp_deep_sleep_start();
                state2_cnt = 0;
            }
        }
        break;
    default:
        deep_sleep_state = 0;
        break;
    }
}



void sleep_bit_write(sleep_en_t *bit, sleep_bit_pos_t bit_pos, uint8_t value)
{
    if (bit_pos == SLEEP_BIT_BAT)
    {
        bit->bits.bat_low_bit = value;
    }
    else if (bit_pos == SLEEP_BIT_POWER)
    {
        bit->bits.power_bit = value;
    }
    else if (bit_pos == SLEEP_BIT_IMU)
    {
        bit->bits.imu_bit = value;
    }
}
void deep_sleep_update()
{

    if (gpio_get_level(BAT_CHARGE_PIN) != BAT_CHARGE_WAKEUP_LEVEL)
    {
        sleep_bit_write(&sleep_bits, SLEEP_BIT_POWER, 1);
        // ESP_LOGI(TAG, "Power sleep standby, sleep_bits: 0x%02x",sleep_bits.all_bits);
    }
    else
    {
        sleep_bit_write(&sleep_bits, SLEEP_BIT_POWER, 0);
    }


    if (bat_is_lowpower(&bat))
    {
        sleep_bit_write(&sleep_bits, SLEEP_BIT_BAT, 1);
        // ESP_LOGI(TAG, "Battery voltage sleep standby, sleep_bits: 0x%02x",sleep_bits.all_bits);
    }
    else
    {
        sleep_bit_write(&sleep_bits, SLEEP_BIT_BAT, 0);
    }
    // ESP_LOGI(TAG, "Deep sleep bits: 0x%02x, voltage: %.2f",sleep_bits.all_bits,voltage);

    if (imu[0].is_inactive)
    {
    //   ESP_LOGI(TAG, "[imu bit ok] , mg:%4.2f, mdps:%4.2f",
    //            rpl_get(&imu[0].rpl_mg), rpl_get(&imu[0].rpl_mdps));
        sleep_bit_write(&sleep_bits, SLEEP_BIT_IMU, 1);
    }
    else
    {
        sleep_bit_write(&sleep_bits, SLEEP_BIT_IMU, 0);
    }

}