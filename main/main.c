#include "audio_bsp.h"
#include "button_input.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "freertos/idf_additions.h"
#include "sdcard_bsp.h"
#include "wav_writer.h"

typedef enum {
  IDLE = 0,
  RECORDING,
  POST_SAVE,
} app_state_t;

static volatile bool is_recording = false;
static SemaphoreHandle_t s_mutex = NULL;

static void record_task(void *arg) {
  const size_t buffer_size = 1024;
  uint8_t *buffer = malloc(buffer_size);
  if (buffer == NULL) {
    esp_rom_printf("Failed to allocate buffer for recording\n");
    vTaskDelete(NULL);
    return;
  }

  while (is_recording) {
    ESP_ERROR_CHECK(audio_bsp_record(buffer, buffer_size));
    ESP_ERROR_CHECK(wav_write(buffer, buffer_size));
  }

  free(buffer);
  xSemaphoreGive(s_mutex);
  vTaskDelete(NULL);
}

void app_main(void) {
  app_state_t state = IDLE;
  EventGroupHandle_t button_group = button_init();

  ESP_ERROR_CHECK(sdcard_init());
  ESP_ERROR_CHECK(audio_bsp_init());

  for (;;) {
    EventBits_t uxBits = xEventGroupWaitBits(
        button_group, RECORD_BUTTON_BIT | POWER_BUTTON_BIT,
        pdTRUE,  /* Clear before returning. */
        pdFALSE, /* Don't wait for both bits, either bit will do. */
        portMAX_DELAY);

    if ((uxBits & RECORD_BUTTON_BIT) != 0) {
      esp_rom_printf("Boot button pressed\n");
      if (state == IDLE) {
        state = RECORDING;
        ESP_ERROR_CHECK(wav_open("/sdcard/test.wav"));
        s_mutex = xSemaphoreCreateBinary();
        is_recording = true;
        xTaskCreate(record_task, "record_task", 4096, NULL, 5, NULL);
      } else if (state == RECORDING) {
        state = POST_SAVE;
        is_recording = false;
        xSemaphoreTake(s_mutex, portMAX_DELAY); // Block until recording task finishes
        esp_rom_printf("Saving data...\n");
        ESP_ERROR_CHECK(wav_close());
      }
    } else if ((uxBits & POWER_BUTTON_BIT) != 0) {
      esp_rom_printf("Power pressed...\n");
    }

    if (state == POST_SAVE) {
      esp_rom_printf("Data saved. Returning to IDLE state.\n");
      state = IDLE;
    }
  }
}
