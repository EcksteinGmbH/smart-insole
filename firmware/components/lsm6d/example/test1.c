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
#include "esp_task.h"
#include "interrupt.h"

static const char *TAG = "APP_LSM6D";

#define WAKEUP_GPIO_0 4  // 第一个 LSM6DS3TR-C 的 INT2
#define WAKEUP_GPIO_1 10 // 第二个 LSM6DS3TR-C 的 INT2

/* Private macro -------------------------------------------------------------*/

#define BOOT_TIME 15       // ms
#define WAIT_TIME_A 100    // ms
#define WAIT_TIME_G_01 150 // ms
#define WAIT_TIME_G_02 50  // ms

/* Self test limits. */
#define MIN_ST_LIMIT_mg 90.0f
#define MAX_ST_LIMIT_mg 1700.0f
#define MIN_ST_LIMIT_mdps 150000.0f
#define MAX_ST_LIMIT_mdps 700000.0f

/* Self test results. */
#define ST_PASS 1U
#define ST_FAIL 0U

stmdev_ctx_t dev_ctx;

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
static int16_t data_raw_acceleration[3];
static int16_t data_raw_angular_rate[3];
static int16_t data_raw_temperature;
static uint8_t whoamI, rst;
static uint8_t tx_buffer[TX_BUF_DIM];

stmdev_ctx_t imu_ctx[2];
imu_data_t imu_data[2];

#define TARGET 1

// 应用2：配置为敲击模式
void app_lsm6d_app2(stmdev_ctx_t *ctx)
{
/* Enable Block Data Update */
  // lsm6ds3tr_c_block_data_update_set(ctx, PROPERTY_ENABLE);
  // /* Set Output Data Rate */
  // lsm6ds3tr_c_xl_data_rate_set(ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  // lsm6ds3tr_c_gy_data_rate_set(ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  // /* Set full scale */
  // lsm6ds3tr_c_xl_full_scale_set(ctx, LSM6DS3TR_C_2g);
  // lsm6ds3tr_c_gy_full_scale_set(ctx, LSM6DS3TR_C_2000dps);
  uint8_t val = 0x50;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL1_XL: 0X%02X", val);

  val = 0x40;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL2_G: 0X%02X", val);

  val = 0x02;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_WAKE_UP_DUR, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_DUR, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "WAKE_UP_DUR: 0X%02X", val);


  val = 0x02;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_WAKE_UP_THS, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_THS, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "WAKE_UP_THS: 0X%02X", val);


  val = 0xe0;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_TAP_CFG, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_TAP_CFG, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "TAP_CFG: 0X%02X", val);




  val = 0x80;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_MD1_CFG, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_MD1_CFG, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "MD1_CFG: 0X%02X", val);

}


// 应用1：配置为唤醒模式
void app_lsm6d_app3(stmdev_ctx_t *ctx)
{
/* Enable Block Data Update */
  // lsm6ds3tr_c_block_data_update_set(ctx, PROPERTY_ENABLE);
  // /* Set Output Data Rate */
  // lsm6ds3tr_c_xl_data_rate_set(ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  // lsm6ds3tr_c_gy_data_rate_set(ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  // /* Set full scale */
  // lsm6ds3tr_c_xl_full_scale_set(ctx, LSM6DS3TR_C_2g);
  // lsm6ds3tr_c_gy_full_scale_set(ctx, LSM6DS3TR_C_2000dps);
  uint8_t val = 0x50;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL1_XL: 0X%02X", val);

  val = 0x40;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL2_G: 0X%02X", val);

  val = 0x02;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_WAKE_UP_DUR, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_DUR, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "WAKE_UP_DUR: 0X%02X", val);


  val = 0x05;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_WAKE_UP_THS, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_WAKE_UP_THS, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "WAKE_UP_THS: 0X%02X", val);


  val = 0xe0;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_TAP_CFG, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_TAP_CFG, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "TAP_CFG: 0X%02X", val);




  val = 0x80;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_MD2_CFG, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_MD2_CFG, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "MD2_CFG: 0X%02X", val);

}


// 应用1：中断读取
void app_lsm6d_app4(stmdev_ctx_t *ctx)
{
/* Enable Block Data Update */
  // lsm6ds3tr_c_block_data_update_set(ctx, PROPERTY_ENABLE);
  // /* Set Output Data Rate */
  // lsm6ds3tr_c_xl_data_rate_set(ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  // lsm6ds3tr_c_gy_data_rate_set(ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  // /* Set full scale */
  // lsm6ds3tr_c_xl_full_scale_set(ctx, LSM6DS3TR_C_2g);
  // lsm6ds3tr_c_gy_full_scale_set(ctx, LSM6DS3TR_C_2000dps);
  uint8_t val = 0x50;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL1_XL, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL1_XL: 0X%02X", val);

  val = 0x40;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_CTRL2_G, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "CTRL2_G: 0X%02X", val);

  
 

  val = 0x01;
  lsm6ds3tr_c_write_reg(ctx, LSM6DS3TR_C_INT2_CTRL, (uint8_t *)&val, 1);
  lsm6ds3tr_c_read_reg(ctx, LSM6DS3TR_C_INT2_CTRL, (uint8_t *)&val, 1);
  ESP_LOGI(TAG, "INT1_CTRL: 0X%02X", val);

}

