#ifndef __APP_LSM6D_H__
#define __APP_LSM6D_H__


#include <stdio.h>
#include "lsm6d.h"
#include <stdint.h>
#include <stdbool.h>
#include "ripple.h"


typedef void (*imu_inactive_cb_t)(void *imu, bool is_inactive);

typedef struct {
    stmdev_ctx_t *ctx;
    int int1_pin;
    int int2_pin;
    bool initialized;
    bool enable;
    uint16_t id;
    uint32_t acc_sleep_thresh_mg; //加速度唤醒阈值，单位mg
    uint32_t acc_sleep_after_s; //加速度传感器 sleep 后延时，单位s
    float_t gx_mdps;
    float_t gy_mdps;
    float_t gz_mdps;
    float_t ax_mg;
    float_t ay_mg;
    float_t az_mg;
    int16_t gx_dps_x10;
    int16_t gy_dps_x10;
    int16_t gz_dps_x10;
    int16_t ax_g_x1000;
    int16_t ay_g_x1000;
    int16_t az_g_x1000;
    float_t temperature_degC;

    float freq[2];
    bool is_inactive;
    imu_inactive_cb_t inactive_cb;
    uint32_t _lastimestamp;
    uint32_t _counter[2];
    Ripple_t rpl_mg;
    Ripple_t rpl_mdps;
} imu_t;


extern imu_t imu[];


int app_lsm6d_init(void);
int app_lsm6d_acc_sleep_thresh_set(imu_t *imu, uint32_t thresh);
uint32_t app_lsm6d_acc_sleep_thresh_get(imu_t *imu);
bool app_lsm6d_is_inactive(imu_t *imu);
int app_lsm6d_enable_set(imu_t *imu, bool enable);
bool app_lsm6d_enable_get(imu_t *imu);
uint32_t app_lsm6d_acc_sleep_after_get(imu_t *imu);
int app_lsm6d_acc_sleep_after_set(imu_t *imu, uint32_t sleep_after_s);
bool app_lsm6d_attach(imu_t *imu, imu_inactive_cb_t inactive_cb);

void app_lsm6d_detach(imu_t *imu);
#endif   // __APP_LSM6D_H__