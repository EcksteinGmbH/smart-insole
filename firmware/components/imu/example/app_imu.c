#include "app_imu.h"
#include "esp_log.h"
#include "imu.h"
#include "utils.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_sleep.h"


static const char *TAG = "APP_IMU";

imux_t imux[2] = {0};
#define WAKEUP_GPIO_0               4     // 第一个 LSM6DS3TR-C 的 INT2
#define WAKEUP_GPIO_1               10    // 第二个 LSM6DS3TR-C 的 INT2

// 全局标志，用于指示是否检测到IMU中断
static volatile bool imu_interrupt_detected = false;
static volatile uint8_t imu_interrupt_source = 0; // 0: none, 1: imu0, 2: imu1




// ================== 中断唤醒处理 ==================
// Forward declaration for ISR handler
static void IRAM_ATTR gpio_isr_handler(void *arg);

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    ESP_LOGI(TAG, "Wake-up interrupt from GPIO %" PRIu32, gpio_num);  // 
}

static void wakeup_gpio_init(void)
{
    const uint64_t wake_gpio_mask = (1ULL << WAKEUP_GPIO_0) | (1ULL << WAKEUP_GPIO_1);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // 下降沿触发唤醒
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = wake_gpio_mask,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(WAKEUP_GPIO_0, gpio_isr_handler, (void*)WAKEUP_GPIO_0);
    gpio_isr_handler_add(WAKEUP_GPIO_1, gpio_isr_handler, (void*)WAKEUP_GPIO_1);

    // 启用 GPIO 唤醒（用于深度睡眠）
    esp_sleep_enable_gpio_wakeup();
    ESP_LOGI(TAG, "GPIO wakeup enabled on GPIO %d and %d", WAKEUP_GPIO_0, WAKEUP_GPIO_1);
}




int app_imu_init(void)
{
    if (i2c_master_init() != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return -1;
    }

    // 初始化 I2C

    // 初始化两个传感器
    bool ok1 = imu_init(&imux[0], 0X6A);
    bool ok2 = imu_init(&imux[1], 0X6B);
    if (!ok1) {
        ESP_LOGE(TAG, "Failed to initialize IMU at address 0X6A");
    }
    else
    {
        // imu_config_gyro_threshold(&imux[0], 100, 100, 100, 1);
    }
    if (!ok2) {
        ESP_LOGE(TAG, "Failed to initialize IMU at address 0X6B");
    }
    else
    {
        // imu_config_gyro_threshold(&imux[1], 100, 100, 100, 1);
    }
    if (ok1 && ok2) {
        ESP_LOGI(TAG, "All IMU sensors initialized successfully");
    } else {
        ESP_LOGW(TAG, "Some IMU sensors failed to initialize");
    }



    wakeup_gpio_init();

    return 0;
}
void app_imu_loop()
{
    static uint32_t last = 0;
    static uint32_t last2 = 0;
    if( millis() - last > 1000 ) 
    {
        last = millis();

        for (int i = 0; i < 2; i++)
        {
            if (imux[i].initialized) 
            {
                imu_read_data(&imux[i]);
            }
        }
    }

    if( millis() - last2 > 100 ) 
    {
        last2 = millis();

        for (int i = 0; i < 2; i++)
        {

        }
    }


}