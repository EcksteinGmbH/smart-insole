#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"

// 电机运行状态枚举
typedef enum {
    MOTOR_STATE_STOPPED = 0,
    MOTOR_STATE_RUNNING,
    MOTOR_STATE_BRAKING
} motor_state_t;

// 电机结构体定义（移除了自动停止相关参数）
typedef struct {
    uint8_t id;                 // 电机ID
    uint8_t pwm_gpio;           // PWM引脚
    uint8_t channel;            // LEDC通道
    uint32_t max_duty;          // 最大占空比
    uint16_t current_speed;     // 当前速度 (0 ~ 1000)
    motor_state_t state;        // 电机状态
} motor_t;

// 电机驱动函数声明（移除了motor_start_with_duration和motor_update）
bool motor_init(motor_t *motor, uint8_t id, uint8_t pwm_gpio, uint8_t channel);
bool motor_start(motor_t *motor, uint16_t speed);
bool motor_stop(motor_t *motor);
bool motor_set_speed(motor_t *motor, uint16_t speed);
motor_state_t motor_get_state(const motor_t *motor);
uint16_t motor_get_speed(const motor_t *motor);

#endif /* __MOTOR_H__ */