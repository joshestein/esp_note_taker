#include "audio_bsp.h"
#include "button_input.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/idf_additions.h"
#include "sdcard_bsp.h"
#include "wav_writer.h"

static const char *TAG = "main";

typedef enum {
  IDLE = 0,
  RECORDING,
  FINALISING,
} app_state_t;

static volatile bool is_recording = false;
static SemaphoreHandle_t s_mutex = NULL;

static void init_led(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void record_task(void *arg) {
  const size_t buffer_size = 1024;
  uint8_t *buffer = malloc(buffer_size);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer for recording");
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
  init_led();
  gpio_set_level(LED_PIN, 1); // LED starts off
  s_mutex = xSemaphoreCreateBinary();

  for (;;) {
    if (state == FINALISING) {
      ESP_LOGI(TAG, "Saving data...");
      xSemaphoreTake(s_mutex,
                     portMAX_DELAY); // Block until recording task finishes
      gpio_set_level(LED_PIN, 1); // Turn LED off
      ESP_ERROR_CHECK(wav_close());
      state = IDLE;
      ESP_LOGI(TAG, "Data saved. Returning to IDLE state.");
      continue;
    }

    EventBits_t uxBits = xEventGroupWaitBits(
        button_group, RECORD_BUTTON_BIT | POWER_BUTTON_BIT,
        pdTRUE,  /* Clear before returning. */
        pdFALSE, /* Don't wait for both bits, either bit will do. */
        portMAX_DELAY);

    if ((uxBits & RECORD_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Boot button pressed");
      if (state == IDLE) {
        state = RECORDING;
        ESP_ERROR_CHECK(wav_open("/sdcard/test.wav"));
        is_recording = true;
        gpio_set_level(LED_PIN, 0); // Turn on LED to indicate recording
        xTaskCreate(record_task, "record_task", 4096, NULL, 5, NULL);
      } else if (state == RECORDING) {
        state = FINALISING;
        is_recording = false;
      }
    } else if ((uxBits & POWER_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Power pressed...");
    }
  }
}
