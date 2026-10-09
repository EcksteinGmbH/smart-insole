#include "motor.h"

// 定义电机对象
motor_t motor_1;

void app_main(void)
{
    // 初始化电机（ID=1, PWM引脚=5, LEDC通道=LEDC_CHANNEL_0）
    motor_init(&motor_1, 1, 5, LEDC_CHANNEL_0);
    
    // 启动电机，速度为500（范围：0 ~ 1000）
    motor_start(&motor_1, 500);
    
    // 运行5秒后降低速度
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    motor_set_speed(&motor_1, 300);
    
    // 再运行3秒后停止电机
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    motor_stop(&motor_1);
    
    // 另一种启动方式：带自动停止时间
    motor_start_with_duration(&motor_1, 400, 8000);  // 以速度400运行8秒后自动停止
    
    // 在主循环中定期调用motor_update处理自动停止
    while(1) {
        motor_update(&motor_1);
        vTaskDelay(100 / portTICK_PERIOD_MS);  // 每100ms更新一次
    }
}