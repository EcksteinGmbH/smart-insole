#include <stdio.h>
#include "imu.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "IMU";

// ================== LSM6DS3TR-C 初始化 ==================
bool imu_init(imux_t *imu, uint8_t addr)
{
    imu->addr = addr;
    imu->initialized = false;
    uint8_t who_am_i = 0;

    if (i2c_read(addr, WHO_AM_I_REG, &who_am_i, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I from 0x%02X", addr);
        return false;
    }
    if (who_am_i != WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: 0x%02X (expected 0x%02X)", who_am_i, WHO_AM_I_VAL);
        return false;
    }
    imu->initialized = true;

    // 配置陀螺仪 ODR = 52 Hz, 满量程 ±500 dps
    // CTRL2_G (0x11): ODR_G=0101 (52 Hz), FS_G=01 (±500 dps)
    i2c_write(addr, CTRL2_G, 0x34);

    // 配置加速度计 ODR = 52 Hz, 满量程 ±4 g
    // CTRL1_XL (0x10): ODR_XL=0101 (52 Hz), FS_XL=10 (±4 g)
    i2c_write(addr, CTRL1_XL, 0x3A);

    // 控制寄存器3: IF_INC=1 (地址自动递增)，BDU=1，I2C 使能
    i2c_write(addr, CTRL3_C, 0x44);

    // 默认禁用中断
    i2c_write(addr, INT1_CTRL, 0x00);
    i2c_write(addr, CTRL10_C, 0x00);

    ESP_LOGI(TAG, "[LSM6DS3TR-C] detected at 0x%02X, WHO_AM_I=0x%02X", addr, who_am_i);
    return true;
}

// ================== 加速度数据读取 ==================
bool imu_read_accel(imux_t *imu)
{
    uint8_t data[6];
    if (i2c_read(imu->addr, OUTX_L_XL, data, 6) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read accelerometer data");
        return false;
    }

    // 小端组合
    int16_t raw_ax = (int16_t)(data[1] << 8 | data[0]);
    int16_t raw_ay = (int16_t)(data[3] << 8 | data[2]);
    int16_t raw_az = (int16_t)(data[5] << 8 | data[4]);

    /**
     * 加速度量程和灵敏度:
     * ±2 g  → 0.061 mg/LSB
     * ±4 g  → 0.122 mg/LSB
     * ±8 g  → 0.244 mg/LSB
     * ±16 g → 0.488 mg/LSB
     * 本例中配置的是 ±4 g，所以 sensitivity = 0.122 mg/LSB
     */
    float sensitivity = 0.122; // mg/LSB
    imu->ax = raw_ax * sensitivity;
    imu->ay = raw_ay * sensitivity;
    imu->az = raw_az * sensitivity;

    return true;
}

// ================== 陀螺仪数据读取 ==================
bool imu_read_gyro(imux_t *imu)
{
    uint8_t data[6];
    if (i2c_read(imu->addr, OUTX_L_G, data, 6) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read gyroscope data");
        return false;
    }

    int16_t raw_gx = (int16_t)(data[1] << 8 | data[0]);
    int16_t raw_gy = (int16_t)(data[3] << 8 | data[2]);
    int16_t raw_gz = (int16_t)(data[5] << 8 | data[4]);

    /**
     * 陀螺仪量程和灵敏度:
     * ±125 dps  → 0.004375 dps/LSB
     * ±250 dps  → 0.00875 dps/LSB
     * ±500 dps  → 0.0175 dps/LSB
     * ±1000 dps → 0.035  dps/LSB
     * ±2000 dps → 0.07   dps/LSB
     * 本例中配置的是 ±500 dps → sensitivity = 0.0175 dps/LSB
     */
    float sensitivity = 0.0175;
    imu->gx = raw_gx * sensitivity;
    imu->gy = raw_gy * sensitivity;
    imu->gz = raw_gz * sensitivity;

    return true;
}

// ================== 一次性读取并打印 ==================
void imu_read_data(imux_t *imu)
{
    if (!imu->initialized) {
        ESP_LOGW(TAG, "IMU not initialized");
        return;
    }
    imu_read_accel(imu);
    imu_read_gyro(imu);

    ESP_LOGI(TAG, "Accel(g): %07d, %07d, %07d | Gyro(dps): %07d, %07d, %07d",
             imu->ax, imu->ay, imu->az,
             imu->gx, imu->gy, imu->gz);
}

// ================== 加速度阈值唤醒配置 ==================
/**
 * @brief 配置 LSM6DS3TR-C 的加速度唤醒中断到 INT1
 * @param imu       IMU 设备指针
 * @param threshold 阈值 (0~127)，1LSB = FS_XL/64
 * @param duration  持续时间 (0~31)，单位 = 采样周期
 */
/**
 * @brief 配置 LSM6DS3TR-C 的加速度唤醒中断到 INT1
 * @param imu       IMU 设备指针
 * @param threshold 阈值 (0~127)，1LSB = FS_XL/64
 * @param duration  持续时间 (0~31)，单位 = 采样周期
 */
bool imu_config_accel_wakeup(imux_t *imu, uint8_t threshold, uint8_t duration)
{
    if (!imu->initialized) {
        ESP_LOGE(TAG, "IMU not initialized");
        return false;
    }

    uint8_t val = 0;

    // 1. 设置阈值 (WAKE_UP_THS)
    uint8_t wake_ths = threshold & 0x7F; // 保留 bit7=0
    i2c_write(imu->addr, WAKE_UP_THS, wake_ths);
    i2c_read(imu->addr, WAKE_UP_THS, &val, 1);
    ESP_LOGI(TAG, "[WAKE_UP_THS] set=0x%02X, readback=0x%02X", wake_ths, val);

    // 2. 设置持续时间 (WAKE_UP_DUR)
    uint8_t wake_dur = duration & 0x1F; // bit4:0
    i2c_write(imu->addr, WAKE_UP_DUR, wake_dur);
    i2c_read(imu->addr, WAKE_UP_DUR, &val, 1);
    ESP_LOGI(TAG, "[WAKE_UP_DUR] set=0x%02X, readback=0x%02X", wake_dur, val);

    // 3. 配置 TAP_CFG (启用中断功能 + LIR=1)
    i2c_read(imu->addr, TAP_CFG, &val, 1);
    uint8_t new_tap_cfg = val;
    new_tap_cfg |= (1 << 7); // INTERRUPTS_ENABLE
    new_tap_cfg |= (1 << 0); // LIR: 锁存中断
    new_tap_cfg &= ~(1 << 4); // SLOPE_FDS=0 (slope filter)
    i2c_write(imu->addr, TAP_CFG, new_tap_cfg);
    i2c_read(imu->addr, TAP_CFG, &val, 1);
    ESP_LOGI(TAG, "[TAP_CFG] set=0x%02X, readback=0x%02X", new_tap_cfg, val);

    // 4. 路由唤醒事件到 INT1 (MD1_CFG bit5=1)
    i2c_read(imu->addr, MD1_CFG, &val, 1);
    uint8_t new_md1_cfg = val | (1 << 5);
    i2c_write(imu->addr, MD1_CFG, new_md1_cfg);
    i2c_read(imu->addr, MD1_CFG, &val, 1);
    ESP_LOGI(TAG, "[MD1_CFG] set=0x%02X, readback=0x%02X", new_md1_cfg, val);

    ESP_LOGI(TAG, "Accel wakeup interrupt configured: THS=%d, DUR=%d (INT1)", threshold, duration);
    return true;
}



// ================== 唤醒源读取 ==================
/**
 * @brief 读取唤醒事件来源
 * @param x_triggered X轴触发
 * @param y_triggered Y轴触发
 * @param z_triggered Z轴触发
 */
bool imu_read_accel_wakeup_source(imux_t *imu, bool *x_triggered, bool *y_triggered, bool *z_triggered)
{
    if (!imu->initialized) return false;

    uint8_t src = 0;
    if (i2c_read(imu->addr, WAKE_UP_SRC, &src, 1) != ESP_OK) {
        return false;
    }

    *x_triggered = (src & (1 << 0));
    *y_triggered = (src & (1 << 1));
    *z_triggered = (src & (1 << 2));

    ESP_LOGI(TAG, "WAKE_UP_SRC=0x%02X (X=%d Y=%d Z=%d)", src, *x_triggered, *y_triggered, *z_triggered);
    return true;
}
