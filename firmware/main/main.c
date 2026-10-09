#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_pm.h"

#include "bsp.h"
#include "test.h"
#include "motor.h"
#include "nimble.h"
#include "protocol.h"
#include "app_lsm6d.h"
#include "app_wakeup.h"
#include "motorx.h"
#include "app_motorx.h"
#include "bat.h"
#include "app_led.h"
#include "app_button.h"
#include "app_bat.h"

static const char *TAG = "MAIN";

motor_t motor_1;

// 接收数据回调函数
static void on_ble_data_received(const uint8_t *data, uint16_t len)
{
    char response_buf[512]; // 分配响应缓冲区
    // ESP_LOGI(TAG, "Received JSON: [%d] [%s]",len, data);
    // ESP_LOG_BUFFER_HEXDUMP(TAG,data,len,0 );

    int resp_len = protocol_receive_json_data(data, len, response_buf, sizeof(response_buf));

    if (resp_len > 0)
    {

        ble_send_data((const uint8_t *)response_buf, resp_len);
        // 成功处理，使用response_buf中的响应数据
        // ESP_LOGI(TAG, "Response: %s", response_buf);
    }
    else
    {
        // 处理错误
        ESP_LOGW(TAG, "Error processing JSON: %d, wait for more data", resp_len);
    }
}
static void ble_imu_notify_task(void *param)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        if (ble_is_connected() && ble_is_timer_notify_enabled())
        {
            // const char *timer_message = "12345678901234567890123456789012345678901234567890";
            // const char *timer_message = "123456789012345678901234567890";
            uint8_t timer_message[256];
            memset(timer_message, 0, sizeof(timer_message));
            int len = protocol_make_imu_frame((uint8_t *)timer_message, 256);
            ble_send_timer_data(timer_message, len);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(30)); // 每秒发送一次
    }
}

void wake_cause_log()
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();

    switch (wake_cause)
    {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        ESP_LOGI(TAG, "Power on or reset");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        ESP_LOGI(TAG, "Wakeup caused by EXT0 (GPIO)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG, "Wakeup caused by timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        ESP_LOGI(TAG, "Wakeup caused by touchpad");
        break;
    case ESP_SLEEP_WAKEUP_UART:
        ESP_LOGI(TAG, "Wakeup caused by UART");
        break;
    default:
        ESP_LOGI(TAG, "Wakeup from other cause: %d", wake_cause);
        break;
    }
}
// ================== 主任务 ==================
void app_main(void)
{
    esp_err_t ret;
    app_motorx_init();
    // esp_wifi_stop();
    // esp_wifi_deinit();
    wake_cause_log();

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    #if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_t pm_config = {
            .max_freq_mhz = 160,
            .min_freq_mhz = 40,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
#endif // CONFIG_PM_ENABLE
    app_bat_init();
    app_btn_init();
    app_led_init();
   

    nimble_init();
    ble_set_receive_callback(on_ble_data_received);

    app_lsm6d_init();

    xTaskCreate(ble_imu_notify_task, "imu_notify", 4 * 1024, NULL, 5, NULL);
    deep_sleep_init();

    while (1)
    {
        // bat_loop();

        // motor_loop();
        // imu_loop(&imu[0]);
        // imu_loop(&imu[1]);

        //     // 休眠前打印
        //     ESP_LOGI(TAG, "Entering deep sleep in 5 seconds...");
        //     vTaskDelay(pdMS_TO_TICKS(5000));

        //     // 进入深度睡眠，等待 INT2 中断唤醒
        //     esp_deep_sleep_start();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}