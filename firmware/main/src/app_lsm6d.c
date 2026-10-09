#include "app_lsm6d.h"
#include "esp_log.h"
#include "lsm6d.h"
#include "utils.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "i2c.h"
#include <string.h>
#include "esp_task.h"

#include "bsp.h"
#include "app_bat.h"
#include "app_wakeup.h"

static const char *TAG = "imu";

/* Private macro -------------------------------------------------------------*/

#define BOOT_TIME 15 // ms

/*
 *   WARNING:
 *   Functions declare in this section are defined at the end of this file
 *   and are strictly related to the hardware platform used.
 *
 */
static int32_t platform_write_6a(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
static int32_t platform_write_6b(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
static int32_t platform_read_6a(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);
static int32_t platform_read_6b(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);
static void tx_com(uint8_t *tx_buffer, uint16_t len);
static void platform_delay(uint32_t ms);
static void platform_init(void);

#define BOOT_TIME 15 // ms
#define TX_BUF_DIM 1000

/* Private variables ---------------------------------------------------------*/

static uint8_t whoamI, rst;

stmdev_ctx_t imu_ctx[2] = {0};
imu_t imu[2] = {0};

#define TARGET 1

void handler_imu_inactive(void *imu__, bool is_inactive)
{
  imu_t *imu_ = (imu_t *)imu__;
  ESP_LOGI(TAG, "[IMU%u] inactive: %d", imu_->id, is_inactive);
}



/* Main Example --------------------------------------------------------------*/
void lsm6d_init(imu_t *imu,int int1_pin,int int2_pin, stmdev_ctx_t *ctx, uint8_t id)
{
  ESP_LOGI(TAG, "LSM6DS3TR-C init (ID=%d)", id);
  /* Initialize mems driver interface */
  if (id == 0)
  {
    ctx->write_reg = platform_write_6a;
    ctx->read_reg = platform_read_6a;
    imu->ctx = ctx;
    imu->int1_pin = int1_pin;
    imu->int2_pin = int2_pin;
    imu->id = id;
    rpl_init(&imu->rpl_mg, 20, 7.0f, 0xffffffff, 10000);
    rpl_init(&imu->rpl_mdps, 300, 7.0f, 0xffffffff, 10000);
  }
  else if (id == 1)
  {
    ctx->write_reg = platform_write_6b;
    ctx->read_reg = platform_read_6b;
    imu->ctx = ctx;
    imu->int1_pin = int1_pin;
    imu->int2_pin = int2_pin;
    imu->id = id;
    rpl_init(&imu->rpl_mg, 20, 7.0f, 0xffffffff, 1000);
    rpl_init(&imu->rpl_mdps, 300, 7.0f, 0xffffffff, 1000);
  }
  else
  {
    ESP_LOGI(TAG, "Invalid IMU ID");
    return;
  }
  imu->acc_sleep_thresh_mg = 150;
  imu->acc_sleep_after_s = 120;

  ctx->mdelay = platform_delay;
  ctx->handle = NULL;
  /* Init test platform */
  platform_init();
  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);
  /* Check device ID */
  whoamI = 0;
  lsm6ds3tr_c_device_id_get(ctx, &whoamI);
  ESP_LOGI(TAG, "WHO: 0x%02x", whoamI);

  if (whoamI != LSM6DS3TR_C_ID)
  {
    ESP_LOGE(TAG, "LSM6DS3TR-C not found");
    return;
  }
  /* Restore default configuration */
  lsm6ds3tr_c_reset_set(ctx, PROPERTY_ENABLE);

  do
  {
    lsm6ds3tr_c_reset_get(ctx, &rst);
    platform_delay(10);
  } while (rst);

  imu->enable = true;
  imu->initialized = true;
}

// 应用1：中断读取
// 自动休眠模式
void app_lsm6d_mode1_init(imu_t *imu)
{
  stmdev_ctx_t *ctx = imu->ctx;
  uint8_t val;

  /* Enable Block Data Update */
  lsm6ds3tr_c_block_data_update_set(ctx, PROPERTY_ENABLE);
  /* Set Output Data Rate */
  lsm6ds3tr_c_xl_data_rate_set(ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  lsm6ds3tr_c_gy_data_rate_set(ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  /* Set full scale */
  lsm6ds3tr_c_xl_full_scale_set(ctx, LSM6DS3TR_C_4g);
  lsm6ds3tr_c_gy_full_scale_set(ctx, LSM6DS3TR_C_500dps);

  // 使能INT1引脚
  // lsm6ds3tr_c_int1_route_t int1_route = {0};
  // int1_route.int1_drdy_xl = PROPERTY_ENABLE;
  // lsm6ds3tr_c_pin_int1_route_set(ctx, int1_route);
  // 设置唤醒阈值和时长
  lsm6ds3tr_c_act_sleep_dur_set(ctx, 0x01);
  // 设置唤醒阈值，单位为20mg，0x05=100mg
  lsm6ds3tr_c_wkup_threshold_set(ctx, 0x05);
  // 进入自动休眠模式
  lsm6ds3tr_c_act_mode_set(ctx, LSM6DS3TR_C_XL_12Hz5_GY_PD);
  // 使能INT2引脚.IMU0的INT2引脚用于自动休眠模式，作为唤醒信号
  lsm6ds3tr_c_int2_route_t int2_route = {0};
  int2_route.int2_inact_state = PROPERTY_ENABLE;
  lsm6ds3tr_c_pin_int2_route_set(ctx, int2_route);

  // 重新设置 自动休眠阈值为100mg
  app_lsm6d_acc_sleep_thresh_set(imu, 150);

  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_INT1_CTRL, (uint8_t *)&val, 1);
  ESP_LOGD(TAG, "INT1_CTRL: 0X%02X", val);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_DUR, (uint8_t *)&val, 1);
  ESP_LOGD(TAG, "WAKE_UP_DUR: 0X%02X", val);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_THS, (uint8_t *)&val, 1);
  ESP_LOGD(TAG, "WAKE_UP_THS: 0X%02X", val);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_TAP_CFG, (uint8_t *)&val, 1);
  ESP_LOGD(TAG, "TAP_CFG: 0X%02X", val);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_MD2_CFG, (uint8_t *)&val, 1);
  ESP_LOGD(TAG, "MD2_CFG: 0X%02X", val);
}
void lsm6d_loop(stmdev_ctx_t *ctx, imu_t *imu)
{
  if (!imu->initialized)
  {
    ESP_LOGW(TAG, "LSM6DS3TR-C not initialized");
    return;
  }
  /* Read output only if new value is available */
  lsm6ds3tr_c_reg_t reg;
  lsm6ds3tr_c_status_reg_get(ctx, &reg.status_reg);
  int16_t data_raw_acceleration[3];
  int16_t data_raw_angular_rate[3];
  int16_t data_raw_temperature;
  if (reg.status_reg.xlda)
  {
    /* Read acceleration data */
    memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_acceleration_raw_get(ctx, data_raw_acceleration);
    imu->ax_mg = lsm6ds3tr_c_from_fs4g_to_mg(data_raw_acceleration[0]);
    imu->ay_mg = lsm6ds3tr_c_from_fs4g_to_mg(data_raw_acceleration[1]);
    imu->az_mg = lsm6ds3tr_c_from_fs4g_to_mg(data_raw_acceleration[2]);
    imu->ax_g_x1000 = (int16_t)(imu->ax_mg);
    imu->ay_g_x1000 = (int16_t)(imu->ay_mg);
    imu->az_g_x1000 = (int16_t)(imu->az_mg);
    imu->_counter[0]++;

    float mg = imu->ax_mg + imu->ay_mg + imu->az_mg;
    rpl_input(&imu->rpl_mg, mg);
    // snprintf((char *)tx_buffer, sizeof(tx_buffer),"[%d][mg]:%4.2f|%d\t%4.2f|%d\t%4.2f|%d\t %4.2f",
    //         imu->id, imu->ax_mg, imu->ax_g_x1000, imu->ay_mg, imu->ay_g_x1000, imu->az_mg, imu->az_g_x1000, imu->freq[0]);
    // tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
  }
  if (reg.status_reg.gda)
  {
    /* Read magnetic field data */
    memset(data_raw_angular_rate, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_angular_rate_raw_get(ctx, data_raw_angular_rate);
    imu->gx_mdps = lsm6ds3tr_c_from_fs500dps_to_mdps(data_raw_angular_rate[0]);
    imu->gy_mdps = lsm6ds3tr_c_from_fs500dps_to_mdps(data_raw_angular_rate[1]);
    imu->gz_mdps = lsm6ds3tr_c_from_fs500dps_to_mdps(data_raw_angular_rate[2]);
    imu->gy_dps_x10 = (int16_t)(imu->gy_mdps / 100.0);
    imu->gz_dps_x10 = (int16_t)(imu->gz_mdps / 100.0);
    imu->_counter[1]++;

    // imu->freq[1] = 1000.0f / (millis() - imu->_lastimestamp[1]);
    // imu->_lastimestamp[1] = millis();

    float mdps = imu->gx_mdps + imu->gy_mdps + imu->gz_mdps;
    rpl_input(&imu->rpl_mdps, mdps);

    // snprintf((char *)tx_buffer, sizeof(tx_buffer),
    //         "[%d][mdps]:%4.2f|%d\t%4.2f|%d\t%4.2f|%d\t %4.2f",
    //         imu_data->id, imu_data->gx_mdps, imu_data->gx_dps_x10, imu_data->gy_mdps, imu_data->gy_dps_x10, imu_data->gz_mdps,imu_data->gz_dps_x10, imu_data->freq[1]);
    // tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
  }

  // if (reg.status_reg.tda)
  // {
  //   /* Read temperature data */
  //   memset(&data_raw_temperature, 0x00, sizeof(int16_t));
  //   lsm6ds3tr_c_temperature_raw_get(ctx, &data_raw_temperature);
  //   imu_data->temperature_degC = lsm6ds3tr_c_from_lsb_to_celsius(data_raw_temperature);
  //   // snprintf((char *)tx_buffer, sizeof(tx_buffer), "Temperature [degC]:%6.2f",
  //   //         imu_data->temperature_degC );
  //   // tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
  // }
  uint32_t diff = millis() - imu->_lastimestamp;
  if (diff >= 1000)
  {
    imu->_lastimestamp = millis();
    imu->freq[0] = imu->_counter[0] / (diff / 1000.0f);
    imu->_counter[0] = 0;
    imu->freq[1] = imu->_counter[1] / (diff / 1000.0f);
    imu->_counter[1] = 0;

    lsm6ds3tr_c_all_sources_t src;
    bool new_inactive = false;
    if (lsm6ds3tr_c_all_sources_get(imu->ctx, &src) == 0)
    {
      if(imu->inactive_cb)
      {
        new_inactive = (src.wake_up_src.sleep_state_ia == 1);
        if(imu->is_inactive != new_inactive)
        {
          imu->inactive_cb(imu, new_inactive);
        }
      }
      imu->is_inactive = new_inactive;
    }
  }
}


static void app_lsm6ds3tr_task(void *arg)
{

  uint32_t last = 0;
  /* Read samples in polling mode (no int) */
  while (1)
  {
    lsm6d_loop(&imu_ctx[0], &imu[0]);
    delay(10);
    lsm6d_loop(&imu_ctx[1], &imu[1]);
    delay(10);

    if (millis() - last > 1000)
    {
      last = millis();
      // ESP_LOGI(TAG, "mg0:[%4.2f], mdps0:[%4.2f],mg1:[%4.2f], mdps1:[%4.2f]",
      //   rpl_get(&imu[0].rpl_mg), (float)rpl_get(&imu[0].rpl_mdps),
      //   rpl_get(&imu[1].rpl_mg), (float)rpl_get(&imu[1].rpl_mdps));

      ESP_LOGI(TAG, "[%d][mg]:%6.2f\t%6.2f\t%6.2f\t hz:%4.2f\t inactive:%d",
               imu[0].id, imu[0].ax_mg, imu[0].ay_mg, imu[0].az_mg, imu[0].freq[0], imu[0].is_inactive);
      // ESP_LOGI(TAG, "[%d][mdps]:%7.2f\t%7.2f\t%7.2f\t hz:%4.2f",
      //   imu[0].id, imu[0].gx_mdps, imu[0].gy_mdps, imu[0].gz_mdps, imu[0].freq[1]);
    }
  }
}

int app_lsm6d_init(void)
{

  lsm6d_init(&imu[0], IMU0_INIT1_PIN, IMU0_INIT2_PIN, &imu_ctx[0], 0);
  lsm6d_init(&imu[1], IMU1_INIT1_PIN, IMU1_INIT2_PIN, &imu_ctx[1], 1);
  app_lsm6d_mode1_init(&imu[0]);
  app_lsm6d_mode1_init(&imu[1]);
  app_lsm6d_attach((void *)&imu[0], handler_imu_inactive);
  app_lsm6d_attach((void *)&imu[1], handler_imu_inactive);

  // lsm6d_init2(&imu_ctx[0]);  
  // lsm6d_init2(&imu_ctx[1]);

  // 执行太频繁，降低优先级
  xTaskCreate(app_lsm6ds3tr_task, "imu_notify", 4 * 1024, NULL, PRIORITY_IMU_TASK, NULL);

  return 0;
}


// int app_lsm6d_acc_wake_thresh_set(imu_t *imu, uint32_t thresh)
// {
//   // Threshold for wakeup.1 LSB = FS_XL / 64 * [set]
//   // FS_XL = 4000mg, 1 LSB = 4000/64=62.5mg
//   // WAKE_UP_THS = thresh/62.5mg;
//   uint8_t val = (uint8_t)(thresh / 62.5f);
//   imu->acc_wake_thresh_mg = val * 62.5f;
//   lsm6ds3tr_c_wkup_threshold_set(imu->ctx, val);
//   ESP_LOGI(TAG, "[IMU%u] set wakeup threshold: %lu mg, reg:%u", imu->id, imu->acc_wake_thresh_mg, val);
//   return 0;
// }
int app_lsm6d_acc_sleep_thresh_set(imu_t *imu, uint32_t thresh_mg)
{
    lsm6ds3tr_c_fs_xl_t fs;
    float fs_mg = 4000.0f; // 默认
    float lsb_mg;

    // 读取当前加速度满量程设置
    if (lsm6ds3tr_c_xl_full_scale_get(imu->ctx, &fs) == 0)
    {
        switch (fs)
        {
            case LSM6DS3TR_C_2g:  fs_mg = 2000.0f; break;
            case LSM6DS3TR_C_4g:  fs_mg = 4000.0f; break;
            case LSM6DS3TR_C_8g:  fs_mg = 8000.0f; break;
            case LSM6DS3TR_C_16g: fs_mg = 16000.0f; break;
            default:               fs_mg = 4000.0f; break;
        }
    }

    // 计算 1 LSB 对应的 mg
    lsb_mg = fs_mg / 64.0f;

    // 计算寄存器值
    uint8_t val = (uint8_t)(thresh_mg / lsb_mg);
    if (val > 0x3F) val = 0x3F;

    // 保存和设置
    imu->acc_sleep_thresh_mg = (uint32_t)(val * lsb_mg);
    lsm6ds3tr_c_wkup_threshold_set(imu->ctx, val);

    ESP_LOGI(TAG, "[IMU%u] sleep thresh: %luf mg (FS=%.0f mg, reg=%u)", 
             imu->id, imu->acc_sleep_thresh_mg, fs_mg, val);

    return 0;
}
uint32_t app_lsm6d_acc_sleep_thresh_get(imu_t *imu)
{
  return imu->acc_sleep_thresh_mg;
}
uint32_t app_lsm6d_acc_sleep_after_get(imu_t *imu)
{
  return imu->acc_sleep_after_s;
}
int app_lsm6d_acc_sleep_after_set(imu_t *imu, uint32_t sleep_after_s)
{
  imu->acc_sleep_after_s = sleep_after_s;
  ESP_LOGI(TAG, "[IMU%u] set sleep after: %lu s", imu->id, imu->acc_sleep_after_s);
  return 0;
}



int app_lsm6d_enable_set(imu_t *imu, bool enable)
{
  imu->enable = enable;
  if (enable)
  {
    lsm6ds3tr_c_xl_data_rate_set(imu->ctx, LSM6DS3TR_C_XL_ODR_52Hz);
    lsm6ds3tr_c_gy_data_rate_set(imu->ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  }
  else
  {
    lsm6ds3tr_c_xl_data_rate_set(imu->ctx, LSM6DS3TR_C_XL_ODR_OFF);
    lsm6ds3tr_c_gy_data_rate_set(imu->ctx, LSM6DS3TR_C_GY_ODR_OFF);
  }
  ESP_LOGI(TAG, "[IMU%u] set enable: %d", imu->id, enable);
  return 0;
}
bool app_lsm6d_enable_get(imu_t *imu)
{
  return imu->enable;
}


bool app_lsm6d_attach(imu_t *imu, imu_inactive_cb_t inactive_cb)
{
  imu->inactive_cb = inactive_cb;
  return true;
}

void app_lsm6d_detach(imu_t *imu)
{
  imu->inactive_cb = NULL;
}



































/*
 * @brief  Write generic device register (platform dependent)
 *
 * @param  handle    customizable argument. In this examples is used in
 *                   order to select the correct sensor bus handler.
 * @param  reg       register to write
 * @param  bufp      pointer to data to write in register reg
 * @param  len       number of consecutive register to write
 *
 */
static int32_t platform_write_6a(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
  i2c_write_buffer(0X6A, reg, bufp, len);
  return 0;
}
static int32_t platform_write_6b(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
  i2c_write_buffer(0X6B, reg, bufp, len);
  return 0;
}
/*
 * @brief  Read generic device register (platform dependent)
 *
 * @param  handle    customizable argument. In this examples is used in
 *                   order to select the correct sensor bus handler.
 * @param  reg       register to read
 * @param  bufp      pointer to buffer that store the data read
 * @param  len       number of consecutive register to read
 *
 */
static int32_t platform_read_6a(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
  i2c_read(0X6A, reg, bufp, len);
  // ESP_LOGI(TAG, "platform_read_6a 0X6A reg 0x%02X, len=%d, data=", reg,len);
  // ESP_LOG_BUFFER_HEXDUMP(TAG, bufp, len, ESP_LOG_INFO);
  // delay(10);
  return 0;
}
static int32_t platform_read_6b(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
  i2c_read(0X6B, reg, bufp, len);
  // ESP_LOGI(TAG, "platform_read_6b 0X6B reg 0x%02X, len=%d, data=", reg,len);
  // ESP_LOG_BUFFER_HEXDUMP(TAG, bufp, len, ESP_LOG_INFO);
  // delay(10);
  return 0;
}

/*
 * @brief  Write generic device register (platform dependent)
 *
 * @param  tx_buffer     buffer to transmit
 * @param  len           number of byte to send
 *
 */
static void tx_com(uint8_t *tx_buffer, uint16_t len)
{
  // ESP_LOG_BUFFER_HEXDUMP(TAG, tx_buffer, len, ESP_LOG_INFO);
  ESP_LOGI(TAG, "%s", (char *)tx_buffer);
}

/*
 * @brief  platform specific delay (platform dependent)
 *
 * @param  ms        delay in ms
 *
 */
static void platform_delay(uint32_t ms)
{

  delay(ms);
}

/*
 * @brief  platform specific initialization (platform dependent)
 */
static void platform_init(void)
{
  if (i2c_master_init() != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize I2C");
  }
}
