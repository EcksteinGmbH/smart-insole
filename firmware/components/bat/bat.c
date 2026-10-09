#include "bat.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_continuous.h"
#include "soc/soc_caps.h"
#include "filter_rc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bat_driver";

#define BAT_SAMPLE_COUNT 64 // 每次采样点数
#define BAT_CONV_MODE ADC_CONV_SINGLE_UNIT_1
#define BAT_CONV_FORMAT ADC_DIGI_OUTPUT_FORMAT_TYPE2

typedef struct
{
    adc_continuous_handle_t cont_handle;
    adc_cali_handle_t cali_handle;
    bool cali_enabled;
} bat_adc_ctx_t;

static bat_adc_ctx_t s_adc_ctx = {0};

/**
 * @brief 初始化电池监测模块（按需采样模式）
 */
void bat_init(bat_t *bat, gpio_num_t charger_pin, adc_channel_t adc_ch, float p0_voltage, float p100_voltage)
{
    ESP_LOGI(TAG, "Initializing battery monitor (on-demand sampling)...");

    bat->charger_pin = charger_pin;
    bat->adc_ch = adc_ch;
    bat->p0_voltage = p0_voltage;
    bat->p100_voltage = p100_voltage;
    bat->voltage = 0.0f;
    bat->percent = 0.0f;
    bat->is_charging = false;
    FilterEWMA_init(&bat->filter, 0.2f);

    // === 配置充电检测引脚 ===
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << charger_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // === 配置 ADC ===
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = BAT_SAMPLE_COUNT * sizeof(adc_digi_output_data_t),
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &s_adc_ctx.cont_handle));

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_11,
        .channel = bat->adc_ch,
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };

    adc_continuous_config_t dig_cfg = {
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
        .sample_freq_hz = 1000, // 采样频率可以保持不变，因为实际使用时间很短
        .conv_mode = BAT_CONV_MODE,
        .format = BAT_CONV_FORMAT,
    };
    ESP_ERROR_CHECK(adc_continuous_config(s_adc_ctx.cont_handle, &dig_cfg));

    // 注意：初始化时不再启动ADC，改为按需启动

    // === 校准初始化 ===
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&(
        adc_cali_curve_fitting_config_t){
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        },
        &s_adc_ctx.cali_handle);

    if (ret == ESP_OK)
    {
        s_adc_ctx.cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled (curve fitting)");
    }
    else
    {
        s_adc_ctx.cali_enabled = false;
        ESP_LOGW(TAG, "Curve fitting calibration not supported, fallback to raw ADC");
    }
}
/**

* @brief 设置电压百分比的上下限
  */
void bat_set_p0_p100(bat_t *bat, float p0_voltage, float p100_voltage)
{
    bat->p0_voltage = p0_voltage;
    bat->p100_voltage = p100_voltage;
}
/**
 * @brief 读取平均电压并更新状态（按需采样，降低功耗）
 */
void bat_update_voltage(bat_t *bat)
{
    uint8_t result[1024] = {0};
    uint32_t bytes_read = 0;
    int total_mv = 0;
    int valid_samples = 0;

    // 1. 启动ADC采样（按需启动）
    esp_err_t start_ret = adc_continuous_start(s_adc_ctx.cont_handle);
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC: %s", esp_err_to_name(start_ret));
        return;
    }

    // 2. 短暂延迟确保有足够的采样数据
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // 3. 读取采样数据
    esp_err_t read_ret = adc_continuous_read(s_adc_ctx.cont_handle, result, sizeof(result), &bytes_read, 100);
    
    // 4. 立即停止ADC采样
    adc_continuous_stop(s_adc_ctx.cont_handle);
    
    if (read_ret == ESP_OK && bytes_read > 0)
    {
        adc_digi_output_data_t *data = (adc_digi_output_data_t *)result;
        int samples = bytes_read / sizeof(adc_digi_output_data_t);

        for (int i = 0; i < samples; i++)
        {
            if (data[i].type2.channel == bat->adc_ch)
            {
                int raw = data[i].type2.data;
                int voltage_mv = 0;

                if (s_adc_ctx.cali_enabled)
                {
                    adc_cali_raw_to_voltage(s_adc_ctx.cali_handle, raw, &voltage_mv);
                }
                else
                {
                    voltage_mv = raw * 3300 / 4095;
                }

                total_mv += voltage_mv;
                valid_samples++;
            }
        }

        if (valid_samples > 0)
        {
            float avg_mv = (float)total_mv / valid_samples;
            float voltage = 2 * avg_mv / 1000.0f; // 分压比修正

            // 应用滤波器
            bat->voltage = FilterEWMA_input(&bat->filter, voltage);

            // 电量百分比换算
            float percent = (bat->voltage - bat->p0_voltage) / (bat->p100_voltage - bat->p0_voltage) * 100.0f;
            if (percent < 0.0f)
                percent = 0.0f;
            if (percent > 100.0f)
                percent = 100.0f;
            bat->percent = percent;

            bat->is_charging = gpio_get_level(bat->charger_pin);

            ESP_LOGD(TAG, "Samples=%d, Voltage=%.3fV, Percent=%.1f%%, Charging=%s",
                     valid_samples, bat->voltage, bat->percent,
                     bat->is_charging ? "YES" : "NO");
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to read ADC data: %s, bytes_read=%lu", esp_err_to_name(read_ret), bytes_read);
    }
}

/**

* @brief 获取电压、电量、状态
  */
float bat_get_voltage(bat_t *bat) { return bat->voltage; }
float bat_get_percent(bat_t *bat) { return bat->percent; }
bool bat_is_charging(bat_t *bat)
 { 
    bat->is_charging = gpio_get_level(bat->charger_pin);
    return bat->is_charging;
 }
bool bat_is_lowpower(bat_t *bat) { return (bat->percent < 1.0f); }
bool bat_is_full(bat_t *bat) { return (bat->percent > 99.5f); }

/**

* @brief 释放资源
  */
void bat_deinit(bat_t *bat)
{
    if (s_adc_ctx.cont_handle)
    {
        adc_continuous_stop(s_adc_ctx.cont_handle);
        adc_continuous_deinit(s_adc_ctx.cont_handle);
        s_adc_ctx.cont_handle = NULL;
    }

    if (s_adc_ctx.cali_enabled)
    {
        adc_cali_delete_scheme_curve_fitting(s_adc_ctx.cali_handle);
        s_adc_ctx.cali_enabled = false;
    }

    ESP_LOGI(TAG, "Battery monitor deinitialized");
}
