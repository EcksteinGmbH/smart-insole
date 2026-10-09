#include "protocol.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "data_convert.h"
#include "app_lsm6d.h"
#include "app_motorx.h"
#include "app_bat.h"

#include "esp_log.h"
static const char *TAG = "pro";

// ===== JSON帧组装器定义 =====
// JSON帧组装器结构体
typedef struct {
    char buffer[384];       // 组装缓冲区，大小可根据实际需求调整
    uint16_t current_len;    // 当前已接收的数据长度
    int brace_count;         // 花括号计数器，用于检测JSON完整性
    uint32_t last_receive_time; // 最后接收数据的时间戳，用于超时处理
    bool is_assembling;      // 是否正在组装一帧数据
} json_frame_assembler_t;
// 创建全局的JSON帧组装器实例
static json_frame_assembler_t json_assembler = {
    .buffer = {0},
    .current_len = 0,
    .brace_count = 0,
    .last_receive_time = 0,
    .is_assembling = false
};

// ===== IMU 设置 =====

static int handle_motor_set(cJSON *root, char *buf, int len);
static int handle_motor_get(cJSON *root, char *buf, int len);
static int handle_imu_set(cJSON *root, char *buf, int len);
static int handle_imu_get(cJSON *root, char *buf, int len);
static int handle_info_get(cJSON *root, char *buf, int len);
int protocol_handle_json(const char *data, char *buf, int len);

// 字段值按 API 文档为字符串；同时容忍数字类型，避免 valuestring 为 NULL 时崩溃
static unsigned long json_to_ul(const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        return strtoul(item->valuestring, NULL, 10);
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble > 0 ? (unsigned long)item->valuedouble : 0;
    }
    return 0;
}
static float json_to_f(const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        return strtof(item->valuestring, NULL);
    }
    if (cJSON_IsNumber(item)) {
        return (float)item->valuedouble;
    }
    return 0.0f;
}


// 重置JSON帧组装器
static void reset_json_assembler(void) {
    memset(json_assembler.buffer, 0, sizeof(json_assembler.buffer));
    json_assembler.current_len = 0;
    json_assembler.brace_count = 0;
    json_assembler.is_assembling = false;
    // 注意：不重置last_receive_time，因为它会在每次接收数据时更新
}
// 检查是否接收到完整的JSON帧
static bool is_json_frame_complete(const char *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] == '{') {
            json_assembler.brace_count++;
            json_assembler.is_assembling = true;
        } else if (data[i] == '}') {
            json_assembler.brace_count--;
            // 如果花括号计数为0，并且正在组装数据，则认为帧完整
            if (json_assembler.brace_count == 0 && json_assembler.is_assembling) {
                return true;
            }
        }
    }
    return false;
}
// 新增：JSON帧组装接口函数，供main.c调用
int protocol_receive_json_data(const uint8_t *data, uint16_t len, char *resp_buf, int resp_buf_len) {
    // 首先检查是否超时（如果当前正在组装数据，且距离上次接收超过1秒）
    if (json_assembler.is_assembling && 
        (xTaskGetTickCount() - json_assembler.last_receive_time) > pdMS_TO_TICKS(1000)) {
        ESP_LOGW(TAG, "JSON frame assembly timeout, resetting");
        reset_json_assembler();
    }
    
    // 更新最后接收时间
    json_assembler.last_receive_time = xTaskGetTickCount();
    
    // 标记为正在组装
    if (!json_assembler.is_assembling) {
        json_assembler.is_assembling = true;
    }
    
    // 检查缓冲区是否足够
    if (json_assembler.current_len + len >= sizeof(json_assembler.buffer)) {
        ESP_LOGW(TAG, "JSON assembler buffer overflow, resetting");
        reset_json_assembler();
        return -1;
    }
    
    // 将新数据添加到组装缓冲区
    memcpy(json_assembler.buffer + json_assembler.current_len, data, len);
    json_assembler.current_len += len;
    
    // 检查是否接收到完整的JSON帧
    if (is_json_frame_complete((const char *)json_assembler.buffer, json_assembler.current_len)) {
        // ESP_LOGI(TAG, "Received complete JSON frame, length: %u", json_assembler.current_len);
        
        // 处理完整的JSON帧
        int resp_len = protocol_handle_json(json_assembler.buffer, resp_buf, resp_buf_len);
        
        // 重置组装器，准备接收下一帧
        reset_json_assembler();
        
        return resp_len;
    } else {
        // 帧不完整，继续等待更多数据
        return 0;
    }
}
// ==== JSON 指令解析层 ====

