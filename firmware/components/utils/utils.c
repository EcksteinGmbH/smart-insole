#include <stdio.h>
#include "utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define NOP as an inline assembly macro
#define NOP() asm volatile ("nop")


unsigned long  micros() {
  return (unsigned long)(esp_timer_get_time());
}

unsigned long  millis() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

void delay(uint32_t ms) {
  vTaskDelay(ms / portTICK_PERIOD_MS);
}

void  delayMicroseconds(uint32_t us) {
  uint64_t m = (uint64_t)esp_timer_get_time();
  if (us) {
    uint64_t e = (m + us);
    if (m > e) {  //overflow
      while ((uint64_t)esp_timer_get_time() > e) {
        NOP();
      }
    }
    while ((uint64_t)esp_timer_get_time() < e) {
      NOP();
    }
  }
}




unsigned int random_limit(unsigned int min, unsigned int max)
{
    return (min + rand() % (max - min));
}

int limit_int(int v, int min,int max)
{
    if(v > max) v = max;
    else if(v < min) v = min;
    return v;
}

uint16_t limit_u16(uint16_t v, uint16_t min,uint16_t max)
{
    if(v > max) v = max;
    else if(v < min) v = min;
    return v;
}
uint32_t limit_u32(uint32_t v, uint32_t min,uint32_t max)
{
    if(v > max) v = max;
    else if(v < min) v = min;
    return v;
}
float limit(float v, float min,float max)
{
    if(v > max) v = max;
    else if(v < min) v = min;
    return v;
}
int limit_is_between(float v, float min,float max)
{
    if(v > max) return 1;
    else if(v < min) return -1;
    return 0;
}