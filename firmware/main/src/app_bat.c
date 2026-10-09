#include "app_bat.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp.h"
#include "esp_timer.h"

#define TAG "app_bat"

// 采样间隔定义（单位：毫秒）
#define SAMPLE_INTERVAL_NORMAL    10000  // 正常状态：10秒
#define SAMPLE_INTERVAL_LOW       1000   // 低电量：1秒
#define SAMPLE_INTERVAL_CHARGING  2000   // 充电状态变化：2秒（临时提高频率）

// 电量阈值定义
#define BATTERY_LOW_THRESHOLD     20.0f  // 低电量阈值：20%

// 状态机定义
typedef enum {
    BAT_STATE_SAMPLE,             // 正常状态
    BAT_STATE_DELAY,       // 正常状态延时
} bat_state_t;

bat_t bat;

// 状态跟踪变量
static bool last_charging_state = false;
static uint32_t charging_state_change_time = 0;

uint32_t generate_state_delay(bat_t *bat)
{
    uint32_t delay_time = 1000;
    if(bat->is_charging)
    {
        delay_time = SAMPLE_INTERVAL_CHARGING;
    }
    else if(bat->percent < BATTERY_LOW_THRESHOLD)
    {
        delay_time = SAMPLE_INTERVAL_LOW;
    }
    else
    {
        delay_time = SAMPLE_INTERVAL_NORMAL;
    }
    return delay_time;
}

static void app_bat_task(void *param)
{
    bat_state_t state = BAT_STATE_DELAY ; // 初始状态设为正常状态
    uint32_t delay_time = SAMPLE_INTERVAL_NORMAL;
    uint32_t last_sample_time = 0;
    bat_update_voltage(&bat);
    delay_time = generate_state_delay(&bat);
    ESP_LOGI(TAG, "充电状态: %s, [%.3f V, %.1f%%]", bat.is_charging ? "开始充电" : "停止充电",bat.voltage,bat.percent);

    while (1)
    {
        uint32_t current_time = esp_timer_get_time() / 1000; // 获取当前时间（毫秒）
        bat_is_charging(&bat);
        // 检查充电状态是否变化
        if (bat.is_charging != last_charging_state) {
            last_charging_state = bat.is_charging;
            state = BAT_STATE_SAMPLE;
            ESP_LOGI(TAG, "充电状态变化: %s", bat.is_charging ? "开始充电" : "停止充电");
        }
        
        // 根据当前状态执行相应的逻辑
        switch (state) {
            case BAT_STATE_SAMPLE:
                // 更新电池状态
                last_sample_time = current_time;
                bat_update_voltage(&bat);
                delay_time = generate_state_delay(&bat);
                state = BAT_STATE_DELAY;
                ESP_LOGD(TAG, "正常状态延时: %lus, 电池电压: %.3f V, 电量: %.1f%%", delay_time/1000,bat.voltage,bat.percent);
                break;
            case BAT_STATE_DELAY:
                if((current_time - last_sample_time) >= delay_time)
                {
                    state = BAT_STATE_SAMPLE;
                }
                break;
            default:
                state = BAT_STATE_SAMPLE;
                break;
        }
        // 等待指定的采样间隔
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void app_bat_init(void)
{
    ESP_LOGI(TAG, "电池监测模块启动");

    // 初始化电池检测模块
    bat_init(&bat, BAT_CHARGE_PIN, BAT_ADC_CHANNEL, BAT_P0_VOLTAGE, BAT_P100_VOLTAGE);
    
    // 获取初始充电状态
    last_charging_state = bat.is_charging;

    xTaskCreate(app_bat_task, "app_bat", 4096, NULL, PRIORITY_BAT_TASK, NULL);
}