int protocol_handle_json(const char *data, char *buf, int len)
{
    // ESP_LOGI(TAG, "Received JSON: %s", data);

    // 初始化输出缓冲区（如果提供）
    if(buf && len > 0){
        buf[0] = '\0';
    }

    cJSON *root = cJSON_Parse(data);
    if(!root){
        ESP_LOGW(TAG, "JSON parse error: [%d][%s]", strlen(data), data);
        return -1;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if(!cmd || !cJSON_IsString(cmd)){
        ESP_LOGW(TAG, "No valid cmd field");
        cJSON_Delete(root);
        return -2;
    }

    // ESP_LOGI(TAG, "Processing cmd: %s", cmd->valuestring);

    int resp_len = 0;
    
    // 根据命令类型调用相应的处理函数
    if(strcmp(cmd->valuestring, "motor_set")==0){
        resp_len = handle_motor_set(root, buf, len);
    } else if(strcmp(cmd->valuestring, "motor_get")==0){
        resp_len = handle_motor_get(root, buf, len);
    } else if(strcmp(cmd->valuestring, "imu_set")==0){
        resp_len = handle_imu_set(root, buf, len);
    } else if(strcmp(cmd->valuestring, "imu_get")==0){
        resp_len = handle_imu_get(root, buf, len);
    } else if(strcmp(cmd->valuestring, "info_get")==0){
        resp_len = handle_info_get(root, buf, len);
    } else {
        ESP_LOGW(TAG, "Unknown cmd: %s", cmd->valuestring);
        resp_len = -3;
    }

    // 如果有响应，记录日志
    // if(buf && len > 0 && buf[0] != '\0'){
    //     ESP_LOGI(TAG, "Response: %s", buf);
    // }

    cJSON_Delete(root);
    return resp_len;
}
int protocol_make_imu_frame(uint8_t *buf, int max_len)
{
    int idx = 0;

    // 起始标志
    buf[idx++] = 0x55;
    buf[idx++] = 0xAA;

    // 数据长度 = 两个 IMU 数据 + CRC
    // uint8_t len = 1;
    // if (imu_data[0].enable) len += 12;
    // if (imu_data[1].enable) len += 12;
    // buf[idx++] = len;
    // // 两个IMU数据
    // for (int i = 0; i < 2; i++) {
    //     if (imu_data[i].enable) {
    //         int16_to_bytes(imu_data[i].ax_g_x1000,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //         int16_to_bytes(imu_data[i].ay_g_x1000,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //         int16_to_bytes(imu_data[i].az_g_x1000,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //         int16_to_bytes(imu_data[i].gx_dps_x10,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //         int16_to_bytes(imu_data[i].gy_dps_x10,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //         int16_to_bytes(imu_data[i].gz_dps_x10,&buf[idx],ENDIAN_LITTLE);idx+=2;
    //     }
    // }
    uint8_t len = 1;
    if (imu[0].enable) len += 24;
    if (imu[1].enable) len += 24;
    buf[idx++] = len;
    // 两个IMU数据
    for (int i = 0; i < 2; i++) {
        if (imu[i].enable) {
            float_to_bytes(imu[i].ax_mg,&buf[idx],ENDIAN_LITTLE);idx+=4;
            float_to_bytes(imu[i].ay_mg,&buf[idx],ENDIAN_LITTLE);idx+=4;
            float_to_bytes(imu[i].az_mg,&buf[idx],ENDIAN_LITTLE);idx+=4;
            float_to_bytes(imu[i].gx_mdps,&buf[idx],ENDIAN_LITTLE);idx+=4;
            float_to_bytes(imu[i].gy_mdps,&buf[idx],ENDIAN_LITTLE);idx+=4;
            float_to_bytes(imu[i].gz_mdps,&buf[idx],ENDIAN_LITTLE);idx+=4;
        }
    }
    // 校验（简单和校验 mod 256）
    uint8_t crc = 0;
    for (int i = 2; i < idx; i++) { // 从 LEN 开始累加
        crc += buf[i];
    }
    buf[idx++] = crc;
    return idx;
}








// ==== 命令处理函数 ====

// 处理电机设置命令
static int handle_motor_set(cJSON *root, char *buf, int len)
{
    int ret = 0;
    char strbuf[32];

    cJSON *id_obj = cJSON_GetObjectItem(root, "id");
    cJSON *freq = cJSON_GetObjectItem(root, "freq");
    cJSON *duty = cJSON_GetObjectItem(root, "duty");
    cJSON *times = cJSON_GetObjectItem(root, "times");
    cJSON *intensity = cJSON_GetObjectItem(root, "intensity");


    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp,"cmd","motor_set");

    motorx_params_t motorx_params;
    motorx_t *mx = NULL;
    uint8_t id = 0;


    if(id_obj)
    {
        id = (uint8_t)json_to_ul(id_obj);
        snprintf(strbuf,32, "%u", id);
        cJSON_AddStringToObject(resp,"id",strbuf);
        if(id > 3)
        {
            ret = -1;
            cJSON_AddStringToObject(resp,"err","-1");
            cJSON_AddStringToObject(resp,"msg","id > 3");
            goto exit;
        }
    }
    else
    {
        ret = -2;
        cJSON_AddStringToObject(resp,"err","-2");
        cJSON_AddStringToObject(resp,"msg","no id");
        goto exit;
    }


    if(freq && duty && times)
    {
        id = (uint8_t)json_to_ul(id_obj);
        motorx_params.times = (uint16_t)json_to_ul(times);
        motorx_params.frequency = (float)json_to_f(freq);
        motorx_params.duty = (float)json_to_f(duty);
        motorx_params.intensity = 800;
    }
    else
    {
        ret = -3;
        cJSON_AddStringToObject(resp,"err","-3");
        cJSON_AddStringToObject(resp,"msg","params missing");
        goto exit;
    }
    if (intensity)
    {
        motorx_params.intensity = (uint16_t)json_to_ul(intensity);
    }
    // ESP_LOGI(TAG,"[%s][%s][%s][%s][%s]",
    //     id->valuestring,freq->valuestring,duty->valuestring,times->valuestring,intensity->valuestring);

    // ESP_LOGI(TAG, "id=%u", motor_id);
    // ESP_LOGI(TAG, "intensity=%u", motorx_params.intensity);
    // ESP_LOGI(TAG, "times=%u", motorx_params.times);
    // ESP_LOGI(TAG, "frequency=%.3f", motorx_params.frequency);
    // ESP_LOGI(TAG, "duty=%.3f", motorx_params.duty);

    

    if(id == 3)
    {
        motorx_start(&motorx[0],motorx_params);
        motorx_start(&motorx[1],motorx_params);
        motorx_start(&motorx[2],motorx_params);
        mx = &motorx[0];

    }
    else if(id <= 2)
    {
        mx = &motorx[id];
        motorx_start(mx,motorx_params);
    }
    // ESP_LOGI(TAG, "motor_set: motor=%d freq=%.2f duty=%.2f times=%u intensity=%u", 
    //          id, motorx_params.frequency, motorx_params.duty, (int)motorx_params.times, motorx_params.intensity);


    sprintf(strbuf, "%.3f", mx->params.frequency);
    cJSON_AddStringToObject(resp,"freq",strbuf);
    sprintf(strbuf, "%.3f", mx->params.duty);
    cJSON_AddStringToObject(resp,"duty",strbuf);
    sprintf(strbuf, "%u", mx->params.times);
    cJSON_AddStringToObject(resp,"times",strbuf);
    sprintf(strbuf, "%u", mx->params.intensity);
    cJSON_AddStringToObject(resp,"intensity",strbuf);
    sprintf(strbuf, "%u", mx->running);
    cJSON_AddStringToObject(resp,"run",strbuf);
    sprintf(strbuf, "%u", mx->count);
    cJSON_AddStringToObject(resp,"count",strbuf);

    cJSON_AddStringToObject(resp,"err","0");
    cJSON_AddStringToObject(resp,"msg","ok");
        
    exit:


    char *str = cJSON_PrintUnformatted(resp);
    int resp_len = strlen(str);
    
    if(buf && len > 0){
        if(resp_len < len){
            strcpy(buf, str);
        } else {
            // 缓冲区不足，截断字符串
            strncpy(buf, str, len-1);
            buf[len-1] = '\0';
        }
    }
    
    free(str);
    cJSON_Delete(resp);
    return resp_len;
}

