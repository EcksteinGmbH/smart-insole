#ifndef DATA_CONVERT_H
#define DATA_CONVERT_H

#include <stdint.h>
#include <string.h>


typedef enum {
    ENDIAN_LITTLE   = 0,   // Little-Endian: 低字节在前 (0x12345678 → 78 56 34 12)
    ENDIAN_BIG      = 1,   // Big-Endian: 高字节在前 (0x12345678 → 12 34 56 78)
    ENDIAN_PDP      = 2,   // PDP-Endian: 16位交换 (0x12345678 → 34 12 78 56)
    ENDIAN_MIDDLE   = 3    // Middle-Endian: 32位按16位分组交换 (0x12345678 → 56 78 12 34)
} endian_t;

/* ================== 基础转换函数 ================== */

// 通用: 任意类型 → uint8_t数组
void to_bytes(const void *src, uint8_t *dst, size_t size, endian_t endian);

// 通用: uint8_t数组 → 任意类型
void from_bytes(const uint8_t *src, void *dst, size_t size, endian_t endian);

/* ================== 各种数据类型封装 ================== */

// uint16_t
void uint16_to_bytes(uint16_t value, uint8_t *dst, endian_t endian);
uint16_t bytes_to_uint16(const uint8_t *src, endian_t endian);

// int16_t
void int16_to_bytes(int16_t value, uint8_t *dst, endian_t endian);
int16_t bytes_to_int16(const uint8_t *src, endian_t endian);

// uint32_t
void uint32_to_bytes(uint32_t value, uint8_t *dst, endian_t endian);
uint32_t bytes_to_uint32(const uint8_t *src, endian_t endian);

// int32_t
void int32_to_bytes(int32_t value, uint8_t *dst, endian_t endian);
int32_t bytes_to_int32(const uint8_t *src, endian_t endian);

// uint64_t
void uint64_to_bytes(uint64_t value, uint8_t *dst, endian_t endian);
uint64_t bytes_to_uint64(const uint8_t *src, endian_t endian);

// int64_t
void int64_to_bytes(int64_t value, uint8_t *dst, endian_t endian);
int64_t bytes_to_int64(const uint8_t *src, endian_t endian);

// float
void float_to_bytes(float value, uint8_t *dst, endian_t endian);
float bytes_to_float(const uint8_t *src, endian_t endian);

// double
void double_to_bytes(double value, uint8_t *dst, endian_t endian);
double bytes_to_double(const uint8_t *src, endian_t endian);

#endif // DATA_CONVERT_H
