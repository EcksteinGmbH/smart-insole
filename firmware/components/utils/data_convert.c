#include "data_convert.h"

/* ================== 内部工具函数 ================== */
static void reverse_bytes(const uint8_t *src, uint8_t *dst, size_t size) {
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[size - 1 - i];
    }
}

static void swap16_bytes(const uint8_t *src, uint8_t *dst, size_t size) {
    for (size_t i = 0; i < size; i += 2) {
        if (i + 1 < size) {
            dst[i]   = src[i + 1];
            dst[i+1] = src[i];
        }
    }
}

static void swap32_middle_bytes(const uint8_t *src, uint8_t *dst, size_t size) {
    // 按 16-bit 半字交换
    for (size_t i = 0; i < size; i += 4) {
        if (i + 3 < size) {
            dst[i]   = src[i+2];
            dst[i+1] = src[i+3];
            dst[i+2] = src[i];
            dst[i+3] = src[i+1];
        }
    }
}

/* ================== 基础转换函数实现 ================== */

void to_bytes(const void *src, uint8_t *dst, size_t size, endian_t endian)
{
    const uint8_t *p = (const uint8_t*)src;
    switch (endian) {
        case ENDIAN_LITTLE:
            memcpy(dst, p, size);
            break;
        case ENDIAN_BIG:
            reverse_bytes(p, dst, size);
            break;
        case ENDIAN_PDP:
            swap16_bytes(p, dst, size);
            break;
        case ENDIAN_MIDDLE:
            swap32_middle_bytes(p, dst, size);
            break;
        default:
            memcpy(dst, p, size);
            break;
    }
}

void from_bytes(const uint8_t *src, void *dst, size_t size, endian_t endian)
{
    uint8_t *p = (uint8_t*)dst;
    switch (endian) {
        case ENDIAN_LITTLE:
            memcpy(p, src, size);
            break;
        case ENDIAN_BIG:
            reverse_bytes(src, p, size);
            break;
        case ENDIAN_PDP:
            swap16_bytes(src, p, size);
            break;
        case ENDIAN_MIDDLE:
            swap32_middle_bytes(src, p, size);
            break;
        default:
            memcpy(p, src, size);
            break;
    }
}

/* ================== 各种数据类型封装实现 ================== */

void uint16_to_bytes(uint16_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
uint16_t bytes_to_uint16(const uint8_t *src, endian_t endian) {
    uint16_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void int16_to_bytes(int16_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
int16_t bytes_to_int16(const uint8_t *src, endian_t endian) {
    int16_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void uint32_to_bytes(uint32_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
uint32_t bytes_to_uint32(const uint8_t *src, endian_t endian) {
    uint32_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void int32_to_bytes(int32_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
int32_t bytes_to_int32(const uint8_t *src, endian_t endian) {
    int32_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void uint64_to_bytes(uint64_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
uint64_t bytes_to_uint64(const uint8_t *src, endian_t endian) {
    uint64_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void int64_to_bytes(int64_t value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
int64_t bytes_to_int64(const uint8_t *src, endian_t endian) {
    int64_t value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void float_to_bytes(float value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
float bytes_to_float(const uint8_t *src, endian_t endian) {
    float value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}

void double_to_bytes(double value, uint8_t *dst, endian_t endian) {
    to_bytes(&value, dst, sizeof(value), endian);
}
double bytes_to_double(const uint8_t *src, endian_t endian) {
    double value;
    from_bytes(src, &value, sizeof(value), endian);
    return value;
}
