#include "battery_bsp.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <stdint.h>

static const char *TAG = "battery_bsp";

// LiPo terminal voltage is halved by a 1:2 divider before reaching GPIO4, so
// the calibrated millivolts are doubled back in read_avg_mv. GPIO4 = ADC1_CH3.
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define DIVIDER_RATIO 2
#define SAMPLE_COUNT 8

// Rest-voltage band edges (mV), highest first. Below MV_LOW is the knee of the
// LiPo curve -- minutes of runtime left, reported Critical.
#define MV_FULL 4000
#define MV_HIGH 3850
#define MV_MEDIUM 3700
#define MV_LOW 3500

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;
static battery_level_t last_good = BATTERY_LEVEL_UNKNOWN;

esp_err_t battery_init(void) {
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
  esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc unit init failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = ADC_ATTEN_DB_12, // ~0-3.3V; the halved LiPo (max ~2.1V) fits
      .bitwidth = ADC_BITWIDTH_12,
  };
  err = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc channel config failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc calibration init failed: %s", esp_err_to_name(err));
    return err;
  }
  return ESP_OK;
}

// Average SAMPLE_COUNT reads, calibrate to millivolts, undo the divider. Any
// failed read aborts the whole sample so a glitch never skews the average.
static esp_err_t read_avg_mv(int *out_mv) {
  int64_t sum_mv = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    int raw;
    esp_err_t err = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
      return err;
    }
    int mv;
    err = adc_cali_raw_to_voltage(cali_handle, raw, &mv);
    if (err != ESP_OK) {
      return err;
    }
    sum_mv += mv;
  }
  *out_mv = (int)(sum_mv / SAMPLE_COUNT) * DIVIDER_RATIO;
  return ESP_OK;
}

static battery_level_t level_from_mv(int mv) {
  if (mv >= MV_FULL) {
    return BATTERY_LEVEL_FULL;
  }
  if (mv >= MV_HIGH) {
    return BATTERY_LEVEL_HIGH;
  }
  if (mv >= MV_MEDIUM) {
    return BATTERY_LEVEL_MEDIUM;
  }
  if (mv >= MV_LOW) {
    return BATTERY_LEVEL_LOW;
  }
  return BATTERY_LEVEL_CRITICAL;
}

battery_level_t battery_level(void) {
  int mv;
  if (read_avg_mv(&mv) != ESP_OK) {
    ESP_LOGW(TAG, "battery read failed, holding last level %d", last_good);
    return last_good;
  }
  last_good = level_from_mv(mv);
  return last_good;
}
