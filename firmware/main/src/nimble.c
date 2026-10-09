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

#include "nimble.h"
#include "bsp.h"

#define TAG "ble"
#define DEVICE_NAME "ESP32C3_XRS"

// 缓冲区大小定义
#define NOTIFY_BUF_SIZE 256
#define TIMER_NOTIFY_BUF_SIZE 128

// UUID定义
const uint16_t SERVICE_UUID = 0xFFF0;
const uint16_t CHAR_NOTIFY_UUID = 0xFFF1;
const uint16_t CHAR_WRITE_UUID = 0xFFF2;
const uint16_t CHAR_TIMER_NOTIFY_UUID = 0xFFF4;

// 全局变量
static uint16_t conn_handle = 0;
static uint16_t notify_handle = 0;  // 用于 CHAR_NOTIFY_UUID (0xFFF1)
static uint16_t timer_notify_handle = 0;  // 用于 CHAR_TIMER_NOTIFY_UUID (0xFFF4)
static bool notify_enabled = false;
static bool timer_notify_enabled = false;  // 跟踪定时器通知状态
static ble_receive_cb_t receive_callback = NULL;  // 接收回调函数

// 发送队列和互斥锁
static QueueHandle_t data_queue = NULL;
static QueueHandle_t timer_queue = NULL;
static QueueHandle_t ble_rx_queue = NULL;
static SemaphoreHandle_t data_mutex = NULL;
static SemaphoreHandle_t timer_mutex = NULL;

// 数据队列项定义
typedef struct {
    uint8_t data[NOTIFY_BUF_SIZE];
    uint16_t len;
} data_queue_item_t;

typedef struct {
    uint8_t data[TIMER_NOTIFY_BUF_SIZE];
    uint16_t len;
} timer_queue_item_t;

// 定义接收数据队列项
typedef struct {
    uint8_t data[256];  // 根据最大预期数据长度调整
    uint16_t len;
} ble_rx_queue_item_t;


// 前置声明
static void ble_app_advertise(void);

// 接收数据处理任务
static void ble_receive_task(void *pvParameters)
{
    ble_rx_queue_item_t item;
    ble_receive_cb_t user_callback = NULL;
    
    while (1) {
        // 等待接收数据
        if (xQueueReceive(ble_rx_queue, &item, portMAX_DELAY) == pdTRUE) {
            // 获取当前注册的回调函数（需要加锁保护）
            if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE) {
                user_callback = receive_callback;
                xSemaphoreGive(data_mutex);
            }
            
            // 如果设置了回调函数，则调用它
            if (user_callback) {
                user_callback(item.data, item.len);
            }
        }
    }
}
// 手机写数据回调
static int gatt_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om = ctxt->om;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ble_rx_queue_item_t item;
        int len = OS_MBUF_PKTLEN(om);
        
        if (len > sizeof(item.data)) {
            ESP_LOGW(TAG, "Received data too large, truncating");
            len = sizeof(item.data);
        }
        
        os_mbuf_copydata(om, 0, len, item.data);
        item.len = len;
        
        // 将数据放入队列，非阻塞方式
        if (ble_rx_queue) {
            xQueueSend(ble_rx_queue, &item, 0);
        }
        // ESP_LOGI(TAG, "Received from phone, data queued for processing");
    }
    return 0;
}


static int empty_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // 什么也不做，只返回成功
    return 0;
}

// 定义透传服务
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(CHAR_NOTIFY_UUID),
                .access_cb = empty_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &notify_handle,
            },
            {
                .uuid = BLE_UUID16_DECLARE(CHAR_WRITE_UUID),
                .access_cb = gatt_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = BLE_UUID16_DECLARE(CHAR_TIMER_NOTIFY_UUID),
                .access_cb = empty_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &timer_notify_handle,
            },
            {0}
        },
    },
    {0}
};

// GAP事件回调
static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected");
        } else {
            ESP_LOGI(TAG, "Connection failed");
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected");
        conn_handle = 0;
        notify_enabled = false;
        timer_notify_enabled = false;
        ble_app_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        // 检查是哪个特征值的通知状态变化
        if (event->subscribe.attr_handle == notify_handle) {
            notify_enabled = event->subscribe.cur_notify > 0;
            // ESP_LOGI(TAG, "Data Notify %s", notify_enabled ? "enabled" : "disabled");
        } else if (event->subscribe.attr_handle == timer_notify_handle) {
            timer_notify_enabled = event->subscribe.cur_notify > 0;
            // ESP_LOGI(TAG, "Timer Notify %s", timer_notify_enabled ? "enabled" : "disabled");
        }
        break;
    default:
        break;
    }
    return 0;
}

// 广播初始化
static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = 0;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 1600;  // 1600 * 0.625ms = 1000ms
    adv_params.itvl_max = 1760;  // 最大间隔略大于最小，避免冲突
    // ble_gap_tx_power_set(BLE_HS_STOP, BLE_HCI_LE_PWR_LEVEL_N12);  // -12dBm


    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started");
    }
}

// Host stack reset 回调
static void on_stack_reset(int reason) {
    ESP_LOGI(TAG, "NimBLE stack reset, reason=%d", reason);
}

// Host stack sync 回调
static void on_stack_sync(void) {
    ble_app_advertise();
}

// Host配置初始化
static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;  // 不使用存储
}

// NimBLE host任务
static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task Started");

    // 阻塞运行NimBLE
    nimble_port_run();

    vTaskDelete(NULL);
}

