// #include <stdio.h>
// #include <string.h>
// #include "esp_log.h"
// #include "esp_bt.h"
// #include "esp_gap_ble_api.h"
// #include "esp_gatts_api.h"
// #include "esp_bt_main.h"
// #include "esp_gatt_common_api.h"
// #include "esp_timer.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "protocol.h"

// #define TAG "BLE_TRANSPARENT"

// // ---------------- 配置 ----------------
// #ifndef MAX_BLE_CHAR_LEN
// #define MAX_BLE_CHAR_LEN 61   // MTU=64 -> 有效负载 = 64 - 3
// #endif

// #define PROFILE_APP_ID 0
// #define GATTS_NUM_HANDLE 9  // SVC + 0xFFF1(Notify+CCCD) + 0xFFF2(Write) + 0xFFF4(Timer+CCCD)

// // ---------------- 数据结构 ----------------
// typedef enum {
//     IDX_SVC,
//     IDX_CHAR_NOTIFY_DECL,
//     IDX_CHAR_NOTIFY_VAL,
//     IDX_CHAR_NOTIFY_CCCD,
//     IDX_CHAR_WRITE_DECL,
//     IDX_CHAR_WRITE_VAL,
//     IDX_CHAR_TIMER_DECL,
//     IDX_CHAR_TIMER_VAL,
//     IDX_CHAR_TIMER_CCCD
// } gatts_attr_idx_t;

// struct gatts_profile_inst {
//     esp_gatts_cb_t gatts_cb;
//     uint16_t gatts_if;
//     uint16_t app_id;
//     uint16_t conn_id;
//     bool notify_enabled;
//     bool timer_notify_enabled;
//     char last_write_value[MAX_BLE_CHAR_LEN + 1];
//     uint16_t handles[GATTS_NUM_HANDLE];
// };

// static struct gatts_profile_inst gl_profile_tab[1] = {
//     [0] = {
//         .gatts_cb = NULL,
//         .gatts_if = ESP_GATT_IF_NONE,
//         .notify_enabled = false,
//         .timer_notify_enabled = false,
//         .conn_id = 0xFFFF,
//         .last_write_value = {0},
//     }
// };

// // ---------------- 定时器 & 队列 ----------------
// static esp_timer_handle_t timer_notify;
// typedef struct {
//     uint16_t handle;
//     char data[MAX_BLE_CHAR_LEN*10]; // 最大支持 10 段
//     int len;
// } ble_packet_t;

// static QueueHandle_t ble_send_queue;
// static SemaphoreHandle_t ble_mutex; // 确保 conn_id / notify_enabled 读取安全

// // ---------------- UUID ----------------
// static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
// static const uint16_t char_decl_uuid       = ESP_GATT_UUID_CHAR_DECLARE;
// static const uint16_t char_cccd_uuid       = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

// // 自定义透传服务 UUID
// static const uint16_t service_uuid      = 0xFFF0;
// static const uint16_t char_notify_uuid  = 0xFFF1;
// static const uint16_t char_write_uuid   = 0xFFF2;
// static const uint16_t char_timer_uuid   = 0xFFF4;

// // 特征值
// static uint8_t char_notify_val[MAX_BLE_CHAR_LEN] = {0};
// static uint8_t char_write_val[MAX_BLE_CHAR_LEN]  = {0};
// static uint8_t char_timer_val[MAX_BLE_CHAR_LEN]  = {0};

// // ---------------- GATT 数据库 ----------------
// static esp_gatts_attr_db_t gatt_db[GATTS_NUM_HANDLE] = {
//     [IDX_SVC] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&primary_service_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = sizeof(service_uuid),
//             .length      = sizeof(service_uuid),
//             .value       = (uint8_t*)&service_uuid
//         },
//     },
//     // Notify特征 0xFFF1
//     [IDX_CHAR_NOTIFY_DECL] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_decl_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = 1,
//             .length      = 1,
//             .value       = (uint8_t[]){ESP_GATT_CHAR_PROP_BIT_NOTIFY}
//         },
//     },
//     [IDX_CHAR_NOTIFY_VAL] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_notify_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = MAX_BLE_CHAR_LEN,
//             .length      = sizeof(char_notify_val),
//             .value       = char_notify_val
//         },
//     },
//     [IDX_CHAR_NOTIFY_CCCD] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_cccd_uuid,
//             .perm        = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
//             .max_length  = 2,
//             .length      = 2,
//             .value       = (uint8_t[]){0x00,0x00}
//         },
//     },
//     // Write特征 0xFFF2
//     [IDX_CHAR_WRITE_DECL] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_decl_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = 1,
//             .length      = 1,
//             .value       = (uint8_t[]){ESP_GATT_CHAR_PROP_BIT_WRITE}
//         },
//     },
//     [IDX_CHAR_WRITE_VAL] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_write_uuid,
//             .perm        = ESP_GATT_PERM_WRITE,
//             .max_length  = MAX_BLE_CHAR_LEN,
//             .length      = sizeof(char_write_val),
//             .value       = char_write_val
//         },
//     },
//     // Timer特征 0xFFF4
//     [IDX_CHAR_TIMER_DECL] = {
//         .attr_control = {.auto_rsp = ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_decl_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = 1,
//             .length      = 1,
//             .value       = (uint8_t[]){ESP_GATT_CHAR_PROP_BIT_NOTIFY}
//         },
//     },
//     [IDX_CHAR_TIMER_VAL] = {
//         .attr_control = {.auto_rsp=ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_timer_uuid,
//             .perm        = ESP_GATT_PERM_READ,
//             .max_length  = MAX_BLE_CHAR_LEN,
//             .length      = sizeof(char_timer_val),
//             .value       = char_timer_val
//         },
//     },
//     [IDX_CHAR_TIMER_CCCD] = {
//         .attr_control = {.auto_rsp=ESP_GATT_RSP_BY_APP},
//         .att_desc = {
//             .uuid_length = ESP_UUID_LEN_16,
//             .uuid_p      = (uint8_t*)&char_cccd_uuid,
//             .perm        = ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
//             .max_length  = 2,
//             .length      = 2,
//             .value       = (uint8_t[]){0x00,0x00}
//         },
//     }
// };

