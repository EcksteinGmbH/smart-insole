#include "led.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "led";


/**
 * @brief 初始化 LED
 */
void led_init(led_t *led, gpio_num_t pin, int active_level)
{
    led->pin = pin;
    led->active_level = active_level;
    led->state = 0; // 默认熄灭
    gpio_set_level(pin, !active_level);

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(led->pin, !led->active_level);

    ESP_LOGI(TAG, "LED init: GPIO%d, active_level=%d", led->pin, led->active_level);
}

/**
 * @brief 打开 LED
 */
void led_on(led_t *led)
{
    gpio_set_level(led->pin, led->active_level);
    led->state = 1;
}

/**
 * @brief 关闭 LED
 */
void led_off(led_t *led)
{
    gpio_set_level(led->pin, !led->active_level);
    led->state = 0;
}

/**
 * @brief 翻转 LED 状态
 */
void led_toggle(led_t *led)
{
    led->state = !led->state;
    gpio_set_level(led->pin, led->state ? led->active_level : !led->active_level);
}

/**
 * 示例：
 * led_t led_r, led_g;
 * led_init(&led_r, GPIO_NUM_8, 1);  // 红灯高亮
 * led_init(&led_g, GPIO_NUM_10, 0); // 绿灯低亮
 *
 * led_on(&led_r);
 * led_off(&led_g);
 * led_toggle(&led_r);
 */

