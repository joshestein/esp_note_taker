#include "audio_bsp.h"
#include "button_input.h"
#include "config.h"
#include "display_bsp.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/idf_additions.h"
#include "menu.h"
#include "sdcard_bsp.h"
#include "wav_writer.h"
#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "main";

typedef enum {
  IDLE = 0,
  RECORDING,
  FINALISING,
} app_state_t;

static volatile bool is_recording = false;
static volatile bool capture_ok = false;
static char path[32];
static EventGroupHandle_t button_group;

static esp_err_t init_led(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << LED_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&io_conf);
}

static void record_task(void *arg) {
  const size_t buffer_size = 1024;
  uint8_t *buffer = malloc(buffer_size);

  if (buffer != NULL) {
    bool audio_record_result = true;
    esp_err_t record_start_err = audio_bsp_record_start();
    if (record_start_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to start audio recording: %s",
               esp_err_to_name(record_start_err));
      audio_record_result = false;
    }

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
  }

  // This must be the last thing we do in this task, because the main task is
  // waiting for this bit to be set before it can proceed to finalise the
  // recording.
  xEventGroupSetBits(button_group, CAPTURE_ENDED_BIT);
  vTaskDelete(NULL);
}

// Opens the next WAV file, turns on the Recording Indicator, and spawns the
// record task. Returns true on success (caller should move to RECORDING) or
// false on any failure (state stays IDLE, nothing left dangling).
static bool start_capture(int *note_counter) {
  snprintf(path, sizeof(path), "/sdcard/note_%04d.wav", *note_counter + 1);
  esp_err_t open_err = wav_open(path);
  if (open_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open WAV file: %s", esp_err_to_name(open_err));
    return false;
  }

  is_recording = true;
  capture_ok = false;
  gpio_set_level(LED_PIN, 0); // Turn on LED to indicate recording
  BaseType_t task_create_err =
      xTaskCreate(record_task, "record_task", 4096, NULL, 5, NULL);
  if (task_create_err != pdPASS) {
    // If task creation fails, clean up and reset state
    ESP_LOGE(TAG, "Failed to create record task");
    wav_close();
    remove(path);
    is_recording = false;
    gpio_set_level(LED_PIN, 1); // Turn off LED
    return false;
  }

  ++(*note_counter);
  return true;
}

// Park the device in Deep Sleep. Wakes on either button, active low; the wake
// cause is read back on the next boot to decide Idle vs Capture. Does not
// return.
static void enter_deep_sleep(void) {
  ESP_LOGI(TAG, "Parking: entering deep sleep");
  // Wake on either button (active low). _wakeup_io configures the RTC pulls
  // itself (idling the pins high, held through RTC_PERIPH power-down), so no
  // manual rtc_gpio setup is needed.
  const uint64_t wake_mask = (1ULL << RECORD_BUTTON) | (1ULL << MENU_BUTTON);
  ESP_ERROR_CHECK(
      esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW));
  esp_deep_sleep_start();
}

void app_main(void) {
  app_state_t state = IDLE;

  // If we woke from Deep Sleep via the Record Button, honor the "instant
  // Capture" promise by starting to record as soon as the SD card and codec are
  // up. A Menu-Button wake (or a cold power-on) wakes to Idle. The status call
  // returns 0 for any non-EXT1 wake, so no separate wakeup-cause check needed.
  bool wake_to_record =
      (esp_sleep_get_ext1_wakeup_status() & (1ULL << RECORD_BUTTON)) != 0;

  ESP_ERROR_CHECK(button_init(&button_group));
  ESP_ERROR_CHECK(menu_init(button_group));
  ESP_ERROR_CHECK(sdcard_init());
  int note_counter = sdcard_scan_max();
  ESP_ERROR_CHECK(audio_bsp_init());
  ESP_ERROR_CHECK(init_led());
  gpio_set_level(LED_PIN, 1); // LED starts off

  // On a Record-Button wake, start the Capture immediately without waiting for
  // display initialisation
  if (wake_to_record && start_capture(&note_counter)) {
    state = RECORDING;
  }

  // Best-effort: a dead panel must not abort captures. The LED is the primary
  // recording tell (ADR 0005), so log and carry on if the display fails.
  esp_err_t display_err = display_init();
  if (display_err != ESP_OK) {
    ESP_LOGW(TAG, "Display init failed: %s", esp_err_to_name(display_err));
  }
  if (state == RECORDING) {
    display_show_recording();
  }

  for (;;) {
    EventBits_t uxBits = xEventGroupWaitBits(
        button_group,
        RECORD_BUTTON_BIT | MENU_BUTTON_BIT | MENU_EXIT_BIT | CAPTURE_ENDED_BIT | MENU_TIMEOUT_BIT,
        pdTRUE,  /* Clear before returning. */
        pdFALSE, /* Don't wait for both bits, either bit will do. */
        portMAX_DELAY);

    if ((uxBits & CAPTURE_ENDED_BIT) && (state == RECORDING)) {
      is_recording = false;

      ESP_LOGI(TAG, "Saving data...");
      gpio_set_level(LED_PIN, 1); // Turn LED off

      esp_err_t close_err = wav_close();
      if (close_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to close WAV file: %s",
                 esp_err_to_name(close_err));
      }

      if (capture_ok) {
        ESP_LOGI(TAG, "Data saved successfully. Saved to %s", path);
      } else {
        ESP_LOGW(TAG, "Capture failed. Deleting file %s", path);
        remove(path);
      }

      state = IDLE;
      display_show_idle(false);
    } else if ((uxBits & RECORD_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Record button pressed");
      if (state == IDLE) {
        if (start_capture(&note_counter)) {
          state = RECORDING;
          display_show_recording();
        }
      } else if (state == RECORDING) {
        is_recording = false;
      }
      // FINALISING: press dropped, no queued restart (see CONTEXT.md).
    } else if ((uxBits & MENU_EXIT_BIT) != 0) {
      // TODO: exit Menu toward Idle once Menu mode exists.
      ESP_LOGI(TAG, "Menu long-press (exit) - Menu mode not yet implemented");
    } else if ((uxBits & MENU_BUTTON_BIT) != 0) {
      // TODO: enter Menu from Idle / step to next card once Menu mode exists.
      ESP_LOGI(TAG, "Menu short-press - Menu mode not yet implemented");
    }
  }
}
