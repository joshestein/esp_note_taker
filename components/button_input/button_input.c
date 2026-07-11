#include "button_input.h"
#include "button_gpio.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "iot_button.h"
#include <stdint.h>

static EventGroupHandle_t button_group;

// Generic handler: sets the event-group bit passed as usr_data. Shared across
// single-click and long-press events for both buttons.
static void button_set_bit_cb(void *arg, void *usr_data) {
  uint32_t bit_num = (uint32_t)(uintptr_t)usr_data;
  xEventGroupSetBits(button_group, bit_num);
}

esp_err_t button_init(EventGroupHandle_t *out_button_group) {
  button_group = xEventGroupCreate();
  if (button_group == NULL) return ESP_ERR_NO_MEM;

  const button_config_t record_btn_cfg = {0};
  const button_gpio_config_t record_btn_gpio_cfg = {
      .gpio_num = RECORD_BUTTON,
      .active_level = 0,
      .enable_power_save = true,
  };
  button_handle_t record_gpio_btn = NULL;
  esp_err_t ret = iot_button_new_gpio_device(
      &record_btn_cfg, &record_btn_gpio_cfg, &record_gpio_btn);
  if (ret != ESP_OK) {
    ESP_LOGE("BUTTON", "Record button create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  iot_button_register_cb(record_gpio_btn, BUTTON_SINGLE_CLICK, NULL,
                         button_set_bit_cb, (void *)RECORD_BUTTON_BIT);

  const button_config_t menu_btn_cfg = {0};
  const button_gpio_config_t menu_btn_gpio_cfg = {
      .gpio_num = MENU_BUTTON,
      .active_level = 0,
      .enable_power_save = true,
  };
  button_handle_t menu_gpio_btn = NULL;
  ret = iot_button_new_gpio_device(&menu_btn_cfg, &menu_btn_gpio_cfg,
                                   &menu_gpio_btn);
  if (ret != ESP_OK) {
    ESP_LOGE("BUTTON", "Menu button create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  iot_button_register_cb(menu_gpio_btn, BUTTON_SINGLE_CLICK, NULL,
                         button_set_bit_cb, (void *)MENU_BUTTON_BIT);

  *out_button_group = button_group;
  return ESP_OK;
}
