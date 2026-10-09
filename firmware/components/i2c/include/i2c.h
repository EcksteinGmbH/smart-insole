#ifndef __I2C_H__
#define __I2C_H__


#include <stdbool.h>
#include "esp_err.h"
// ================== LSM6DS3TR-C ================
#define I2C_MASTER_SCL_IO           0
#define I2C_MASTER_SDA_IO           1
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          1000000


// ================== I2C & 基础接口 ==================
esp_err_t i2c_master_init();
esp_err_t i2c_write(uint8_t sensor_addr, uint8_t reg_addr, uint8_t data);
esp_err_t i2c_write_buffer(uint8_t sensor_addr, uint8_t reg_addr, const uint8_t *data, size_t len);
esp_err_t i2c_read(uint8_t sensor_addr, uint8_t reg_addr, uint8_t *data, size_t len);

#endif   // __I2C_H__