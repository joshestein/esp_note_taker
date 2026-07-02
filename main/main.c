#include "button.h"
#include "esp_rom_sys.h"
#include <stdio.h>

typedef enum {
  IDLE = 0,
  RECORDING,
  POST_SAVE,
} app_state_t;

void app_main(void) {
  app_state_t state = IDLE;
  EventGroupHandle_t button_group = button_init();

  for (;;) {
    EventBits_t uxBits = xEventGroupWaitBits(
        button_group, BOOT_BUTTON_BIT | POWER_BUTTON_BIT,
        pdTRUE,  /* Clear before returning. */
        pdFALSE, /* Don't wait for both bits, either bit will do. */
        portMAX_DELAY);

    if ((uxBits & BOOT_BUTTON_BIT) != 0) {
      esp_rom_printf("Boot button pressed\n");
      if (state == IDLE) {
        state = RECORDING;
      } else if (state == RECORDING) {
        state = POST_SAVE;
      }
    } else if ((uxBits & POWER_BUTTON_BIT) != 0) {
      esp_rom_printf("Power pressed...\n");
    }

    if (state == POST_SAVE) {
      esp_rom_printf("Saving data...\n");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_rom_printf("Data saved. Returning to IDLE state.\n");
      state = IDLE;
    }
  }
}
