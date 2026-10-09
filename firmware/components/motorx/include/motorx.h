#ifndef __MOTORX_H__
#define __MOTORX_H__

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"  // 添加头文件
#include "motor.h"



// 参数限制宏定义
#define MOTORX_FREQ_MIN       0.1f    // 最小频率 (Hz)
#define MOTORX_FREQ_MAX       100.0f  // 最大频率 (Hz)
#define MOTORX_DUTY_MIN       0.01f   // 最小占空比
#define MOTORX_DUTY_MAX       1.0f    // 最大占空比
#define MOTORX_INTENSITY_MIN  200     // 最小强度
#define MOTORX_INTENSITY_MAX  1000    // 最大强度

// 电机X控制参数结构体
typedef struct {
    float frequency;          // 运行频率 (Hz)，可低至0.1Hz
    float duty;         // 运行占空比 (0.0-1.0)
    uint16_t times;       // 执行次数，0表示一直运行
    uint16_t intensity;       // 电机强度 (映射到motor.c的占空比)
} motorx_params_t;

// 电机X结构体定义
typedef struct {
    uint8_t id;               // 电机X ID
    motor_t *motor;           // 底层电机驱动指针
    motorx_params_t params;   // 运行参数
    TaskHandle_t handler; // 控制任务句柄
    bool running;          // 内部运行标志
    uint16_t count;   // 当前执行次数计数
    SemaphoreHandle_t mutex;  // 添加互斥锁成员
    int8_t priority;        // 任务优先级
} motorx_t;

// 电机X驱动函数声明
bool motorx_init(motorx_t *motorx, uint8_t id, motor_t *motor, int8_t priority);
bool motorx_start(motorx_t *motorx, motorx_params_t params);
bool motorx_stop(motorx_t *motorx);
bool motorx_set_intensity(motorx_t *motorx, uint16_t intensity);
bool motorx_set_params(motorx_t *motorx, motorx_params_t params);
motorx_params_t motorx_get_params(const motorx_t *motorx);
uint16_t motorx_get_intensity(const motorx_t *motorx);

#endif /* __MOTORX_H__ */