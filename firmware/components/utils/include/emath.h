#ifndef __SYS_MATH_H
#define __SYS_MATH_H

#include "stdint.h"
#include "math.h"

typedef struct 
{
    float max;
    uint16_t max_index;
    float min;
    uint16_t min_index;
    float vpp;
}Vpp_t;
Vpp_t peak_to_peak(uint16_t *ptr, uint16_t size);

//����ĳ���������½�
uint16_t max_in_buf(uint16_t *ptr, uint16_t size); 
uint16_t min_in_buf(uint16_t *ptr, uint16_t size);
//uint16_t vpp(uint16_t *ptr, uint16_t size);

double avarage(float *ptr, uint16_t size);
float avarage_u16(uint16_t *ptr, uint16_t size);


//���� s= ��(x-u)^2/N;;;
double variance(float *ptr, uint16_t size);

//��׼��(������) o = sqrt(var);
double stdev(float *ptr, uint16_t size);

#endif


