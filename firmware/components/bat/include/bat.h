#ifndef __BAT_H__
#define __BAT_H__

#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdbool.h>
#include "filter_rc.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**

  * @brief 电池监测对象
    */
  typedef struct
  {
    gpio_num_t charger_pin; // 充电检测引脚
    adc_channel_t adc_ch;   // 电池电压检测通道
    float voltage;          // 电池电压（单位：V）
    float percent;          // 电池电量百分比（0.0 ~ 100.0）
    float p0_voltage;       // 电量 0% 时的电压
    float p100_voltage;     // 电量 100% 时的电压
    bool is_charging;       // 当前是否在充电
    FilterEWMA_t filter;     // 电压滤波对象
  } bat_t;

  /**

  * @brief 初始化电池检测模块（连续采样 + 曲线拟合校准）
    */
  void bat_init(bat_t *bat, gpio_num_t charger_pin, adc_channel_t adc_ch, float p0_voltage, float p100_voltage);

  /**

  * @brief 设置电压与电量百分比的映射范围
    */
  void bat_set_p0_p100(bat_t *bat, float p0_voltage, float p100_voltage);

  /**

  * @brief 更新电池电压、电量百分比和充电状态
    */
  void bat_update_voltage(bat_t *bat);

  /**

  * @brief 获取当前电池电压（单位：V）
    */
  float bat_get_voltage(bat_t *bat);

  /**

  * @brief 获取当前电池电量百分比
    */
  float bat_get_percent(bat_t *bat);

  /**

  * @brief 当前是否正在充电
    */
  bool bat_is_charging(bat_t *bat);

  /**

  * @brief 电池是否电量低
    */
  bool bat_is_lowpower(bat_t *bat);

  /**

  * @brief 电池是否已充满
    */
  bool bat_is_full(bat_t *bat);

  /**

  * @brief 释放资源
    */
  void bat_deinit(bat_t *bat);

#ifdef __cplusplus
}
#endif

#endif