// // ---------------- 分段发送任务 ----------------
// static void ble_send_task(void *param) {
//     ble_packet_t pkt;
//     struct gatts_profile_inst *profile = &gl_profile_tab[PROFILE_APP_ID];
//     while(1){
//         if(xQueueReceive(ble_send_queue, &pkt, portMAX_DELAY)){
//             if(profile->conn_id == 0xFFFF) continue;
//             int offset = 0;
//             int chunk_size = MAX_BLE_CHAR_LEN;
//             while(offset < pkt.len){
//                 int len = (pkt.len - offset > chunk_size) ? chunk_size : (pkt.len - offset);
//                 esp_err_t ret = esp_ble_gatts_send_indicate(profile->gatts_if, profile->conn_id,
//                                 pkt.handle, len, (uint8_t*)(pkt.data + offset), false);
//                 if(ret != ESP_OK){
//                     vTaskDelay(pdMS_TO_TICKS(5)); // 重试
//                     continue;
//                 }
//                 offset += len;
//             }
//         }
//     }
// }

// // ---------------- 定时器回调 ----------------
// static void periodic_notify_cb(void *arg){
//     struct gatts_profile_inst *profile = &gl_profile_tab[PROFILE_APP_ID];
//     if(!profile->timer_notify_enabled || profile->conn_id==0xFFFF) return;

//     uint8_t data[MAX_BLE_CHAR_LEN*10];
//     int len = protocol_make_imu_frame(data, sizeof(data)); // 获取 IMU 二进制数据
//     if(len > 0){
//         ble_packet_t pkt;
//         pkt.handle = profile->handles[IDX_CHAR_TIMER_VAL];
//         pkt.len = len > sizeof(pkt.data) ? sizeof(pkt.data) : len;
//         memcpy(pkt.data, data, pkt.len);
//         xQueueSend(ble_send_queue, &pkt, 0);
//     }
// }

// // ---------------- GAP 回调 ----------------
// static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param){
//     if(event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT){
//         esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
//             .adv_int_min=0x20,.adv_int_max=0x40,.adv_type=ADV_TYPE_IND,
//             .own_addr_type=BLE_ADDR_TYPE_PUBLIC,.channel_map=ADV_CHNL_ALL,
//             .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY});
//     }
// }

// // ---------------- GATT Profile 事件 ----------------
// static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
//                                         esp_gatt_if_t gatts_if,
//                                         esp_ble_gatts_cb_param_t *param){
//     struct gatts_profile_inst *profile = &gl_profile_tab[PROFILE_APP_ID];

