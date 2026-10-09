#include "app_lsm6d.h"
#include "esp_log.h"
#include "lsm6d.h"
#include "utils.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_sleep.h"
#include "i2c.h"
#include <string.h>


static const char *TAG = "APP_LSM6D";


#define WAKEUP_GPIO_0               4     // 第一个 LSM6DS3TR-C 的 INT2
#define WAKEUP_GPIO_1               10    // 第二个 LSM6DS3TR-C 的 INT2



/* Private macro -------------------------------------------------------------*/

#define    BOOT_TIME        15 //ms
#define    WAIT_TIME_A     100 //ms
#define    WAIT_TIME_G_01  150 //ms
#define    WAIT_TIME_G_02   50 //ms

/* Self test limits. */
#define    MIN_ST_LIMIT_mg        90.0f
#define    MAX_ST_LIMIT_mg      1700.0f
#define    MIN_ST_LIMIT_mdps   150000.0f
#define    MAX_ST_LIMIT_mdps   700000.0f

/* Self test results. */
#define    ST_PASS     1U
#define    ST_FAIL     0U

stmdev_ctx_t dev_ctx;

/*
 *   WARNING:
 *   Functions declare in this section are defined at the end of this file
 *   and are strictly related to the hardware platform used.
 *
 */
static int32_t platform_write_6a(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len);
static int32_t platform_read_6a(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len);
static void tx_com( uint8_t *tx_buffer, uint16_t len );
static void platform_delay(uint32_t ms);
static void platform_init(void);


#define    BOOT_TIME            15 //ms
#define    TX_BUF_DIM         1000

/* Private variables ---------------------------------------------------------*/
static int16_t data_raw_acceleration[3];
static int16_t data_raw_angular_rate[3];
static int16_t data_raw_temperature;
static float_t acceleration_mg[3];
static float_t angular_rate_mdps[3];
static float_t temperature_degC;
static uint8_t whoamI, rst;
static uint8_t tx_buffer[TX_BUF_DIM];