// 处理电机获取命令
static int handle_motor_get(cJSON *root, char *buf, int len)
{
    int ret = 0;
    motorx_t *mx = NULL;
    char strbuf[32];
    uint8_t id;

    cJSON *id_obj = cJSON_GetObjectItem(root, "id");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp,"cmd","motor_get");

    if(id_obj)
    {
        id = (uint8_t)json_to_ul(id_obj);
        snprintf(strbuf,32, "%u", id);
        cJSON_AddStringToObject(resp,"id",strbuf);
        if(id >= 3)
        {
            ret = -1;
            cJSON_AddStringToObject(resp,"err","-1");
            cJSON_AddStringToObject(resp,"msg","id >= 3");
            goto exit;
        }
        else
        {
            mx = &motorx[id];
        }
    }
    else
    {
        ret = -2;
        cJSON_AddStringToObject(resp,"err","-2");
        cJSON_AddStringToObject(resp,"msg","no id");
        goto exit;
    }

    sprintf(strbuf, "%.3f", mx->params.frequency);
    cJSON_AddStringToObject(resp,"freq",strbuf);
    sprintf(strbuf, "%.3f", mx->params.duty);
    cJSON_AddStringToObject(resp,"duty",strbuf);
    sprintf(strbuf, "%u", mx->params.times);
    cJSON_AddStringToObject(resp,"times",strbuf);
    sprintf(strbuf, "%u", mx->params.intensity);
    cJSON_AddStringToObject(resp,"intensity",strbuf);
    sprintf(strbuf, "%u", mx->running);
    cJSON_AddStringToObject(resp,"run",strbuf);
    sprintf(strbuf, "%u", mx->count);
    cJSON_AddStringToObject(resp,"count",strbuf);

    cJSON_AddStringToObject(resp,"err","0");
    cJSON_AddStringToObject(resp,"msg","ok");
    

    exit:

    char *str = cJSON_PrintUnformatted(resp);
    int resp_len = strlen(str);
    
    if(buf && len > 0){
        if(resp_len < len){
            strcpy(buf, str);
        } else {
            // 缓冲区不足，截断字符串
            strncpy(buf, str, len-1);
            buf[len-1] = '\0';
        }
    }

    free(str);
    cJSON_Delete(resp);
    return resp_len;
}

