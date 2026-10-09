#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "interrupt.h"
#include "esp_sleep.h"

#define GPIO_INPUT_IO    GPIO_NUM_4
#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "GPIO_INT";

static TaskHandle_t gpio_task_handle = NULL;

// ================== ISR ==================
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gpio_task_handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ================== GPIO 事件任务 ==================
static void gpio_event_task(void* arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "GPIO[%d] interrupt triggered", GPIO_INPUT_IO);
    }
}

// ================== 应用入口 ==================
void interrupt_init(void)
{
    ESP_LOGI(TAG, "External interrupt demo on IO4");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,   // 下降沿触发
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_INPUT_IO),
        .pull_up_en = GPIO_PULLDOWN_DISABLE ,
        .pull_down_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    // 创建事件任务
    xTaskCreate(gpio_event_task, "gpio_event_task", 2048, NULL, 10, &gpio_task_handle);

    // 安装 ISR 服务
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_IO, gpio_isr_handler, NULL);
    ESP_LOGI(TAG, "GPIO wakeup enabled on GPIO %d and %d", GPIO_INPUT_IO, GPIO_INPUT_IO);
    ESP_LOGI(TAG, "GPIO interrupt configured, waiting...");
}
