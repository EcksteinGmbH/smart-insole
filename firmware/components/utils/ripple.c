#include "ripple.h"
#include "utils.h"
#include "emath.h"

void rpl_init(Ripple_t *rpl, uint16_t wnd, float th,float th_direct, uint16_t keep_time)
{
//    rpl->window = 20;
//    rpl->threshold = 20;
//    rpl->keep = 100;
    rpl->window = wnd;
    rpl->threshold = th;
    rpl->threshold_direct = th_direct;
    rpl->keep = keep_time;
    rpl_reset(rpl);
}

void rpl_reset(Ripple_t *rpl)
{
    for(int i = 0; i < rpl->window; i++)
    {
        rpl->buf[i] = random_limit(0,1000);
    }
    rpl->state = 0;
    rpl->is_stable = 0;
    rpl->counter = 0;
}
float rpl_get(Ripple_t *rpl)
{

    return rpl->stdev;
}
float rpl_get_avg(Ripple_t *rpl)
{
    rpl->avg = avarage(rpl->buf,rpl->window);
    // Serial.printf("\r\navg\t%0.6f\r\n",avg);
    return rpl->avg;
}
void rpl_input(Ripple_t *rpl, float var)
{
    rpl->delta = var - rpl->var_last;
    rpl->var_last = var;
    rpl->buf[rpl->counter++] = var;
    rpl->counter %= rpl->window; 

    rpl->stdev = stdev(rpl->buf,rpl->window);
    float rp = rpl->stdev ;
//    rt_kprintf("sta:%d\t stdev:%0.3f\t stable:%d\n",rpl->state, rpl->stdev,rpl->is_stable);
    switch(rpl->state)
    {
        case 0:
            if(rp < rpl->threshold)
            {
                rpl->state = 1;
                rpl->last_stable_time = millis();
            }
            break;
        case 1:
            if(rp > rpl->threshold || fabsf(rpl->delta) > rpl->threshold_direct)
            {
                rpl->state = 0;
            }
            else if(millis() - rpl->last_stable_time > rpl->keep)
            {

                rpl->is_stable = 1;
                rpl->state = 2;
            }
            break;
        case 2:
            if(rp > rpl->threshold || fabsf(rpl->delta) > rpl->threshold_direct)
            {
                rpl->state = 0;
                rpl->is_stable = 0;
            }
            break;
        default:
            rpl->state = 0;break;
    }

}

uint8_t rpl_is_stable(Ripple_t *rpl)
{
    return rpl->is_stable;
};

