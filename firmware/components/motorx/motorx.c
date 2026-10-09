#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "motorx.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MOTORX";

// 控制任务函数，用于实现频率和占空比控制
static void motorx_control_task(void *pvParameters)
{
    motorx_t *motorx = (motorx_t *)pvParameters;
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in control task");
        vTaskDelete(NULL);
        return;
    }

    // 获取参数（使用锁保护）
    uint16_t period_ms, on_time_ms, off_time_ms;
    uint16_t times;
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        motorx->count = 0;
        period_ms = (uint16_t)(1000.0f / motorx->params.frequency);
        on_time_ms = (uint16_t)(period_ms * motorx->params.duty);
        off_time_ms = period_ms - on_time_ms;
        times = motorx->params.times;
        xSemaphoreGive(motorx->mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in control task");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "MotorX %u control task started: freq=%.1fHz, duty=%.1f%%, period=%u ms, on=%u ms, off=%u ms, times=%u, intensity=%u",
             motorx->id, motorx->params.frequency, motorx->params.duty * 100, period_ms, on_time_ms, off_time_ms, motorx->params.times, motorx->params.intensity);

    while (1) {
        // 检查运行状态和计数（使用锁保护）
        bool running = false;
        uint16_t current_count = 0;
        uint16_t intensity = 0;
        
        if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            running = motorx->running;
            current_count = motorx->count;
            intensity = motorx->params.intensity;
            
            // 检查是否达到执行次数
            if (times > 0 && current_count >= times) {
                motorx->running = false;
                running = false;
            }
            xSemaphoreGive(motorx->mutex);
        } else {
            ESP_LOGE(TAG, "Failed to take mutex in control loop");
            break;
        }
        
        if (!running) {
            break;
        }

        // 电机开启阶段
        if (motorx->motor != NULL) {
            motor_start(motorx->motor, intensity);
        }

        // 等待开启时间
        vTaskDelay(on_time_ms / portTICK_PERIOD_MS);

        // 电机关闭阶段
        if (motorx->motor != NULL) {
            motor_stop(motorx->motor);
        }

        // 更新执行次数计数（使用锁保护）
        if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            motorx->count++;
            xSemaphoreGive(motorx->mutex);
        }

        // 等待关闭时间
        vTaskDelay(off_time_ms / portTICK_PERIOD_MS);
        
        // 重新获取频率和占空比参数，以支持运行时修改
        if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            period_ms = (uint16_t)(1000.0f / motorx->params.frequency);
            on_time_ms = (uint16_t)(period_ms * motorx->params.duty);
            off_time_ms = period_ms - on_time_ms;
            times = motorx->params.times;
            xSemaphoreGive(motorx->mutex);
        }
        
        ESP_LOGI(TAG, "MotorX %u cycle completed, count=%u", motorx->id, current_count + 1);
    }

    // 更新任务句柄（使用锁保护）
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint16_t final_count = motorx->count;
        ESP_LOGI(TAG, "MotorX %u control task stopped, executed %u times", motorx->id, final_count);
        motorx->handler = NULL;
        xSemaphoreGive(motorx->mutex);
    }
    
    vTaskDelete(NULL);
}

// 初始化电机X
bool motorx_init(motorx_t *motorx, uint8_t id, motor_t *motor, int8_t priority)
{
    if (motorx == NULL || motor == NULL) {
        ESP_LOGE(TAG, "Invalid pointers in motorx_init");
        return false;
    }

    memset(motorx, 0, sizeof(motorx_t));
    motorx->id = id;
    motorx->motor = motor;
    motorx->params.frequency = 1.0f;  // 默认1Hz
    motorx->params.duty = 0.5f; // 默认50%占空比
    motorx->params.times = 0;     // 默认一直运行
    motorx->params.intensity = 0;     // 默认强度0
    motorx->running = false;
    motorx->handler = NULL;
    motorx->count = 0;
    motorx->priority = priority;
    
    // 创建互斥锁
    motorx->mutex = xSemaphoreCreateMutex();
    if (motorx->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex in motorx_init");
        return false;
    }

    ESP_LOGI(TAG, "MotorX %u initialized with motor ID %u", motorx->id, motor->id);
    return true;
}

