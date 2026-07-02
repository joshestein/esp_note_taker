#include "button.h"
#include "config.h"
#include "driver/gpio.h"

static EventGroupHandle_t button_group;

static void IRAM_ATTR button_handler(void* arg) {
  BaseType_t xHigherPriorityTaskWoken, xResult;
  xHigherPriorityTaskWoken = pdFALSE;
  uint32_t bit_num = (uint32_t)(uintptr_t) arg;
  xResult = xEventGroupSetBitsFromISR(button_group, bit_num, &xHigherPriorityTaskWoken);
  if (xResult == pdPASS) {
    // If xHigherPriorityTaskWoken is now set to pdTRUE then a context
    // switch should be requested.  The macro used is port specific and
    // will be either portYIELD_FROM_ISR() or portEND_SWITCHING_ISR() -
    // refer to the documentation page for the port being used.
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

static void gpio_init(void) {
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_NEGEDGE;
  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pin_bit_mask = (1ULL << BOOT_BUTTON) | (1ULL << POWER_BUTTON);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
}

EventGroupHandle_t button_init(void) {
  button_group = xEventGroupCreate();

  gpio_init();

  gpio_wakeup_enable(BOOT_BUTTON, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable(POWER_BUTTON, GPIO_INTR_LOW_LEVEL);

  gpio_install_isr_service(0); // 0 = default
  gpio_isr_handler_add(BOOT_BUTTON, button_handler, (void *)BOOT_BUTTON_BIT);
  gpio_isr_handler_add(POWER_BUTTON, button_handler, (void *)POWER_BUTTON_BIT);

  return button_group;
}
