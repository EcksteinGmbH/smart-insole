#ifndef __IMU_H
#define __IMU_H

#include <stdbool.h>
#include "esp_err.h"
#include "i2c.h"

// ================== 宏定义 ==================


#define LSM6DS3TR_C_ADDR_0          0x6A  // SDO/SA0 接 GND
#define LSM6DS3TR_C_ADDR_1          0x6B  // SDO/SA0 接 VDD

#define WHO_AM_I_REG                0x0F
#define WHO_AM_I_VAL                0x6A // 0x69 也可能是有效值

// 控制寄存器
#define CTRL1_XL    0x10
#define CTRL2_G     0x11
#define CTRL3_C     0x12
#define CTRL4_C     0x13
#define CTRL5_C     0x14
#define CTRL6_C     0x15
#define CTRL7_G     0x16
#define CTRL8_XL    0x17
#define CTRL9_XL    0x18
#define CTRL10_C    0x19

// 中断控制
#define INT1_CTRL   0x0D
#define INT2_CTRL   0x0E
#define WAKE_UP_SRC 0x1B
#define TAP_SRC     0x1C
#define D6D_SRC     0x1D
#define STATUS_REG  0x1E

// 陀螺仪输出寄存器
#define OUTX_L_G    0x22
#define OUTX_H_G    0x23
#define OUTY_L_G    0x24
#define OUTY_H_G    0x25
#define OUTZ_L_G    0x26
#define OUTZ_H_G    0x27

// 加速度计输出寄存器
#define OUTX_L_XL   0x28
#define OUTX_H_XL   0x29
#define OUTY_L_XL   0x2A
#define OUTY_H_XL   0x2B
#define OUTZ_L_XL   0x2C
#define OUTZ_H_XL   0x2D

// 唤醒/自由落体/6D 检测寄存器
#define TAP_CFG     0x58
#define TAP_THS_6D  0x59
#define INT_DUR2    0x5A
#define WAKE_UP_THS 0x5B
#define WAKE_UP_DUR 0x5C
#define FREE_FALL   0x5D
#define MD1_CFG     0x5E
#define MD2_CFG     0x5F

// 中断回调函数指针类型
typedef void (*imu_irq_callback_t)(void);

typedef struct {
    uint8_t addr;          // I2C 地址
    int16_t ax, ay, az;    // 加速度
    int16_t gx, gy, gz;    // 陀螺仪
    float temp;            // 温度
    bool initialized;      // 是否初始化成功
    imu_irq_callback_t irq_callback; // 中断回调函数
} imux_t;



// ================== LSM6DS3TR-C 功能 ==================
bool imu_init(imux_t *imu, uint8_t addr);
bool imu_read_accel(imux_t *imu);
bool imu_read_gyro(imux_t *imu);
void imu_read_data(imux_t *imu);

// ================== 唤醒中断接口 ==================
bool imu_config_accel_wakeup(imux_t *imu, uint8_t threshold, uint8_t duration);
bool imu_read_accel_wakeup_source(imux_t *imu, bool *x_triggered, bool *y_triggered, bool *z_triggered);

#endif // __IMU_H