// 启动电机X
bool motorx_start(motorx_t *motorx, motorx_params_t params)
{
    if (!motorx || !motorx->motor) {
        ESP_LOGE(TAG, "Invalid motorx pointer or motor not initialized");
        return false;
    }

    // 验证并限制参数范围
    if (params.frequency < MOTORX_FREQ_MIN || params.frequency > MOTORX_FREQ_MAX || 
        params.duty < MOTORX_DUTY_MIN || params.duty > MOTORX_DUTY_MAX || 
        params.intensity < MOTORX_INTENSITY_MIN || params.intensity > MOTORX_INTENSITY_MAX) {
        
        // 记录超出范围的参数
        ESP_LOGE(TAG, "Parameters out of range: freq=%.1fHz (range: %.1f-%.1f), duty=%.2f (range: %.2f-%.1f), intensity=%u (range: %u-%u)", 
                 params.frequency, MOTORX_FREQ_MIN, MOTORX_FREQ_MAX,
                 params.duty, MOTORX_DUTY_MIN, MOTORX_DUTY_MAX,
                 params.intensity, MOTORX_INTENSITY_MIN, MOTORX_INTENSITY_MAX);
                   
        // 自动调整参数到有效范围
        if (params.frequency < MOTORX_FREQ_MIN) params.frequency = MOTORX_FREQ_MIN;
        if (params.frequency > MOTORX_FREQ_MAX) params.frequency = MOTORX_FREQ_MAX;
        if (params.duty < MOTORX_DUTY_MIN) params.duty = MOTORX_DUTY_MIN;
        if (params.duty > MOTORX_DUTY_MAX) params.duty = MOTORX_DUTY_MAX;
        if (params.intensity < MOTORX_INTENSITY_MIN) params.intensity = MOTORX_INTENSITY_MIN;
        if (params.intensity > MOTORX_INTENSITY_MAX) params.intensity = MOTORX_INTENSITY_MAX;
        
        ESP_LOGW(TAG, "Parameters automatically adjusted to range: freq=%.1fHz, duty=%.2f, intensity=%u", 
                 params.frequency, params.duty, params.intensity);
    }

    // 停止当前运行（如果正在运行）
    motorx_stop(motorx);

    // 使用互斥锁保护共享资源的访问
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 更新参数
        motorx->params = params;
        motorx->count = 0;
        motorx->running = true;
        
        // 启动控制任务
        if (xTaskCreate(motorx_control_task, "motorx_task", 
                                   1024*3, motorx, 
                                   motorx->priority, &motorx->handler) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create motorx control task");
            motorx->running = false;
            xSemaphoreGive(motorx->mutex);
            return false;
        }
        
        ESP_LOGI(TAG, "MotorX %u started with frequency=%.1fHz, duty=%.2f, times=%u, intensity=%u", 
                 motorx->id, params.frequency, params.duty, params.times, params.intensity);
        
        xSemaphoreGive(motorx->mutex);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_start");
        return false;
    }
}

// 停止电机X
bool motorx_stop(motorx_t *motorx)
{
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in motorx_stop");
        return false;
    }

    // 使用互斥锁保护共享资源的访问
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 停止电机
        if (motorx->motor != NULL) {
            motor_stop(motorx->motor);
        }

        // 更新状态
        motorx->running = false;

        // 任务会自行删除
        if (motorx->handler != NULL) {
            vTaskDelete(motorx->handler);
            motorx->handler = NULL;
        }

        ESP_LOGI(TAG, "MotorX %u stopped", motorx->id);
        xSemaphoreGive(motorx->mutex);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_stop");
        return false;
    }
}