// 应用1：中断读取
void app_lsm6d_app1(stmdev_ctx_t *ctx)
{
/* Enable Block Data Update */
  lsm6ds3tr_c_block_data_update_set(ctx, PROPERTY_ENABLE);
  /* Set Output Data Rate */
  lsm6ds3tr_c_xl_data_rate_set(ctx, LSM6DS3TR_C_XL_ODR_52Hz);
  lsm6ds3tr_c_gy_data_rate_set(ctx, LSM6DS3TR_C_GY_ODR_52Hz);
  /* Set full scale */
  lsm6ds3tr_c_xl_full_scale_set(ctx, LSM6DS3TR_C_2g);
  lsm6ds3tr_c_gy_full_scale_set(ctx, LSM6DS3TR_C_2000dps);

  lsm6ds3tr_c_int2_route_t val;
  val.int2_drdy_xl = PROPERTY_ENABLE;
   lsm6ds3tr_c_pin_int2_route_get(ctx, &val);
  ESP_LOGI(TAG, "INT2_ROUTE: 0X%02X", val);


}

/* Main Example --------------------------------------------------------------*/
void lsm6d_init(stmdev_ctx_t *ctx, uint8_t id)
{

  /* Initialize mems driver interface */
  if (id == 0)
  {
    ctx->write_reg = platform_write_6a;
    ctx->read_reg = platform_read_6a;
  }
  else if (id == 1)
  {
    ctx->write_reg = platform_write_6b;
    ctx->read_reg = platform_read_6b;
  }
  else
  {
    ESP_LOGI(TAG, "Invalid IMU ID");
    return;
  }

  ctx->mdelay = platform_delay;
  ctx->handle = NULL;
  /* Init test platform */
  platform_init();
  /* Wait sensor boot time */
  platform_delay(BOOT_TIME);
  /* Check device ID */
  whoamI = 0;
  lsm6ds3tr_c_device_id_get(ctx, &whoamI);
  ESP_LOGI(TAG,"WHO: 0x%02x",whoamI);

  if (whoamI != LSM6DS3TR_C_ID)
  {
    ESP_LOGI(TAG,"LSM6DS3TR-C not found");
    while (1)
    {
      platform_delay(100);
    }
  }
  /* Restore default configuration */
  lsm6ds3tr_c_reset_set(ctx, PROPERTY_ENABLE);

  do
  {
    lsm6ds3tr_c_reset_get(ctx, &rst);
    platform_delay(10);
  } while (rst);
  
  
}
void lsm6d_loop(stmdev_ctx_t *ctx,imu_data_t *imu_data)
{
  /* Read output only if new value is available */
  lsm6ds3tr_c_reg_t reg;
  lsm6ds3tr_c_status_reg_get(ctx, &reg.status_reg);

  if (reg.status_reg.xlda)
  {
    /* Read acceleration data */
    memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_acceleration_raw_get(ctx, data_raw_acceleration);
    imu_data->ax_mg = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[0])/1.0f;
    imu_data->ay_mg = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[1])/1.0f;
    imu_data->az_mg = lsm6ds3tr_c_from_fs2g_to_mg(data_raw_acceleration[2])/1.0f;
    imu_data->freq[0] = 1000.0f / (millis() - imu_data->_lastimestamp[0]);
    imu_data->_lastimestamp[0] = millis();



    snprintf((char *)tx_buffer, sizeof(tx_buffer),"Acceleration [mg]:%4.2f\t%4.2f\t%4.2f\t%4.2f",
            imu_data->ax_mg, imu_data->ay_mg, imu_data->az_mg, imu_data->freq[0]);
    tx_com( tx_buffer, strlen( (char const *)tx_buffer ) );
  }
  if (reg.status_reg.gda)
  {
    /* Read magnetic field data */
    memset(data_raw_angular_rate, 0x00, 3 * sizeof(int16_t));
    lsm6ds3tr_c_angular_rate_raw_get(ctx, data_raw_angular_rate);   
    imu_data->gx_mdps = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[0]);
    imu_data->gy_mdps = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[1]);
    imu_data->gz_mdps = lsm6ds3tr_c_from_fs2000dps_to_mdps(data_raw_angular_rate[2]);
    imu_data->freq[1] = 1000.0f / (millis() - imu_data->_lastimestamp[1]);
    imu_data->_lastimestamp[1] = millis();

    // snprintf((char *)tx_buffer, sizeof(tx_buffer),
    //         "Angular rate [%d][mdps]:%4.2f\t%4.2f\t%4.2f\t %4.2f",
    //         imu_data->id, imu_data->gx_mdps, imu_data->gy_mdps, imu_data->gz_mdps,imu_data->freq[1]);
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
}

static void app_lsm6ds3tr_task(void *arg)
{

  uint32_t last = 0;
  /* Read samples in polling mode (no int) */
  while (1)
  {
    lsm6d_loop(&imu_ctx[TARGET], &imu_data[TARGET]);
    // delay(2);
    // lsm6d_loop(&imu_ctx[1], &imu_data[1]);
    delay(2);
  }
}

int app_lsm6d_init(void)
{
  interrupt_init();
  imu_data[0].enable = true;
  imu_data[0].id = 0;
  imu_data[1].enable = true;
  imu_data[1].id = 1;
  lsm6d_init(&imu_ctx[TARGET], TARGET);
  app_lsm6d_app1(&imu_ctx[TARGET]);

  // lsm6d_init2(&imu_ctx[0]);
  // lsm6d_init2(&imu_ctx[1]);

  xTaskCreate(app_lsm6ds3tr_task, "imu_notify", 4 * 1024, NULL, 5, NULL);

  return 0;
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
  ESP_LOGI(TAG, "tx_com: %s", (char *)tx_buffer);
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
