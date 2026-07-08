#include "audio_bsp.h"
#include "button_input.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "sdcard_bsp.h"
#include "wav_writer.h"
#include <stdio.h>

static const char *TAG = "main";

typedef enum {
  IDLE = 0,
  RECORDING,
  FINALISING,
} app_state_t;

static volatile bool is_recording = false;
static volatile bool capture_ok = false;
static SemaphoreHandle_t s_mutex = NULL;
static char path[32];
static EventGroupHandle_t button_group;

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
    xSemaphoreGive(s_mutex);
    xEventGroupSetBits(button_group, CAPTURE_ENDED_BIT);
    vTaskDelete(NULL);
    return;
  }

  bool audio_record_result = true;
  audio_bsp_record_start();
  while (is_recording) {
    esp_err_t audio_err = audio_bsp_record(buffer, buffer_size);
    if (audio_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to record audio: %s", esp_err_to_name(audio_err));
      audio_record_result = false;
      break;
    }

    esp_err_t write_err = wav_write(buffer, buffer_size);
    if (write_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to write to WAV file: %s",
               esp_err_to_name(write_err));
      audio_record_result = false;
      break;
    }
  }
  audio_bsp_record_stop();
  capture_ok = audio_record_result;

  free(buffer);
  xSemaphoreGive(s_mutex);
  xEventGroupSetBits(button_group, CAPTURE_ENDED_BIT);
  vTaskDelete(NULL);
}

void app_main(void) {
  app_state_t state = IDLE;
  button_group = button_init();

  ESP_ERROR_CHECK(sdcard_init());
  int note_counter = sdcard_scan_max();
  ESP_ERROR_CHECK(audio_bsp_init());
  init_led();
  gpio_set_level(LED_PIN, 1); // LED starts off

  s_mutex = xSemaphoreCreateBinary();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    abort();
  }

  for (;;) {
    if (state == FINALISING) {
      ESP_LOGI(TAG, "Saving data...");
      xSemaphoreTake(s_mutex,
                     portMAX_DELAY); // Block until recording task finishes
      gpio_set_level(LED_PIN, 1);    // Turn LED off

      ESP_ERROR_CHECK(wav_close());

      if (capture_ok) {
        ESP_LOGI(TAG, "Data saved successfully. Saved to %s", path);
      } else {
        ESP_LOGW(TAG, "Capture failed. Deleting file %s", path);
        remove(path);
      }

      state = IDLE;
      continue;
    }

    EventBits_t uxBits = xEventGroupWaitBits(
        button_group, RECORD_BUTTON_BIT | POWER_BUTTON_BIT | CAPTURE_ENDED_BIT,
        pdTRUE,  /* Clear before returning. */
        pdFALSE, /* Don't wait for both bits, either bit will do. */
        portMAX_DELAY);

    if ((uxBits & CAPTURE_ENDED_BIT) && (state == RECORDING)) {
      ESP_LOGI(TAG, "Recording ended");
      state = FINALISING;
      is_recording = false;
    } else if ((uxBits & RECORD_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Boot button pressed");
      if (state == IDLE) {
        state = RECORDING;
        ++note_counter;
        snprintf(path, sizeof(path), "/sdcard/note_%04d.wav", note_counter);
        ESP_ERROR_CHECK(wav_open(path));
        is_recording = true;
        capture_ok = false;
        gpio_set_level(LED_PIN, 0); // Turn on LED to indicate recording
        xTaskCreate(record_task, "record_task", 4096, NULL, 5, NULL);
      } else if (state == RECORDING) {
        is_recording = false;
      }
    } else if ((uxBits & POWER_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Power pressed...");
    }
  }
}