//     switch(event){
//         case ESP_GATTS_REG_EVT:{
//             esp_ble_gap_set_device_name("ESP32_Transparent");
//             esp_ble_gap_config_adv_data(&(esp_ble_adv_data_t){
//                 .set_scan_rsp=false,.include_name=true,.include_txpower=true,
//                 .flag=(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
//             });
//             esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, GATTS_NUM_HANDLE, 0);
//             break;
//         }
//         case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
//             memcpy(profile->handles, param->add_attr_tab.handles, sizeof(profile->handles));
//             esp_ble_gatts_start_service(profile->handles[IDX_SVC]);
//             break;
//         }
//         case ESP_GATTS_CONNECT_EVT:
//             profile->conn_id = param->connect.conn_id;
//             profile->gatts_if = gatts_if;
//             ESP_LOGI(TAG,"CONNECTED, conn_id=%d",profile->conn_id);
//             break;
//         case ESP_GATTS_DISCONNECT_EVT:
//             profile->conn_id = 0xFFFF;
//             profile->notify_enabled = false;
//             profile->timer_notify_enabled = false;
//             esp_timer_stop(timer_notify);
//             esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
//                 .adv_int_min=0x20,.adv_int_max=0x40,.adv_type=ADV_TYPE_IND,
//                 .own_addr_type=BLE_ADDR_TYPE_PUBLIC,.channel_map=ADV_CHNL_ALL,
//                 .adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY});
//             break;
//         case ESP_GATTS_WRITE_EVT:{
//             // 0xFFF1 CCCD
//             if(param->write.handle == profile->handles[IDX_CHAR_NOTIFY_CCCD] && param->write.len==2){
//                 uint16_t cccd = param->write.value[1]<<8 | param->write.value[0];
//                 profile->notify_enabled = (cccd==0x0001);
//             } 
//             // 0xFFF4 CCCD
//             else if(param->write.handle == profile->handles[IDX_CHAR_TIMER_CCCD] && param->write.len==2){
//                 uint16_t cccd = param->write.value[1]<<8 | param->write.value[0];
//                 profile->timer_notify_enabled = (cccd==0x0001);
//                 if(profile->timer_notify_enabled){
//                     esp_timer_start_periodic(timer_notify, 30000); // 20ms -> 50Hz
//                 } else {
//                     esp_timer_stop(timer_notify);
//                 }
//             }
//             // Write特征 0xFFF2
//             else if(param->write.handle == profile->handles[IDX_CHAR_WRITE_VAL]){
//                 int len = param->write.len > MAX_BLE_CHAR_LEN ? MAX_BLE_CHAR_LEN : param->write.len;
//                 protocol_handle_json((char*)param->write.value);
//                 memcpy(profile->last_write_value, param->write.value, len);
//                 profile->last_write_value[len]='\0';
//                 ESP_LOGI(TAG,"Write Feature Received: %s",profile->last_write_value);
//             }

//             if(param->write.need_rsp){
//                 esp_gatt_rsp_t rsp;
//                 memset(&rsp,0,sizeof(rsp));
//                 rsp.attr_value.handle = param->write.handle;
//                 rsp.attr_value.len = param->write.len;
//                 memcpy(rsp.attr_value.value,param->write.value,param->write.len);
//                 esp_ble_gatts_send_response(profile->gatts_if,param->write.conn_id,
//                                             param->write.trans_id,ESP_GATT_OK,&rsp);
//             }
//             break;
//         }
//         default: break;
//     }
// }

// // ---------------- GATTS 事件分发 ----------------
// static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
//                                 esp_ble_gatts_cb_param_t *param){
//     if(event==ESP_GATTS_REG_EVT && param->reg.status==ESP_GATT_OK)
//         gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;

//     for(int i=0;i<1;i++){
//         if(gatts_if==ESP_GATT_IF_NONE || gatts_if==gl_profile_tab[i].gatts_if){
//             if(gl_profile_tab[i].gatts_cb)
//                 gl_profile_tab[i].gatts_cb(event,gatts_if,param);
//         }
//     }
// }

// // ---------------- Main ----------------
// void ble_v2(void){
//     ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
//     esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
//     ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
//     ESP_ERROR_CHECK(esp_bluedroid_init());
//     ESP_ERROR_CHECK(esp_bluedroid_enable());




//     // 设置固定 MTU=64
//     ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(64));

//     // 初始化队列和互斥锁
//     ble_send_queue = xQueueCreate(10, sizeof(ble_packet_t));
//     ble_mutex = xSemaphoreCreateMutex();

//     gl_profile_tab[PROFILE_APP_ID].gatts_cb = gatts_profile_event_handler;
//     ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
//     ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
//     ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_APP_ID));

//     // 创建定时器
//     esp_timer_create_args_t timer_args = {
//         .callback = &periodic_notify_cb,
//         .name = "timer_notify"
//     };
//     esp_timer_create(&timer_args, &timer_notify);

//     // 创建 BLE 发送任务
//     xTaskCreate(ble_send_task, "ble_send_task", 4096, NULL, 5, NULL);
// }

// // ---------------- 发送接口 ----------------
// void protocol_send_json(const char *json_str)
// {
//     if(!json_str) 
//     {
//         ESP_LOGW(TAG, "protocol_send_json: null string");
//         return;

//     }

//     struct gatts_profile_inst *profile = &gl_profile_tab[PROFILE_APP_ID];
//     if(profile->notify_enabled && profile->conn_id != 0xFFFF){
//         ble_packet_t pkt;
//         pkt.handle = profile->handles[IDX_CHAR_NOTIFY_VAL];
//         pkt.len = strlen(json_str);
//         if(pkt.len > sizeof(pkt.data)) pkt.len = sizeof(pkt.data);
//         memcpy(pkt.data, json_str, pkt.len);
//         xQueueSend(ble_send_queue, &pkt, 0);
//         ESP_LOGI(TAG, "Sent JSON len=%d queued", pkt.len);
//     } else {
//         ESP_LOGW(TAG, "BLE not connected or notify not enabled");
//     }
// }
