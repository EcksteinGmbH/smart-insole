#include "imu.h"
void imu_loopx()
{
    // static uint32_t last = 0;
    // if( millis() - last < 500 ) {
    //     return;
    // }
    // last = millis();
    //  // int16_t gx0, gy0, gz0;
    //     int16_t gx1, gy1, gz1;

    //     bool ok0 = sensor0_ok && read_gyro_data(LSM6DS3TR_C_ADDR_0, &gx0, &gy0, &gz0);
    //     bool ok1 = sensor1_ok && read_gyro_data(LSM6DS3TR_C_ADDR_1, &gx1, &gy1, &gz1);

    //     if (ok0 || ok1) {
    //         ESP_LOGI(TAG, "Gyro Data:");
    //         if (ok0) {
    //             ESP_LOGI(TAG, "  Sensor 0 (0x6A): Gx=%6d, Gy=%6d, Gz=%6d", gx0, gy0, gz0);
    //         }
    //         if (ok1) {
    //             ESP_LOGI(TAG, "  Sensor 1 (0x6B): Gx=%6d, Gy=%6d, Gz=%6d", gx1, gy1, gz1);
    //         }
    //     } else {
    //         ESP_LOGW(TAG, "Failed to read gyro data from both sensors.");
    //     }

    //     // func();

    //     read_imu_data(0x6A);
    //     vTaskDelay(pdMS_TO_TICKS(500));
    //     // read_imu_data(0x6B);
    //     // vTaskDelay(pdMS_TO_TICKS(500));
}