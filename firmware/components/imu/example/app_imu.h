#ifndef __APP_IMU_H__
#define __APP_IMU_H__

#include <stdio.h>
#include "imu.h"

extern imux_t imux[];

int app_imu_init(void);
void app_imu_loop();

#endif // __APP_IMU_H__
