#include "button_input.h"
#include "app_events.h"
#include "button_gpio.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "iot_button.h"
#include <stdint.h>

static const char *TAG = "button_input";

// Generic handler: sets the event-group bit passed as usr_data. Shared across
// single-click and long-press events for both buttons.
static void button_set_bit_cb(void *arg, void *usr_data) {
  app_events_set((EventBits_t)(uintptr_t)usr_data);
}

// A GPIO button that raises `bit` on a short press. Both buttons are wired the
// same way -- active low, power-save enabled so they still wake us from sleep.
static esp_err_t add_button(gpio_num_t gpio, const char *name, EventBits_t bit,
                            button_handle_t *out) {
  const button_config_t cfg = {0};
  const button_gpio_config_t gpio_cfg = {
      .gpio_num = gpio,
      .active_level = 0,
      .enable_power_save = true,
  };

  esp_err_t err = iot_button_new_gpio_device(&cfg, &gpio_cfg, out);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s button create failed: %s", name, esp_err_to_name(err));
    return err;
  }

  return iot_button_register_cb(*out, BUTTON_SINGLE_CLICK, NULL,
                                button_set_bit_cb, (void *)(uintptr_t)bit);
}

esp_err_t button_init(void) {
  button_handle_t record_btn = NULL;
  esp_err_t err =
      add_button(RECORD_BUTTON, "Record", RECORD_BUTTON_BIT, &record_btn);
  if (err != ESP_OK) {
    return err;
  }

  button_handle_t menu_btn = NULL;
  err = add_button(MENU_BUTTON, "Menu", MENU_BUTTON_BIT, &menu_btn);
  if (err != ESP_OK) {
    return err;
  }

  // The Menu Button alone carries a second gesture: hold it to leave the Menu.
  button_event_args_t exit_args = {
      .long_press = {.press_time = MENU_EXIT_HOLD_MS}};
  return iot_button_register_cb(menu_btn, BUTTON_LONG_PRESS_START, &exit_args,
                                button_set_bit_cb, (void *)MENU_EXIT_BIT);
}