// 处理IMU设置命令
static int handle_imu_set(cJSON *root, char *buf, int len)
{
    int ret = 0;
    char strbuf[32];


    cJSON *id_obj = cJSON_GetObjectItem(root, "id");
    cJSON *enable_obj = cJSON_GetObjectItem(root, "enable");
    // 字段名以《智能鞋垫API文档V1.0.0》为准；旧字段名 acc_wake_thresh / acc_sleep_after 仍兼容
    cJSON *acc_sleep_thresh_obj = cJSON_GetObjectItem(root, "acc_sleep_thresh");
    if (!acc_sleep_thresh_obj) {
        acc_sleep_thresh_obj = cJSON_GetObjectItem(root, "acc_wake_thresh");
    }
    cJSON *acc_sleep_after_obj = cJSON_GetObjectItem(root, "acc_sleep_after_s");
    if (!acc_sleep_after_obj) {
        acc_sleep_after_obj = cJSON_GetObjectItem(root, "acc_sleep_after");
    }




    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp,"cmd","imu_set");


    uint8_t id = 0;
    uint16_t acc_sleep_thresh_mg = 0;   
    uint32_t acc_sleep_after_s = 0;
    bool enable = false;
    if(id_obj)
    {
        id = (uint8_t)json_to_ul(id_obj);
        snprintf(strbuf,32, "%u", id);
        cJSON_AddStringToObject(resp,"id",strbuf);
        if(id > 2)
        {
            ret = -1;
            cJSON_AddStringToObject(resp,"err","-1");
            cJSON_AddStringToObject(resp,"msg","id > 2");
            goto exit;
        }
    }
    else
    {
        ret = -2;
        cJSON_AddStringToObject(resp,"err","-2");
        cJSON_AddStringToObject(resp,"msg","no id");
        goto exit;
    }

    if(acc_sleep_thresh_obj)
    {
        acc_sleep_thresh_mg = (uint16_t)(json_to_ul(acc_sleep_thresh_obj));
        if(id == 2)
        {
            app_lsm6d_acc_sleep_thresh_set(&imu[0], acc_sleep_thresh_mg);
            app_lsm6d_acc_sleep_thresh_set(&imu[1], acc_sleep_thresh_mg);
        }
        else
        {
            app_lsm6d_acc_sleep_thresh_set(&imu[id], acc_sleep_thresh_mg);
        }
        // 返回量化到寄存器步进后的实际生效值
        snprintf(strbuf,32, "%lu", app_lsm6d_acc_sleep_thresh_get(&imu[id == 2 ? 0 : id]));
        cJSON_AddStringToObject(resp,"acc_sleep_thresh",strbuf);

        // motor_id = (uint8_t)strtoul(id->valuestring,NULL,10);
        // motorx_params.times = (uint16_t)strtoul(times->valuestring, NULL, 10);;
        // motorx_params.frequency = (float)strtof(freq->valuestring, NULL);;
        // motorx_params.duty = (float)strtof(duty->valuestring, NULL);;
        // motorx_params.intensity = 800;
    }
    if(acc_sleep_after_obj)
    {
        acc_sleep_after_s = (uint32_t)(json_to_ul(acc_sleep_after_obj));
        if(id == 2)
        {
            app_lsm6d_acc_sleep_after_set(&imu[0], acc_sleep_after_s);
            app_lsm6d_acc_sleep_after_set(&imu[1], acc_sleep_after_s);
        }
        else
        {
            app_lsm6d_acc_sleep_after_set(&imu[id], acc_sleep_after_s);
        }
        snprintf(strbuf,32, "%lu", acc_sleep_after_s);
        cJSON_AddStringToObject(resp,"acc_sleep_after_s",strbuf);
    }



    if (enable_obj)
    {
        enable = json_to_ul(enable_obj);
        if(id == 2)
        {
            app_lsm6d_enable_set(&imu[0], enable);
            app_lsm6d_enable_set(&imu[1], enable);
        }
        else
        {
            app_lsm6d_enable_set(&imu[id], enable);
        }
        snprintf(strbuf,32, "%u", enable);
        cJSON_AddStringToObject(resp,"enable",strbuf);
    }
    cJSON_AddStringToObject(resp,"err","0");
    cJSON_AddStringToObject(resp,"msg","ok");

    exit:

    // ESP_LOGI(TAG, "imu_set: freq=%d acc_wake_thresh=%.2f", imu_freq, imu_acc_wake_thresh);


    
    char *str = cJSON_PrintUnformatted(resp);
    int resp_len = strlen(str);
    
    if(buf && len > 0){
        if(resp_len < len){
            strcpy(buf, str);
        } else {
            // 缓冲区不足，截断字符串
            strncpy(buf, str, len-1);
            buf[len-1] = '\0';
        }
    }
    
    free(str);
    cJSON_Delete(resp);
    return resp_len;
}

