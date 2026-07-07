#include "audio_bsp.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"

#define VOICE_VOLUME 80
#define MIC_GAIN 20

static const char *TAG = "i2s";
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static esp_codec_dev_handle_t codec_handle = NULL;

static esp_err_t i2s_driver_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_MCLK_PIN,
              .bclk = I2S_BCLK_PIN,
              .ws = I2S_WS_PIN,
              .dout = I2S_DOUT_PIN,
              .din = I2S_DIN_PIN,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
  return ESP_OK;
}

static esp_err_t codec_init(void) {
  i2c_master_bus_handle_t i2c_bus_handle = NULL;
  i2c_master_bus_config_t i2c_mst_cfg = {
      .i2c_port = I2C_DEV_NUM,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      /* Pull-up internally for no external pull-up case.
      Suggest to use external pull-up to ensure a strong enough pull-up. */
      .flags.enable_internal_pullup = true,
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_cfg, &i2c_bus_handle));

  /* Create control interface with I2C bus handle */
  audio_codec_i2c_cfg_t i2c_cfg = {
      .port = I2C_DEV_NUM,
      .addr = ES8311_CODEC_DEFAULT_ADDR,
      .bus_handle = i2c_bus_handle,
  };
  const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
  ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG,
                      "Failed to create I2C ctrl interface");

  /* Create data interface with I2S bus handle */
  audio_codec_i2s_cfg_t i2s_cfg = {
      .port = I2S_NUM,
      .rx_handle = rx_handle,
      .tx_handle = tx_handle,
  };
  const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
  ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG,
                      "Failed to create I2C data interface");

  /* Create ES8311 interface handle */
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
  ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG,
                      "Failed to create I2C GPIO interface");

  es8311_codec_cfg_t es8311_cfg = {
      .ctrl_if = ctrl_if,
      .gpio_if = gpio_if,
      .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
      .master_mode = false,
      .use_mclk = I2S_MCLK_PIN >= 0,
      .pa_pin = PA_CTRL_PIN,
      .pa_reverted = false,
      .hw_gain =
          {
              .pa_voltage = 5.0,
              .codec_dac_voltage = 3.3,
          },
      .mclk_div = MCLK_MULTIPLE,
  };
  const audio_codec_if_t *es8311_if = es8311_codec_new(&es8311_cfg);
  ESP_RETURN_ON_FALSE(es8311_if, ESP_FAIL, TAG,
                      "Failed to create I2C interface");

  /* Create the top codec handle with ES8311 interface handle and data interface
   */
  esp_codec_dev_cfg_t dev_cfg = {
      .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
      .codec_if = es8311_if,
      .data_if = data_if,
  };
  codec_handle = esp_codec_dev_new(&dev_cfg);
  ESP_RETURN_ON_FALSE(codec_handle, ESP_FAIL, TAG,
                      "Failed to create codec handle");

  /* Specify the sample configurations and open the device */
  esp_codec_dev_sample_info_t sample_cfg = {
      .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
      .channel = 2,
      .channel_mask = 0x03,
      .sample_rate = SAMPLE_RATE,
      .mclk_multiple = MCLK_MULTIPLE,
  };
  ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_open(codec_handle, &sample_cfg));

  /* Set the initial volume and gain */
  ESP_ERROR_CHECK(
      (esp_err_t)esp_codec_dev_set_out_vol(codec_handle, VOICE_VOLUME));
  ESP_ERROR_CHECK((esp_err_t)esp_codec_dev_set_in_gain(codec_handle, MIC_GAIN));
  return ESP_OK;
}

esp_err_t audio_bsp_init(void) {
  ESP_ERROR_CHECK(i2s_driver_init());
  ESP_ERROR_CHECK(codec_init());
  return ESP_OK;
}
