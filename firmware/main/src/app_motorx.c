#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "motor.h"
#include "motorx.h"
#include "bsp.h"

static const char *TAG = "MOTORX_APP";



// 定义电机和电机x对象
motor_t motor[3];
motorx_t motorx[3];

ledc_channel_t ledc_channel[3] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};
uint8_t pwm_pin_table[3] ={MOTOR1_PIN,MOTOR2_PIN,MOTOR3_PIN};



// 示例任务，用于演示电机控制
static void motorx_example_task(void *pvParameters)
{

    // 3. 创建电机x参数配置
    motorx_params_t params = {
        .frequency = 10.5f,      // 1Hz频率
        .duty = 0.30f,    // 10%占空比
        .times = 20,        // 执行10个周期
        .intensity = 700        // 中等强度700
    };

    uint8_t id = 0;

    // 4. 启动电机x，强度已包含在params中
    ESP_LOGI(TAG, "Starting MotorX with 1Hz frequency, 10%% duty cycle, 10 cycles, intensity 700");
    if (!motorx_start(&motorx[id], params)) {
        ESP_LOGE(TAG, "Failed to start MotorX");
        vTaskDelete(NULL);
        return;
    }
    while (motorx[id].running) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Stopping MotorX");
    
  

    vTaskDelete(NULL);
}

// 应用程序入口函数
void app_motorx_init(void)
{
    // 1. 初始化底层电机

    for(int i=0;i<3;i++){
        // 1. 初始化底层电机
        ESP_LOGI(TAG, "Initializing motor[%d]", i);
        if (!motor_init(&motor[i], i, pwm_pin_table[i], ledc_channel[i])) {
            ESP_LOGE(TAG, "Failed to initialize motor");
            vTaskDelete(NULL);
            return;
        }
        // 2. 初始化电机x
        ESP_LOGI(TAG, "Initializing MotorX[%d]", i);
        if(!motorx_init(&motorx[i], i, &motor[i], PRIORITY_MOTORX_TASK)){
            ESP_LOGE(TAG, "Failed to initialize MotorX");
            vTaskDelete(NULL);
            return;
        }
    }
 
    // 创建示例任务
    // xTaskCreate(motorx_example_task, "motorx_example_task", 4096, NULL, 5, NULL);
}

