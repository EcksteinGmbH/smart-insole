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

#define    BOOT_TIME        150 //ms
#define    WAIT_TIME_A     100 //ms
#define    WAIT_TIME_G_01  150 //ms
#define    WAIT_TIME_G_02   150 //ms

/* Self test limits. */
#define    MIN_ST_LIMIT_mg        90.0f
#define    MAX_ST_LIMIT_mg      1700.0f
#define    MIN_ST_LIMIT_mdps   150000.0f
#define    MAX_ST_LIMIT_mdps   700000.0f

/* Self test results. */
#define    ST_PASS     1U
#define    ST_FAIL     0U

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
    // ESP_LOGI(TAG, "platform_read 0X6A reg 0x%02X, len=%d, data=", reg,len);
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


/* Main Example --------------------------------------------------------------*/
void lsm6ds3tr_c_self_test(void)
{
  uint8_t tx_buffer[1000];
  int16_t data_raw[3];
  stmdev_ctx_t dev_ctx;
  float_t val_st_off[3];
  float_t val_st_on[3];
  float_t test_val[3];
  uint8_t st_result;
  uint8_t whoamI;
  uint8_t drdy;
  uint8_t rst;
  uint8_t i;
  uint8_t j;
  /* Initialize mems driver interface */
  dev_ctx.write_reg = platform_write_6a;
  dev_ctx.read_reg = platform_read_6a;
  dev_ctx.mdelay = platform_delay;
  dev_ctx.handle = 0;
  /* Init test platform */
  platform_init();
  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);
  /* Check device ID */
  lsm6ds3tr_c_device_id_get(&dev_ctx, &whoamI);

  if (whoamI != LSM6DS3TR_C_ID)
  {
    ESP_LOGI(TAG, "LSM6DS3TR-C not found");
    while (1)
    {
        delay(1000);
    }
  }

  /* Restore default configuration */
  lsm6ds3tr_c_reset_set(&dev_ctx, PROPERTY_ENABLE);

  do {
    lsm6ds3tr_c_reset_get(&dev_ctx, &rst);
  } while (rst);
  ESP_LOGI(TAG, "LSM6DS3TR-C found");

  /* Enable Block Data Update */
  lsm6ds3tr_c_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
  /*
   * Accelerometer Self Test
   */
  /* Set Output Data Rate */
  lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  /* Set full scale */
  lsm6ds3tr_c_xl_full_scale_set(&dev_ctx, LSM6DS3TR_C_4g);
  /* Wait stable output */
  platform_delay(WAIT_TIME_A);

  /* Check if new value available */
  do {
    lsm6ds3tr_c_xl_flag_data_ready_get(&dev_ctx, &drdy);
  } while (!drdy);
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test");

  /* Read dummy data and discard it */
  lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, data_raw);
  /* Read 5 sample and get the average vale for each axis */
  memset(val_st_off, 0x00, 3 * sizeof(float));

  for (i = 0; i < 5; i++) {
    /* Check if new value available */
    do {
      lsm6ds3tr_c_xl_flag_data_ready_get(&dev_ctx, &drdy);
    } while (!drdy);
    ESP_LOGI(TAG, "LSM6DS3TR-C Self Test %d", i);
    /* Read data and accumulate the mg value */
    lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, data_raw);

    for (j = 0; j < 3; j++) {
      val_st_off[j] += lsm6ds3tr_c_from_fs4g_to_mg(data_raw[j]);
    }
  }

  /* Calculate the mg average values */
  for (i = 0; i < 3; i++) {
    val_st_off[i] /= 5.0f;
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test val_st_off: %f, %f, %f", val_st_off[0], val_st_off[1], val_st_off[2]);
  /* Enable Self Test positive (or negative) */
  lsm6ds3tr_c_xl_self_test_set(&dev_ctx, LSM6DS3TR_C_XL_ST_NEGATIVE);
  //lsm6ds3tr_c_xl_self_test_set(&dev_ctx, LSM6DS3TR_C_XL_ST_POSITIVE);
  /* Wait stable output */
  platform_delay(WAIT_TIME_A);

  /* Check if new value available */
  do {
    lsm6ds3tr_c_xl_flag_data_ready_get(&dev_ctx, &drdy);
  } while (!drdy);
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test A");

  /* Read dummy data and discard it */
  lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, data_raw);
  /* Read 5 sample and get the average vale for each axis */
  memset(val_st_on, 0x00, 3 * sizeof(float));

  for (i = 0; i < 5; i++) {
    /* Check if new value available */
    do {
      lsm6ds3tr_c_xl_flag_data_ready_get(&dev_ctx, &drdy);
    } while (!drdy);

    /* Read data and accumulate the mg value */
    lsm6ds3tr_c_acceleration_raw_get(&dev_ctx, data_raw);

    for (j = 0; j < 3; j++) {
      val_st_on[j] += lsm6ds3tr_c_from_fs4g_to_mg(data_raw[j]);
    }
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test val_st_on: %f, %f, %f", val_st_on[0], val_st_on[1], val_st_on[2]);

  /* Calculate the mg average values */
  for (i = 0; i < 3; i++) {
    val_st_on[i] /= 5.0f;
  }

  /* Calculate the mg values for self test */
  for (i = 0; i < 3; i++) {
    test_val[i] = fabsf((val_st_on[i] - val_st_off[i]));
  }

  /* Check self test limit */
  st_result = ST_PASS;

  for (i = 0; i < 3; i++) {
    if (( MIN_ST_LIMIT_mg > test_val[i] ) ||
        ( test_val[i] > MAX_ST_LIMIT_mg)) {
      st_result = ST_FAIL;
    }
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test result: %s", (st_result == ST_PASS) ? "PASS" : "FAIL");
  
  /* Disable Self Test */
  lsm6ds3tr_c_xl_self_test_set(&dev_ctx, LSM6DS3TR_C_XL_ST_DISABLE);
  /* Disable sensor. */
  lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_XL_ODR_OFF);
  /*
   * Gyroscope Self Test
   */
  /* Set Output Data Rate */
  lsm6ds3tr_c_gy_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_208Hz);
  /* Set full scale */
  lsm6ds3tr_c_gy_full_scale_set(&dev_ctx, LSM6DS3TR_C_2000dps);
  /* Wait stable output */
  platform_delay(WAIT_TIME_G_01);

  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test-------------");
  /* Check if new value available */
  do {
    lsm6ds3tr_c_gy_flag_data_ready_get(&dev_ctx, &drdy);
  } while (!drdy);
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test B");
  
  /* Read dummy data and discard it */
  lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx, data_raw);
  /* Read 5 sample and get the average vale for each axis */
  memset(val_st_off, 0x00, 3 * sizeof(float));
  for (i = 0; i < 5; i++) {
    uint32_t start_time = millis() ; // 转换为毫秒
    /* Check if new value available */
    do {
      lsm6ds3tr_c_gy_flag_data_ready_get(&dev_ctx, &drdy);
      if(millis() - start_time > 1000) {
        ESP_LOGE(TAG, "数据就绪检查超时");
        break; // 或处理超时情况
      }
      delay(1000);
      // ESP_LOGI(TAG, "LSM6DS3TR-C Self Test B2 %d", i);
    } while (!drdy);
    ESP_LOGI(TAG, "LSM6DS3TR-C Self Test B2");

    /* Read data and accumulate the mg value */
    lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx, data_raw);

    for (j = 0; j < 3; j++) {
      val_st_off[j] += lsm6ds3tr_c_from_fs2000dps_to_mdps(
                         data_raw[j]);
    }
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test val_st_off C: %f, %f, %f", val_st_off[0], val_st_off[1], val_st_off[2]);

  /* Calculate the mg average values */
  for (i = 0; i < 3; i++) {
    val_st_off[i] /= 5.0f;
  }

  /* Enable Self Test positive (or negative) */
  lsm6ds3tr_c_gy_self_test_set(&dev_ctx, LSM6DS3TR_C_GY_ST_POSITIVE);
  //lsm6ds3tr_c_gy_self_test_set(&dev_ctx, LIS2DH12_GY_ST_NEGATIVE);
  /* Wait stable output */
  platform_delay(WAIT_TIME_G_02);
  /* Read 5 sample and get the average vale for each axis */
  memset(val_st_on, 0x00, 3 * sizeof(float));

  for (i = 0; i < 5; i++) {
    /* Check if new value available */
    do {
      lsm6ds3tr_c_xl_flag_data_ready_get(&dev_ctx, &drdy);
      delay(1000);

    } while (!drdy);

    /* Read data and accumulate the mg value */
    lsm6ds3tr_c_angular_rate_raw_get(&dev_ctx, data_raw);

    for (j = 0; j < 3; j++) {
      val_st_on[j] += lsm6ds3tr_c_from_fs2000dps_to_mdps(
                        data_raw[j]);
    }
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test val_st_on D: %f, %f, %f", val_st_on[0], val_st_on[1], val_st_on[2]);

  /* Calculate the mg average values */
  for (i = 0; i < 3; i++) {
    val_st_on[i] /= 5.0f;
  }

  /* Calculate the mg values for self test */
  for (i = 0; i < 3; i++) {
    test_val[i] = fabsf((val_st_on[i] - val_st_off[i]));
  }

  /* Check self test limit */
  for (i = 0; i < 3; i++) {
    if (( MIN_ST_LIMIT_mdps > test_val[i] ) ||
        ( test_val[i] > MAX_ST_LIMIT_mdps)) {
      st_result = ST_FAIL;
    }
  }
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test result E: %s", (st_result == ST_PASS) ? "PASS" : "FAIL");
  
  /* Disable Self Test */
  lsm6ds3tr_c_gy_self_test_set(&dev_ctx, LSM6DS3TR_C_GY_ST_DISABLE);
  /* Disable sensor. */
  lsm6ds3tr_c_xl_data_rate_set(&dev_ctx, LSM6DS3TR_C_GY_ODR_OFF);

  if (st_result == ST_PASS) {
    snprintf((char *)tx_buffer, sizeof(tx_buffer), "Self Test - PASS\r\n" );
  }

  else {
    snprintf((char *)tx_buffer, sizeof(tx_buffer), "Self Test - FAIL\r\n" );
  }

//   tx_com(tx_buffer, strlen((char const *)tx_buffer));
  ESP_LOG_BUFFER_HEXDUMP(TAG, tx_buffer, strlen((char const *)tx_buffer), ESP_LOG_INFO);
  ESP_LOGI(TAG, "LSM6DS3TR-C Self Test done");
}

int app_lsm6d_init(void)
{
    if (i2c_master_init() != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return -1;
    }

    lsm6ds3tr_c_self_test();
    // 初始化 I2C


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