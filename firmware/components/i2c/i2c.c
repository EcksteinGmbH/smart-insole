#include <stdio.h>
#include "i2c.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "I2C";


bool i2c_initiated = false;

// ================== I2C 初始化 ==================
esp_err_t i2c_master_init()
{
    if(i2c_initiated)
    {
        ESP_LOGI(TAG, "I2C already initiated");
        return ESP_OK;
    }
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_initiated = true;
    return ESP_OK;
}

// ================== I2C 读写函数 ==================
esp_err_t i2c_write(uint8_t sensor_addr, uint8_t reg_addr, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}
/**
 * @brief 向 I2C 设备写多个字节
 * @param sensor_addr  I2C 设备地址
 * @param reg_addr     寄存器起始地址
 * @param data         要写入的数据缓冲区指针
 * @param len          要写入的字节数
 * @return ESP_OK 成功，否则返回 ESP-IDF 错误码
 */
esp_err_t i2c_write_buffer(uint8_t sensor_addr, uint8_t reg_addr, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);

    // 写设备地址 + 写方向
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_WRITE, true);

    // 写寄存器地址
    i2c_master_write_byte(cmd, reg_addr, true);

    // 写缓冲区数据
    i2c_master_write(cmd, (uint8_t *)data, len, true);

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}


esp_err_t i2c_read(uint8_t sensor_addr, uint8_t reg_addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}
