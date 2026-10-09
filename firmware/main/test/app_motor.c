#include "app_motor.h"
#include "motor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWM_GPIO_0                  5
#define PWM_GPIO_1                  6
#define PWM_GPIO_2                  7

static motor_t motor[3] ;


static void app_motor_task(void *pvParameters)
{
   
    // 启动电机，速度为500（范围：0 ~ 1000）
    motor_start(&motor[0], 10);
    motor_start(&motor[1], 10);
    motor_start(&motor[2], 10);
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    for(int i = 0; i < 100; i++)
    {
        motor_set_speed(&motor[0], i*10);
        motor_set_speed(&motor[1], i*10);
        motor_set_speed(&motor[2], i*10);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    motor_stop(&motor[0]);
    motor_stop(&motor[1]);
    motor_stop(&motor[2]);
    vTaskDelay(1000 / portTICK_PERIOD_MS);


    while(1) {

        vTaskDelay(100 / portTICK_PERIOD_MS);  // 每100ms更新一次
    }
}
void app_motor_init(void)
{
    motor_init(&motor[0], 0, 5, LEDC_CHANNEL_0);
    motor_init(&motor[1], 1, 6, LEDC_CHANNEL_1);
    motor_init(&motor[2], 2, 7, LEDC_CHANNEL_2);
    xTaskCreate(app_motor_task, "app_motor_task", 2048, NULL, 10, NULL);
}

