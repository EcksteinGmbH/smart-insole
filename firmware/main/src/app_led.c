#include "app_led.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "app_bat.h"
#include "bsp.h"



#define TAG "app_led"



led_t led_g;
led_t led_r;
bool led_task_runing = false;

// 开机时绿灯闪烁3次, 每次间隔200ms
static void app_led_start_task(void *param)
{
    int count = 0;
    led_off(&led_g);
    led_task_runing = true;
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        led_on(&led_g);
        vTaskDelayUntil( &xLastWakeTime, 100 / portTICK_PERIOD_MS );
        led_off(&led_g);
        vTaskDelayUntil( &xLastWakeTime, 100 / portTICK_PERIOD_MS );
        count++;
        ESP_LOGD(TAG, "led_g toggle %d times", count);
        if (count >= 3)
        {
            led_task_runing = false;
           break;
        }
    }
    vTaskDelete(NULL);
}

//0. s0:充电状态，电池电量低于99.5%，红灯闪烁，0.5秒亮，0.5秒灭
//   s0:充满到99.5%，绿灯常亮,进入s1
//   s0:断开充电器，绿灯灭，红灯灭,进入s2
//1. s1:满电状态，绿灯常亮，关闭红灯,
//   s1:如果充电，且电量低于98%，进入s0，施密特触发
//   s1:断开充电器，绿灯灭，红灯灭，进入s2
//2. s2:未充电状态，保持全灭
//   s2:如果充电，进入s0
//   s2:如果充电，且电量高于99.5%，进入s1
static void app_led_charging_task(void *param)
{
    uint8_t state = 0;

    while(led_task_runing)
    {
        vTaskDelay(10);
    }
    ESP_LOGI(TAG, "app_led_charging_task start...");

        TickType_t xLastWakeTime;
        xLastWakeTime = xTaskGetTickCount();
    while (1)
    {
        switch (state)
        {
        case 0:
            if(bat_is_charging(&bat))//充电状态
            {
                if(bat_is_full(&bat))//充电状态，电池电压高于4.1v，绿灯常亮，关闭红灯
                {
                    led_on(&led_g);
                    led_off(&led_r);
                    state = 1;
                    ESP_LOGI(TAG, "0->1: charging full, led_g on !!!, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
                else//充电状态，电池电压低于4.1v，虹灯闪烁，0.5秒亮，0.5秒灭
                {
                    led_toggle(&led_r);
                    // ESP_LOGI(TAG, "0 charging: %0.1f%%, %.3fV, %s",  bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
            }
            else//未充电状态，绿灯灭，红灯灭
            {
                state = 2;
                led_off(&led_g);
                led_off(&led_r);
                ESP_LOGI(TAG, "0->2: power offline, led off, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
            }
            break;
        case 1://充满状态，
            if(bat_is_charging(&bat))//充满状态，
            {
               if(bat_get_percent(&bat) < 98.0f)//充电状态，电池电压低于98%,绿灯灭，红灯闪烁，0.5秒亮，0.5秒灭
                {
                    led_off(&led_g);
                    led_toggle(&led_r);
                    state = 0;
                    ESP_LOGI(TAG, "1->0: power offline, led off, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
                else//充电状态，电池电压高于4.05v，绿灯常亮
                {
                   //do nothing
                    // ESP_LOGI(TAG, "1 full:  %0.1f%%, %.3fV, %s", bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
            }
            else//进入未充电状态，绿灯灭，红灯闪烁，0.5秒亮，0.5秒灭
            {
                led_off(&led_g);
                led_off(&led_r);
                state = 2;
                ESP_LOGI(TAG, "1->2: power offline, led off, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
            }
            break;
        case 2://未充电状态，绿灯灭，红灯灭
            if(bat_is_charging(&bat))//如果充电，返回充电状态
            {
                if(bat_is_full(&bat))//充电状态，电池电压高于4.1v，绿灯常亮,关闭红灯
                {
                    led_on(&led_g);
                    led_off(&led_r);
                    state = 1;
                    ESP_LOGI(TAG, "2->1:  power online -> full, led_g on !!!, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
                else//充电状态，电池电压低于4.1v，关闭绿灯
                {
                    led_off(&led_g);
                    state = 0;
                    ESP_LOGI(TAG, "2->0: power online -> charging ,led_r toggle !!!, %0.1f%%, %.3fV, charging:%s", 
                        bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                }
            }
            else//power offline
            {
                // ESP_LOGI(TAG, "2 off :  %0.1f%%, %.3fV, %s", bat_get_percent(&bat), bat_get_voltage(&bat), bat.is_charging?"yes":"no");
                //do nothing
            }
            break;
        default:
            break;
        }
        vTaskDelayUntil( &xLastWakeTime, 500 / portTICK_PERIOD_MS );
    }
    
    vTaskDelete(NULL);
}

void app_led_init()
{
    led_init(&led_g, LED_GREEN_PIN, 0);
    led_init(&led_r, LED_RED_PIN, 0);
    xTaskCreate(app_led_start_task, "app_led",  2048, NULL, PRIORITY_APP_LED_TASK, NULL);
    xTaskCreate(app_led_charging_task, "app_led_charging",  4096, NULL, PRIORITY_APP_LED_TASK, NULL);
}

