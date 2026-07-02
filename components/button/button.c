#include "button.h"
#include "config.h"
#include "esp_log.h"
#include "iot_button.h"
#include <stdint.h>

static EventGroupHandle_t button_group;

static void button_single_click_cb(void *arg, void *usr_data) {
  ESP_LOGI("BUTTON", "BUTTON_SINGLE_CLICK");
  uint32_t bit_num = (uint32_t)(uintptr_t)arg;
  xEventGroupSetBits(button_group, bit_num);
}

EventGroupHandle_t button_init(void) {
  button_group = xEventGroupCreate();

  const button_config_t menu_btn_cfg = {0};
  const button_gpio_config_t menu_btn_gpio_cfg = {
      .gpio_num = BOOT_BUTTON,
      .active_level = 0,
      .enable_power_save = true,
  };
  button_handle_t menu_gpio_btn = NULL;
  esp_err_t ret = iot_button_new_gpio_device(&menu_btn_cfg, &menu_btn_gpio_cfg,
                                             &menu_gpio_btn);
  if (menu_gpio_btn == NULL) {
    ESP_LOGE("BUTTON", "Menu button create failed");
    return button_group;
  }

  iot_button_register_cb(menu_gpio_btn, BUTTON_SINGLE_CLICK, NULL,
                         button_single_click_cb, (void *)BOOT_BUTTON_BIT);

  const button_config_t power_btn_cfg = {0};
  const button_gpio_config_t power_btn_gpio_cfg = {
      .gpio_num = POWER_BUTTON,
      .active_level = 0,
      .enable_power_save = true,
  };
  button_handle_t power_gpio_btn = NULL;
  ret = iot_button_new_gpio_device(&power_btn_cfg, &power_btn_gpio_cfg,
                                   &power_gpio_btn);
  if (power_gpio_btn == NULL) {
    ESP_LOGE("BUTTON", "Power button create failed");
    return button_group;
  }

  iot_button_register_cb(power_gpio_btn, BUTTON_SINGLE_CLICK, NULL,
                         button_single_click_cb, (void *)POWER_BUTTON_BIT);

  return button_group;
}