// 处理IMU获取命令
static int handle_imu_get(cJSON *root, char *buf, int len)
{
    int ret = 0;
    motorx_t *mx = NULL;
    char strbuf[32];
    imu_t *imu_ptr = NULL;

    cJSON *id_obj = cJSON_GetObjectItem(root, "id");
    
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp,"cmd","imu_get");




    bool enable = false;
    if(id_obj)
    {
        uint8_t id = 0;
        id = (uint8_t)json_to_ul(id_obj);
        snprintf(strbuf,32, "%u", id);
        cJSON_AddStringToObject(resp,"id",strbuf);
        if(id >= 2)
        {
            ret = -1;
            cJSON_AddStringToObject(resp,"err","-1");
            cJSON_AddStringToObject(resp,"msg","id >= 2");
            goto exit;
        }
        else
        {
            imu_ptr = &imu[id];
        }
    }
    else
    {
        ret = -2;
        cJSON_AddStringToObject(resp,"err","-2");
        cJSON_AddStringToObject(resp,"msg","no id");
        goto exit;
    }
    snprintf(strbuf,32, "%u", imu_ptr->enable);
    cJSON_AddStringToObject(resp,"enable",strbuf);
    snprintf(strbuf,32, "%4.2f", imu_ptr->freq[0]);
    cJSON_AddStringToObject(resp,"freq",strbuf);
    snprintf(strbuf,32, "%lu", imu_ptr->acc_sleep_thresh_mg);
    cJSON_AddStringToObject(resp,"acc_sleep_thresh",strbuf);
    snprintf(strbuf,32, "%lu", imu_ptr->acc_sleep_after_s);
    cJSON_AddStringToObject(resp,"acc_sleep_after_s",strbuf);
    cJSON_AddStringToObject(resp,"err","0");
    cJSON_AddStringToObject(resp,"msg","ok");

    exit:
    // ESP_LOGI(TAG, "imu_get: freq=%d acc_wake_thresh=%.2f status=running", imu_freq, imu_acc_wake_thresh);

    char *str = cJSON_PrintUnformatted(resp);
    int resp_len = strlen(str);
    
    if(buf && len > 0){
        if(resp_len < len){
            strcpy(buf, str);
        } else {
            // 缓冲区不足，截断字符串
            strncpy(buf, str, len-1);
            buf[len-1] = '\0';
        }
    }
    
    free(str);
    cJSON_Delete(resp);
    return resp_len;
}

