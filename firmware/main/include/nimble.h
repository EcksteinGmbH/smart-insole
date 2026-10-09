#ifndef NIMBLE_H
#define NIMBLE_H


#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"


typedef void (*ble_receive_cb_t)(const uint8_t *data, uint16_t len);



// 发送数据到 CHAR_NOTIFY_UUID
esp_err_t ble_send_data(const uint8_t *data, uint16_t len);
// 发送数据到 CHAR_TIMER_NOTIFY_UUID
esp_err_t ble_send_timer_data(const uint8_t *data, uint16_t len);
// 设置接收数据回调函数
void ble_set_receive_callback(ble_receive_cb_t callback);
// 检查是否已连接
bool ble_is_connected(void);
// 检查数据通知是否已启用
bool ble_is_data_notify_enabled(void);
// 检查定时器通知是否已启用
bool ble_is_timer_notify_enabled(void);
void nimble_init(void);


#endif // NIMBLE_H