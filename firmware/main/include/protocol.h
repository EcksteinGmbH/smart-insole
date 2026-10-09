#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__


#include <stdint.h>
#include "cJSON.h"



// 接收 JSON 数据处理
int protocol_receive_json_data(const uint8_t *data, uint16_t len, char *resp_buf, int resp_buf_len);
// int protocol_handle_json(const char *data, char *buf, int len);
int protocol_make_imu_frame(uint8_t *buf, int max_len);

#endif // __PROTOCOL_H__