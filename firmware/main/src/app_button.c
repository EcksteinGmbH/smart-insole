#include "app_button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bsp.h"
#include "button_gpio.h"
#include "iot_button.h"
#include "button_types.h"
#include "app_wakeup.h"
#include "utils.h"
#include "app_bat.h"


#define BUTTON_ACTIVE_LEVEL     0


uint32_t last_press_time = 0;

static void button_event_cb(void *arg, void *data)
{
    // button_event_t event = (button_event_t)data;  // ⚠️ 注意：data 参数就是事件类型
    //  iot_button_print_event((button_handle_t)arg); // 保留原来的打印方便调试
     button_event_t event = iot_button_get_event((button_handle_t)arg);

    switch (event)
    {
    case BUTTON_SINGLE_CLICK:
        ESP_LOGI("button", "Single click detected");
        // TODO: 单击事件
        // 例如：切换模式、开始计数
        break;
    case BUTTON_DOUBLE_CLICK:
        ESP_LOGI("button", "Double click detected");
        // TODO: 双击事件
        break;
    case BUTTON_LONG_PRESS_START:
        // ESP_LOGI("button", "Long press start");
        if(!bat_is_charging(&bat))
        {
            ESP_LOGI("button", "Long press sleep trigger !!!");
            deep_sleep_register_gpio_wakeup(BAT_CHARGE_PIN, GPIO_NUM_NC);
            deep_sleep_enter_start();
        }
        else
        {
            ESP_LOGI("button", "Long press sleep XXX: [bat charging]!!!");
        }
        // TODO: 长按开始执行的动作
        break;
    default :
        break;
    }
}

void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {0};
    btn_cfg.long_press_time = 5000;

    button_gpio_config_t gpio_cfg = {
        .gpio_num = button_num,
        .active_level = BUTTON_ACTIVE_LEVEL,
        .enable_power_save = true,
    };

    button_handle_t btn;
    esp_err_t ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn);
    assert(ret == ESP_OK);

    // ret = iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, NULL, button_event_cb, NULL);
    ret = iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, NULL, button_event_cb, NULL);
    ret |= iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, NULL, button_event_cb, NULL);
    ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, NULL, button_event_cb, NULL);
    // ret |= iot_button_register_cb(btn, BUTTON_PRESS_END, NULL, button_event_cb, NULL);

#if CONFIG_ENTER_LIGHT_SLEEP_MODE_MANUALLY
    /*!< For enter Power Save */
    button_power_save_config_t config = {
        .enter_power_save_cb = button_enter_power_save,
    };
    ret |= iot_button_register_power_save_cb(&config);
#endif

    ESP_ERROR_CHECK(ret);
}

void app_btn_init(void)
{


    button_init(BOOT_BUTTON_NUM);

}