// 设置电机X强度
bool motorx_set_intensity(motorx_t *motorx, uint16_t intensity)
{
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in motorx_set_intensity");
        return false;
    }

    // 限制强度范围
    if (intensity < MOTORX_INTENSITY_MIN) {
        intensity = MOTORX_INTENSITY_MIN;
        ESP_LOGW(TAG, "Intensity adjusted to minimum: %u", intensity);
    }
    if (intensity > MOTORX_INTENSITY_MAX) {
        intensity = MOTORX_INTENSITY_MAX;
        ESP_LOGW(TAG, "Intensity adjusted to maximum: %u", intensity);
    }

    // 使用互斥锁保护共享资源的访问
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        motorx->params.intensity = intensity;
        ESP_LOGI(TAG, "MotorX %u intensity set to %u", motorx->id, intensity);
        xSemaphoreGive(motorx->mutex);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_set_intensity");
        return false;
    }
}

// 设置电机X参数
bool motorx_set_params(motorx_t *motorx, motorx_params_t params)
{
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in motorx_set_params");
        return false;
    }

    // 验证并限制参数范围
    bool adjusted = false;
    
    if (params.frequency < MOTORX_FREQ_MIN) {
        params.frequency = MOTORX_FREQ_MIN;
        adjusted = true;
    }
    if (params.frequency > MOTORX_FREQ_MAX) {
        params.frequency = MOTORX_FREQ_MAX;
        adjusted = true;
    }
    if (params.duty < MOTORX_DUTY_MIN) {
        params.duty = MOTORX_DUTY_MIN;
        adjusted = true;
    }
    if (params.duty > MOTORX_DUTY_MAX) {
        params.duty = MOTORX_DUTY_MAX;
        adjusted = true;
    }
    if (params.intensity < MOTORX_INTENSITY_MIN) {
        params.intensity = MOTORX_INTENSITY_MIN;
        adjusted = true;
    }
    if (params.intensity > MOTORX_INTENSITY_MAX) {
        params.intensity = MOTORX_INTENSITY_MAX;
        adjusted = true;
    }
    
    if (adjusted) {
        ESP_LOGW(TAG, "MotorX %u parameters adjusted to range: freq=%.1fHz, duty=%.2f, intensity=%u",
                 motorx->id, params.frequency, params.duty, params.intensity);
    }

    // 使用互斥锁保护共享资源的访问
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // 保存当前运行状态
        bool was_running = motorx->running;

        // 如果正在运行，先停止（注意这里在锁内停止可能导致死锁，所以改为先记录状态）
        xSemaphoreGive(motorx->mutex);
        
        if (was_running) {
            motorx_stop(motorx);
        }
        
        // 再次获取锁更新参数
        if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 更新参数
            motorx->params = params;
            motorx->count = 0;
            
            ESP_LOGI(TAG, "MotorX %u parameters updated: freq=%.1fHz, duty=%.1f%%, times=%u, intensity=%u",
                     motorx->id, params.frequency, params.duty * 100, params.times, params.intensity);
            
            xSemaphoreGive(motorx->mutex);
        } else {
            ESP_LOGE(TAG, "Failed to take mutex after stopping motor in motorx_set_params");
            return false;
        }
        
        // 如果之前正在运行，重新启动
        if (was_running) {
            return motorx_start(motorx, params);
        }
        
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_set_params");
        return false;
    }
}


// 获取电机X参数
motorx_params_t motorx_get_params(const motorx_t *motorx)
{
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in motorx_get_params");
        motorx_params_t default_params = {0.0f, 0.0f, 0, 0};
        return default_params;
    }
    
    // 使用互斥锁保护共享资源的访问
    motorx_params_t params;
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        params = motorx->params;
        xSemaphoreGive(motorx->mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_get_params");
        motorx_params_t default_params = {0.0f, 0.0f, 0, 0};
        return default_params;
    }
    
    return params;
}

// 获取电机X强度
uint16_t motorx_get_intensity(const motorx_t *motorx)
{
    if (motorx == NULL) {
        ESP_LOGE(TAG, "MotorX pointer is NULL in motorx_get_intensity");
        return 0;
    }
    
    // 使用互斥锁保护共享资源的访问
    uint16_t intensity;
    if (xSemaphoreTake(motorx->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        intensity = motorx->params.intensity;
        xSemaphoreGive(motorx->mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex in motorx_get_intensity");
        return 0;
    }
    
    return intensity;
}