// 透传通知任务
static void ble_notify_task(void *param)
{
    data_queue_item_t data_item;
    timer_queue_item_t timer_item;
    BaseType_t received;
    
    while (1) {
        // 检查是否有数据要发送到 CHAR_NOTIFY_UUID
        if (conn_handle != 0 && notify_handle != 0 && notify_enabled) {
            received = xQueueReceive(data_queue, &data_item, 0);
            if (received == pdTRUE) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(data_item.data, data_item.len);
                if (om) {
                    ble_gattc_notify_custom(conn_handle, notify_handle, om);
                }
            }
        }
        // 检查是否有数据要发送到 CHAR_TIMER_NOTIFY_UUID
        if (conn_handle != 0 && timer_notify_handle != 0 && timer_notify_enabled) {
            received = xQueueReceive(timer_queue, &timer_item, 0);
            if (received == pdTRUE) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(timer_item.data, timer_item.len);
                if (om) {
                    ble_gattc_notify_custom(conn_handle, timer_notify_handle, om);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // 20ms 发送间隔
    }
}
// 发送数据到 CHAR_NOTIFY_UUID
esp_err_t ble_send_data(const uint8_t *data, uint16_t len)
{
    if (!data || !data_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查数据长度
    if (len > NOTIFY_BUF_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 检查连接状态和通知是否启用
    if (conn_handle == 0) {
        ESP_LOGW(TAG, "Cannot send data: not connected");
        return ESP_FAIL;
    }
    
    if (!notify_enabled) {
        ESP_LOGW(TAG, "Cannot send data: notify not enabled");
        return ESP_FAIL;
    }
    
    if (notify_handle == 0) {
        ESP_LOGW(TAG, "Cannot send data: notify handle not set");
        return ESP_FAIL;
    }
    
    data_queue_item_t item;
    memcpy(item.data, data, len);
    item.len = len;
    
    // 发送到队列
    if (xQueueSend(data_queue, &item, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// 发送数据到 CHAR_TIMER_NOTIFY_UUID
esp_err_t ble_send_timer_data(const uint8_t *data, uint16_t len)
{
    if (!data || !timer_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查数据长度
    if (len > TIMER_NOTIFY_BUF_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 检查连接状态和通知是否启用
    if (conn_handle == 0) {
        ESP_LOGW(TAG, "Cannot send timer data: not connected");
        return ESP_FAIL;
    }
    
    if (!timer_notify_enabled) {
        ESP_LOGW(TAG, "Cannot send timer data: timer notify not enabled");
        return ESP_FAIL;
    }
    
    if (timer_notify_handle == 0) {
        ESP_LOGW(TAG, "Cannot send timer data: timer notify handle not set");
        return ESP_FAIL;
    }
    
    timer_queue_item_t item;
    memcpy(item.data, data, len);
    item.len = len;
    
    // 发送前检查队列剩余空间
    // UBaseType_t queue_space = uxQueueSpacesAvailable(timer_queue);
    // ESP_LOGW(TAG, "Low space in timer_queue: %u", queue_space);
    
    // 发送到队列
    if (xQueueSend(timer_queue, &item, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    

    
    return ESP_OK;
}

// 设置接收数据回调函数
void ble_set_receive_callback(ble_receive_cb_t callback)
{
    receive_callback = callback;
}

// 检查是否已连接
bool ble_is_connected(void)
{
    return (conn_handle != 0);
}

// 检查数据通知是否已启用
bool ble_is_data_notify_enabled(void)
{
    return notify_enabled;
}

// 检查定时器通知是否已启用
bool ble_is_timer_notify_enabled(void)
{
    return timer_notify_enabled;
}

int gap_init(void) {
    /* Local variables */
    int rc = 0;

    /* Call NimBLE GAP initialization API */
    ble_svc_gap_init();

    /* Set GAP device name */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
                 DEVICE_NAME, rc);
        return rc;
    }
    return rc;
}

int gatt_svc_init(void) {
    /* Local variables */
    int rc;

    
    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

void nimble_init(void)
{
    esp_err_t ret;

    // 1. 创建队列和互斥锁
    data_queue = xQueueCreate(10, sizeof(data_queue_item_t));
    timer_queue = xQueueCreate(10, sizeof(timer_queue_item_t));
    ble_rx_queue = xQueueCreate(10, sizeof(ble_rx_queue_item_t));  // 创建接收队列
    data_mutex = xSemaphoreCreateMutex();
    timer_mutex = xSemaphoreCreateMutex();
    
    if (!data_queue || !timer_queue || !ble_rx_queue || !data_mutex || !timer_mutex) {
        ESP_LOGE(TAG, "Failed to create queues or mutexes");
        return;
    }
    // 2. 初始化 NimBLE
    ret = nimble_port_init();
    ESP_ERROR_CHECK(ret);

    // 3. 初始化 GAP / GATT
    gap_init();
    gatt_svc_init();
    
    // 4. Host配置
    nimble_host_config_init();

    // 5. 启动 NimBLE Host 任务
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, PRIORITY_NIMBLE_TASK, NULL);

    // 6. 启动透传通知任务
    xTaskCreate(ble_notify_task, "BLE Notify", 4*1024, NULL, PRIORITY_NIMBLE_TASK, NULL);
    // 7. 启动接收数据处理任务
    xTaskCreate(ble_receive_task, "BLE Receive", 4*1024, NULL, PRIORITY_NIMBLE_TASK, NULL);
}