/* Main Example --------------------------------------------------------------*/
void lsm6ds3tr_c_read_data_polling(void)
{
  /* Initialize mems driver interface */
  stmdev_ctx_t dev_ctx;
  dev_ctx.write_reg = platform_write_6a;
  dev_ctx.read_reg = platform_read_6a;
  dev_ctx.mdelay = platform_delay;
  dev_ctx.handle = NULL;
  /* Init test platform */
  platform_init();
  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);
  /* Check device ID */
  whoamI = 0;
  lsm6ds3tr_c_device_id_get(&dev_ctx, &whoamI);

  if ( whoamI != LSM6DS3TR_C_ID )
    while (1); /*manage here device not found */

  /* Restore default configuration */
  lsm6ds3tr_c_reset_set(&dev_ctx, PROPERTY_ENABLE);

  do {
    lsm6ds3tr_c_reset_get(&dev_ctx, &rst);
  } while (rst);

  /* Enable Block Data Update */
  lsm6ds3tr_c_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
  /* Set Output Data Rate */
  lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_12Hz5);
  lsm6ds3tr_c_gy_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_12Hz5);
  /* Set full scale */
  lsm6ds3tr_c_xl_full_scale_set(&dev_ctx, LSM6DS3TR_C_2g);
  lsm6ds3tr_c_gy_full_scale_set(&dev_ctx, LSM6DS3TR_C_2000dps);
  /* Configure filtering chain(No aux interface) */
  /* Accelerometer - analog filter */
  lsm6ds3tr_c_xl_filter_analog_set(&dev_ctx,
                                   LSM6DS3TR_C_XL_ANA_BW_400Hz);
  /* Accelerometer - LPF1 path ( LPF2 not used )*/
  //lsm6ds3tr_c_xl_lp1_bandwidth_set(&dev_ctx, LSM6DS3TR_C_XL_LP1_ODR_DIV_4);
  /* Accelerometer - LPF1 + LPF2 path */
  lsm6ds3tr_c_xl_lp2_bandwidth_set(&dev_ctx,
                                   LSM6DS3TR_C_XL_LOW_NOISE_LP_ODR_DIV_100);
  /* Accelerometer - High Pass / Slope path */
  //lsm6ds3tr_c_xl_reference_mode_set(&dev_ctx, PROPERTY_DISABLE);
  //lsm6ds3tr_c_xl_hp_bandwidth_set(&dev_ctx, LSM6DS3TR_C_XL_HP_ODR_DIV_100);
  /* Gyroscope - filtering chain */
  lsm6ds3tr_c_gy_band_pass_set(&dev_ctx,
                               LSM6DS3TR_C_HP_260mHz_LP1_STRONG);

  /* Read samples in polling mode (no int) */
  while (1) {
    /* Read output only if new value is available */
    lsm6ds3tr_c_reg_t reg;
    lsm6ds3tr_c_status_reg_get(&dev_ctx, &reg.status_reg);

    if (reg.status_reg.xlda) {
      /* Read magnetic field data */
      memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
      lsm6ds3tr_c_acceleration_raw_get(&dev_ctx,
                                       data_raw_acceleration);
      acceleration_mg[0] = lsm6ds3tr_c_from_fs2g_to_mg(
                             data_raw_acceleration[0]);
      acceleration_mg[1] = lsm6ds3tr_c_from_fs2g_to_mg(
                             data_raw_acceleration[1]);
      acceleration_mg[2] = lsm6ds3tr_c_from_fs2g_to_mg(
                             data_raw_acceleration[2]);
      snprintf((char *)tx_buffer, sizeof(tx_buffer),
              "Acceleration [mg]:%4.2f\t%4.2f\t%4.2f\r\n",
              acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);
      tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
    }

    if (reg.status_reg.gda) {
      /* Read magnetic field data */
      memset(data_raw_angular_rate, 0x00, 3 * sizeof(int16_t));
      lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx,
                                       data_raw_angular_rate);
      angular_rate_mdps[0] = lsm6ds3tr_c_from_fs2000dps_to_mdps(
                               data_raw_angular_rate[0]);
      angular_rate_mdps[1] = lsm6ds3tr_c_from_fs2000dps_to_mdps(
                               data_raw_angular_rate[1]);
      angular_rate_mdps[2] = lsm6ds3tr_c_from_fs2000dps_to_mdps(
                               data_raw_angular_rate[2]);
      snprintf((char *)tx_buffer, sizeof(tx_buffer),
              "Angular rate [mdps]:%4.2f\t%4.2f\t%4.2f\r\n",
              angular_rate_mdps[0], angular_rate_mdps[1], angular_rate_mdps[2]);
      tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
    }

    if (reg.status_reg.tda) {
      /* Read temperature data */
      memset(&data_raw_temperature, 0x00, sizeof(int16_t));
      lsm6ds3tr_c_temperature_raw_get(&dev_ctx, &data_raw_temperature);
      temperature_degC = lsm6ds3tr_c_from_lsb_to_celsius(
                           data_raw_temperature );
      snprintf((char *)tx_buffer, sizeof(tx_buffer), "Temperature [degC]:%6.2f\r\n",
              temperature_degC );
      tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
    }
  }
}

int app_lsm6d_init(void)
{
    if (i2c_master_init() != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return -1;
    }

    lsm6ds3tr_c_read_data_polling();

    


    return 0;
}
void app_lsm6d_loop()
{
    static uint32_t last = 0;
    static uint32_t last2 = 0;
    if( millis() - last > 1000 ) 
    {
        last = millis();

    }

    if( millis() - last2 > 100 ) 
    {
        last2 = millis();

        for (int i = 0; i < 2; i++)
        {

        }
    }


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
static int32_t platform_write_6a(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len)
{
    i2c_write_buffer(0X6A, reg, bufp, len);
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
static int32_t platform_read_6a(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len)
{
    i2c_read(0X6A, reg, bufp, len);
    // ESP_LOGI(TAG, "platform_read_6a 0X6A reg 0x%02X, len=%d, data=", reg,len);
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
    #if defined(NUCLEO_F401RE)
    HAL_UART_Transmit(&huart2, tx_buffer, len, 1000);
    #elif defined(STEVAL_MKI109V3)
    CDC_Transmit_FS(tx_buffer, len);
    #elif defined(SPC584B_DIS)
    sd_lld_write(&SD2, tx_buffer, len);
    #endif
    ESP_LOG_BUFFER_HEXDUMP(TAG, tx_buffer, len, ESP_LOG_INFO);
}

/*
 * @brief  platform specific delay (platform dependent)
 *
 * @param  ms        delay in ms
 *
 */
static void platform_delay(uint32_t ms)
{
#if defined(NUCLEO_F401RE) | defined(STEVAL_MKI109V3)
  HAL_Delay(ms);
#elif defined(SPC584B_DIS)
  osalThreadDelayMilliseconds(ms);
#endif
    delay(ms);
}

/*
 * @brief  platform specific initialization (platform dependent)
 */
static void platform_init(void)
{

}
