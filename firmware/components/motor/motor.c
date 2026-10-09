#include <stdio.h>
#include "motor.h"
#include "esp_log.h"

// ================== 宏定义 ==================
#define PWM_TIMER                   LEDC_TIMER_0
#define PWM_MODE                    LEDC_LOW_SPEED_MODE
#define PWM_DUTY_RES                LEDC_TIMER_13_BIT  // 固定为13位
#define PWM_FREQUENCY_HZ            5000               // 固定频率为5000Hz
#define MAX_SPEED_VALUE             1000               // 最大速度值

static const char *TAG = "MOTOR";
static bool pwm_global_init_flag = false;

// 初始化全局PWM定时器
static bool pwm_global_init(void)
{
    if (pwm_global_init_flag) {
        return true;
    }

    ledc_timer_config_t ledc_timer = {
        .speed_mode = PWM_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = PWM_DUTY_RES,
        .freq_hz = PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer");
        return false;
    }
    
    pwm_global_init_flag = true;
    ESP_LOGI(TAG, "PWM timer initialized");
    return true;
}

// 初始化单个电机
bool motor_init(motor_t *motor, uint8_t id, uint8_t pwm_gpio, uint8_t channel)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return false;
    }
    
    // 初始化全局PWM定时器
    if (!pwm_global_init()) {
        return false;
    }
    
    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {
        .gpio_num = pwm_gpio,
        .speed_mode = PWM_MODE,
        .channel = channel,
        .timer_sel = PWM_TIMER,
        .duty = 0,  // 初始占空比为0
        .hpoint = 0,
    };
    
    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel %u", channel);
        return false;
    }
    
    // 初始化电机结构体（移除了自动停止相关参数）
    motor->id = id;
    motor->pwm_gpio = pwm_gpio;
    motor->channel = channel;
    motor->max_duty = (1 << 13) - 1;  // 13位分辨率最大占空比
    motor->current_speed = 0;
    motor->state = MOTOR_STATE_STOPPED;
    
    ESP_LOGI(TAG, "Motor %u initialized on GPIO %u (PWM)", id, pwm_gpio);
    return true;
}

// 启动电机（带速度参数）
bool motor_start(motor_t *motor, uint16_t speed)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return false;
    }
    
    // 限制速度范围
    if (speed > MAX_SPEED_VALUE) speed = MAX_SPEED_VALUE;
    
    motor->current_speed = speed;
    motor->state = MOTOR_STATE_RUNNING;
    
    // 设置PWM占空比
    uint32_t duty = (speed * motor->max_duty) / MAX_SPEED_VALUE;
    ledc_set_duty(PWM_MODE, motor->channel, duty);
    ledc_update_duty(PWM_MODE, motor->channel);
    
    // ESP_LOGI(TAG, "Motor %d started with speed %d (duty: %lu)", 
    //          motor->id, speed, duty);
    return true;
}

// 停止电机
bool motor_stop(motor_t *motor)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return false;
    }
    
    // 设置占空比为0
    ledc_set_duty(PWM_MODE, motor->channel, 0);
    ledc_update_duty(PWM_MODE, motor->channel);
    
    motor->current_speed = 0;
    motor->state = MOTOR_STATE_STOPPED;
    
    // ESP_LOGI(TAG, "Motor %d stopped", motor->id);
    return true;
}

// 设置电机速度
bool motor_set_speed(motor_t *motor, uint16_t speed)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return false;
    }
    
    // 如果电机未运行，先启动
    if (motor->state == MOTOR_STATE_STOPPED) {
        return motor_start(motor, speed);
    }
    
    // 限制速度范围
    if (speed > MAX_SPEED_VALUE) speed = MAX_SPEED_VALUE;
    
    // 如果速度为0，停止电机
    if (speed == 0) {
        return motor_stop(motor);
    }
    
    motor->current_speed = speed;
    
    // 设置PWM占空比
    uint32_t duty = (speed * motor->max_duty) / MAX_SPEED_VALUE;
    ledc_set_duty(PWM_MODE, motor->channel, duty);
    ledc_update_duty(PWM_MODE, motor->channel);
    
    // ESP_LOGI(TAG, "Motor %d speed set to %d (duty: %lu)", 
    //          motor->id, speed, duty);
    return true;
}

// 获取电机状态
motor_state_t motor_get_state(const motor_t *motor)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return MOTOR_STATE_STOPPED;
    }
    return motor->state;
}

// 获取电机当前速度
uint16_t motor_get_speed(const motor_t *motor)
{
    if (motor == NULL) {
        ESP_LOGE(TAG, "Motor pointer is NULL");
        return 0;
    }
    return motor->current_speed;
}
