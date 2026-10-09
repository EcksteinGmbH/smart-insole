#ifndef __RIPPLE_H
#define __RIPPLE_H

#include "stdio.h"

#define RIPPLE_SAMPLE_MAX 128


typedef struct
{
    uint8_t window;
    float threshold;
    float threshold_direct;
    float var_last;
    float delta;
    uint16_t keep;

    
    float buf[RIPPLE_SAMPLE_MAX];
    float avg;
    float stdev;
    int state;
    uint32_t last_stable_time;
    uint8_t is_stable;
    uint32_t last_time;
    uint16_t counter;
}Ripple_t;


void rpl_init(Ripple_t *rpl, uint16_t wnd, float th,float th_direct, uint16_t keep_time);
void rpl_reset(Ripple_t *rpl);
float rpl_get(Ripple_t *rpl);
float rpl_get_avg(Ripple_t *rpl);
void rpl_input(Ripple_t *rpl, float var);
uint8_t rpl_is_stable(Ripple_t *rpl);


#endif


