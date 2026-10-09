#ifndef __BSP_H__
#define __BSP_H__

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "imu.h"
#include "motor.h"

#include "utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ================== LSM6DS3TR-C  在i2c中定义================
// #define I2C_MASTER_SCL_IO           0
// #define I2C_MASTER_SDA_IO           1
// #define I2C_MASTER_NUM              I2C_NUM_0
// #define I2C_MASTER_FREQ_HZ          1000000

#define IMU0_INIT1_PIN  GPIO_NUM_4  // 第一个 LSM6DS3TR-C 的 INT2
#define IMU0_INIT2_PIN  GPIO_NUM_NC // 第二个 LSM6DS3TR-C 的 INT2

#define IMU1_INIT1_PIN  GPIO_NUM_NC  // 第一个 LSM6DS3TR-C 的 INT2
#define IMU1_INIT2_PIN  GPIO_NUM_NC // 第二个 LSM6DS3TR-C 的 INT2

// ================== BOOT BUTTON ================
#define BOOT_BUTTON_NUM         9

// ================== LED ================
#define LED_RED_PIN                 GPIO_NUM_8
#define LED_GREEN_PIN               GPIO_NUM_10


// ================== BATTERY VOLTAGE ================
#define BAT_ADC_PIN        GPIO_NUM_2
#define BAT_ADC_CHANNEL     ADC_CHANNEL_2   // 例如 GPIO34 (ADC1_CH6)
#define BAT_P0_VOLTAGE      3.6f            // 0% 对应电压
#define BAT_P100_VOLTAGE    4.05f            // 100% 对应电压
// ================== USB POWER DETECHT ================
#define BAT_CHARGE_PIN      GPIO_NUM_3      // 例如连接到充电芯片 CHG 引脚
#define BAT_CHARGE_WAKEUP_LEVEL  ESP_GPIO_WAKEUP_GPIO_HIGH



// ================== MOTOR ================

#define MOTOR1_PIN                 GPIO_NUM_5
#define MOTOR2_PIN                 GPIO_NUM_6
#define MOTOR3_PIN                 GPIO_NUM_7


//=========优先级配置================
// 任务优先级high to low
#define PRIORITY_MOTORX_TASK          10
#define PRIORITY_APP_LED_TASK         9
#define PRIORITY_NIMBLE_TASK          8
#define PRIORITY_DEEP_SLEEP_TASK      5
#define PRIORITY_BAT_TASK             5
#define PRIORITY_IMU_TASK             5



#endif // __BSP_H__