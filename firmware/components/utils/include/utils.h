#ifndef __UTILS_H
#define __UTILS_H

#include <stdint.h>
#include <string.h>
#include "stdbool.h"

#define low_byte(w) ((uint8_t)((w) & 0xff))
#define high_byte(w) ((uint8_t)((w) >> 8))
/** read bit of value */
#define bit_read(value, bit) (((value) >> (bit)) & 0x01)
/** set bit to 1 of value */
#define bit_set(value, bit) ((value) |= (1UL << (bit)))
/** set bit to 0 of value */
#define bit_clear(value, bit) ((value) &= ~(1UL << (bit)))
/** write bit of value*/
#define bit_write(value, bit, bitvalue) (bitvalue ? bit_set(value, bit) : bit_clear(value, bit))
/** Bits m to n of x */
#define bits_get(x, m, n) ((((uint32_t)x) << (31 - (n))) >> ((31 - (n)) + (m)))
/** 1UL shifted left by 'shift' */
#define bit_shift(shift) (1UL << (shift))
/** 'Mask' shifted left by 'shift' */
#define bit_mask_shift(mask, shift) ((mask) << (shift))
/** True iff v is a power of two (1, 2, 4, 8, ...) */
#define is_pow_tow(v) ((v) && !((v) & ((v) - 1)))


#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))
#define bitsGet(x, m, n) ((((uint32)x) << (31 - (n))) >> ((31 - (n)) + (m)))
#define bitShift(shift) (1UL << (shift))
#define bitMaskShift(mask, shift) ((mask) << (shift))
#define isPow2(v) ((v) && !((v) & ((v) - 1)))
#define getArraySize(__ARR) (sizeof(__ARR) / sizeof(__ARR[0])) // 获取数组大小

unsigned long micros();
unsigned long millis();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);




unsigned int random_limit(unsigned int min, unsigned int max);
int limit_int(int v, int min,int max);
uint16_t limit_u16(uint16_t v, uint16_t min,uint16_t max);
uint32_t limit_u32(uint32_t v, uint32_t min,uint32_t max);
float limit(float v, float min,float max);
int limit_is_between(float v, float min,float max);

#endif // MOTOR_H