// 处理info获取命令
static int handle_info_get(cJSON *root, char *buf, int len)
{
    char strbuf[16];
    cJSON *resp = cJSON_CreateObject();
    // 电池电压

    cJSON_AddStringToObject(resp,"cmd","info_get");

    snprintf(strbuf, sizeof(strbuf), "%.1f%%", bat.percent);
    cJSON_AddStringToObject(resp,"bat",strbuf);
       
    snprintf(strbuf, sizeof(strbuf), "%.3f", bat.voltage);
    cJSON_AddStringToObject(resp,"voltage",strbuf);

    cJSON_AddStringToObject(resp,"charging",bat.is_charging?"yes":"no");

    cJSON_AddStringToObject(resp,"err","0");
    cJSON_AddStringToObject(resp,"msg","ok");


    // snprintf(strbuf, sizeof(strbuf), "%lu", esp_get_free_heap_size());
    // cJSON_AddStringToObject(resp,"free",strbuf);

    char *str = cJSON_PrintUnformatted(resp);
    int resp_len = strlen(str);
    
    if(buf && len > 0){
        if(resp_len < len){
            strcpy(buf, str);
        } else {
            // 缓冲区不足，截断字符串
            strncpy(buf, str, len-1);
            buf[len-1] = '\0';
        }
    }
    
    free(str);
    cJSON_Delete(resp);
    return resp_len;
}
