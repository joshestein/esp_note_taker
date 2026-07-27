#include "app_events.h"
#include "audio_bsp.h"
#include "battery_bsp.h"
#include "button_input.h"
#include "config.h"
#include "display_bsp.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "menu.h"
#include "sdcard_bsp.h"
#include "sync.h"
#include "wav_writer.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "main";

// Awake window at cold boot before light sleep engages
#define PM_BOOT_DELAY_MS 3000

// Generous, because a Menu-Button wake is a deliberate hold
#define BUTTON_RELEASE_TIMEOUT_MS 3000

#define POWER_OFF_SETTLE_MS 500

typedef enum {
  IDLE = 0,
  MAIN_MENU,
  SYNCING,
  RECORDING,
} app_state_t;

static volatile bool is_recording = false;
static volatile bool capture_ok = false;
static char path[32];

// How this boot began. A park resumes as ESP_RST_DEEPSLEEP; a wake from Off, a
// USB plug-in and a flash all come back as ESP_RST_POWERON. Anything else is a
// fault worth seeing.
static void log_boot_cause(uint64_t ext1) {
  const esp_reset_reason_t reset = esp_reset_reason();
  const uint32_t wake = esp_sleep_get_wakeup_causes();

  if (reset == ESP_RST_DEEPSLEEP) {
    ESP_LOGI(TAG, "Woke from park: wake=0x%" PRIx32 " record=%d menu=%d", wake,
             (ext1 & (1ULL << RECORD_BUTTON)) != 0,
             (ext1 & (1ULL << MENU_BUTTON)) != 0);
  } else if (reset == ESP_RST_POWERON) {
    ESP_LOGI(TAG, "Cold boot: rail came up");
  } else {
    ESP_LOGW(TAG, "Unexpected boot: reset=%d wake=0x%" PRIx32, (int)reset, wake);
  }
}

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

static esp_err_t init_power(void) {
  // Load the output latch high BEFORE enabling the output driver
  gpio_set_level(VBAT_PWR_PIN, 1);
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << VBAT_PWR_PIN),
      .mode = GPIO_MODE_OUTPUT,
      // Keeps T2's base driven through any window where the output driver is
      // off; ~57µA is still enough to hold Q4's gate down. As Waveshare do.
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    return err;
  }

  // Release the hold that carried the latch through Deep Sleep, only now that
  // the output is already driving high: earlier, the pad floats and cuts our
  // own power. No-op on a cold boot.
  return gpio_hold_dis(VBAT_PWR_PIN);
}

// Both buttons are active low, so a press still held at a sleep boundary is
// indistinguishable from a fresh one on the other side of it. Callers use this to
// put the boundary between the press and its release. Requires the pins to be
// readable already.
static void wait_buttons_released(void) {
  const int64_t deadline =
      esp_timer_get_time() + (int64_t)BUTTON_RELEASE_TIMEOUT_MS * 1000;
  while (esp_timer_get_time() < deadline) {
    if (gpio_get_level(RECORD_BUTTON) && gpio_get_level(MENU_BUTTON)) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  ESP_LOGW(TAG, "Button still held after %d ms", BUTTON_RELEASE_TIMEOUT_MS);
}

// A wake press is still down when the button driver starts, which reads its
// release as a fresh gesture: a Menu wake lands in the Menu instead of Idle, and
// a Record wake latches a press that stops the Capture it just started. Wait it
// out before button_init, configuring the pins ourselves since nothing owns them
// yet.
static void drain_wake_press(void) {
  const gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RECORD_BUTTON) | (1ULL << MENU_BUTTON),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE, // both pins pull up externally
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  wait_buttons_released();
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
  app_events_set(CAPTURE_ENDED_BIT);
  vTaskDelete(NULL);
}

// Opens the next WAV file, turns on the Recording Indicator, and spawns the
// record task. Returns true on success (caller should move to RECORDING) or
// false on any failure (state stays IDLE, nothing left dangling).
static bool start_capture(int *note_counter) {
  snprintf(path, sizeof(path), NOTE_FILENAME_FMT, *note_counter + 1);
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

// The sync task reports phases and counts; turn them into words. Keeps the
// sync component free of any notion that there is a screen.
static const char *sync_phase_text(sync_phase_t phase) {
  switch (phase) {
  case SYNC_PHASE_CONNECTING:
    return "Connecting...";
  case SYNC_PHASE_RESOLVING:
    return "Finding companion...";
  case SYNC_PHASE_UPLOADING:
    return "Uploading...";
  case SYNC_PHASE_DOWNLOADING:
    return "Downloading...";
  case SYNC_PHASE_DONE:
    return "Done";
  }
  return "";
}

// NULL for SYNC_OK: a successful sync has counts to format, not a fixed string.
static const char *sync_error_text(sync_error_t error) {
  switch (error) {
  case SYNC_ERR_WIFI:
    return "Sync failed:\nno Wi-Fi";
  case SYNC_ERR_NO_KNOWN_WIFI:
    return "Sync failed:\nno known Wi-Fi";
  case SYNC_ERR_NO_COMPANION:
    return "Sync failed:\nno companion";
  case SYNC_ERR_UNAUTHORIZED:
    return "Sync failed:\nunauthorized";
  case SYNC_ERR_INTERNAL:
    return "Sync failed";
  case SYNC_OK:
    return NULL;
  }
  return NULL;
}

static void sync_result_text(sync_result_t result, char *out, size_t len) {
  const char *error = sync_error_text(result.error);
  if (error != NULL) {
    strlcpy(out, error, len);
  } else if (result.failed > 0) {
    snprintf(out, len, "%d up, %d down,\n%d failed", result.uploaded,
             result.downloaded, result.failed);
  } else {
    snprintf(out, len, "Synced\n%d up, %d down", result.uploaded,
             result.downloaded);
  }
}

// Park the device in Deep Sleep. Wakes on either button, active low; the wake
// cause is read back on the next boot to decide Idle vs Capture. Does not
// return.
static void enter_deep_sleep(void) {
  ESP_LOGI(TAG, "Parking: entering deep sleep");

  // Off first: PM re-arms a timer wake-up before every light sleep, so leaving
  // it running would undo the clear below while the display refresh yields.
  const esp_pm_config_t pm_awake = {
      .max_freq_mhz = 160, .min_freq_mhz = 160, .light_sleep_enable = false};
  ESP_ERROR_CHECK(esp_pm_configure(&pm_awake));

  display_show_deep_sleep();

  // Triggers are global and the enable calls only OR bits in, so PM's leftover
  // millisecond timer would end the park instantly. Clear it, explicitly, first.
  ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));

  // Wake on either button (active low). _wakeup_io configures the RTC pulls
  // itself (idling the pins high, held through RTC_PERIPH power-down), so no
  // manual rtc_gpio setup is needed.
  const uint64_t wake_mask = (1ULL << RECORD_BUTTON) | (1ULL << MENU_BUTTON);
  ESP_ERROR_CHECK(
      esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW));

  // ANY_LOW fires on a pin that is *already* low, so the press that chose this
  // card would park and wake us in one breath. The card acts on PRESS_DOWN, so
  // that press is still held unless we wait it out. Safe to yield here: PM is
  // off above, so the idle task cannot re-arm what we just cleared.
  wait_buttons_released();

  // Freeze the VBAT latch, or the pad resets on the way in and the battery rail
  // collapses -- powering off rather than parking, recoverable only by the Menu
  // Button, which *is* the hardware power key. GPIO17 is RTC-capable so this
  // routes to rtc_gpio_hold_en; gpio_deep_sleep_hold_en would drag unrelated
  // digital pads (panel gate, LED, codec PA) along with it.
  ESP_ERROR_CHECK(gpio_hold_en(VBAT_PWR_PIN));

  esp_deep_sleep_start();
}

// Cut the board's own power by opening the VBAT latch. Only the Menu Button can
// revive the device afterwards, because that pin *is* the hardware power key.
// Does not return on battery. On USB it does: VBUS holds VSYS up through D4
// whatever GPIO17 does, so the latch is restored and the caller must cope.
static void power_off(void) {
  ESP_LOGI(TAG, "Powering off: opening VBAT latch");
  display_show_message("Off", true);

  // Driving low beats the pull-up init_power leaves on the pad, so T2 stops
  // conducting and Q4's gate rises to VBAT.
  gpio_set_level(VBAT_PWR_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(POWER_OFF_SETTLE_MS));

  gpio_set_level(VBAT_PWR_PIN, 1);
  ESP_LOGW(TAG, "Still powered: USB is feeding VSYS, latch restored");
}

void app_main(void) {
  app_state_t state = IDLE;

  ESP_ERROR_CHECK(init_power()); // holds the VBAT latch so battery survives USB unplug

  // Read once: this is the only record of why we woke, and it is what both the
  // boot log and the wake-to-record decision are built on. Reads 0 for any
  // non-EXT1 wake, so no separate wakeup-cause check is needed.
  const uint64_t ext1_status = esp_sleep_get_ext1_wakeup_status();
  log_boot_cause(ext1_status);

  // If we woke from Deep Sleep via the Record Button, honor the "instant
  // Capture" promise by starting to record as soon as the SD card and codec are
  // up. A Menu-Button wake (or a cold power-on) wakes to Idle.
  const bool wake_to_record = (ext1_status & (1ULL << RECORD_BUTTON)) != 0;

  drain_wake_press(); // before button_init, so the driver never sees the tail

  ESP_ERROR_CHECK(app_events_init()); // before any producer can raise a bit
  ESP_ERROR_CHECK(button_init());
  ESP_ERROR_CHECK(menu_init());
  ESP_ERROR_CHECK(sync_init());
  ESP_ERROR_CHECK(sdcard_init());
  int note_counter = sdcard_scan_max();
  const bool capture_ready = note_counter >= 0;
  ESP_ERROR_CHECK(audio_bsp_init());
  ESP_ERROR_CHECK(battery_init());
  ESP_ERROR_CHECK(init_led());
  gpio_set_level(LED_PIN, 1); // LED starts off

  // On a Record-Button wake, start the Capture immediately without waiting for
  // display initialisation
  if (wake_to_record && capture_ready && start_capture(&note_counter)) {
    state = RECORDING;
  }

  ESP_ERROR_CHECK(display_init(battery_level()));
  if (state == RECORDING) {
    display_show_recording();
  } else if (wake_to_record && !capture_ready) {
    display_show_message("SD scan failed", true);
  }

  esp_sleep_enable_gpio_switch(false); // Don't disable GPIO configs during sleep

  // Stay awake briefly before enabling light sleep. Once PM is on, Idle sleeps
  // within seconds and the USB-Serial-JTAG drops, so flash problems.
  if (!wake_to_record) {
    ESP_LOGI(TAG, "Boot flash window: enabling light sleep in %d ms",
             PM_BOOT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(PM_BOOT_DELAY_MS));
  }

  esp_pm_config_t light_sleep_config = {
      .max_freq_mhz = 160, .min_freq_mhz = 160, .light_sleep_enable = true};
  ESP_ERROR_CHECK(esp_pm_configure(&light_sleep_config));

  for (;;) {
    const EventBits_t uxBits = app_events_wait();

    const app_state_t arrived_in = state;

    // The state-changing events are each on their own `if`, never an `else if`
    // chain: app_events_wait clears every bit it returns, so a chain would
    // consume bits it never ran a branch for.
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
      display_show_idle(battery_level(), false);
    }

    if ((uxBits & SYNC_ENDED_BIT) && (state == SYNCING)) {
      char summary[48];
      sync_result_text(sync_get_result(), summary, sizeof(summary));
      // Full refresh: this screen persists until the next button press, and it
      // clears the ghosting left by the Menu's and the phases' partials.
      display_show_message(summary, true);
      state = IDLE;
    } else if ((uxBits & SYNC_PROGRESS_BIT) && (state == SYNCING)) {
      display_show_message(sync_phase_text(sync_get_result().phase), false);
    }

    if (((uxBits & MENU_EXIT_BIT) != 0 || (uxBits & MENU_TIMEOUT_BIT) != 0) &&
        (state == MAIN_MENU)) {
      menu_exit();
      display_show_idle(battery_level(), true);
      state = IDLE;
    }

    // A press that arrived in the same tick as one of the transitions above was
    // made while the old state still held, so it is not an instruction about
    // the state we just moved into. Drop it.
    if (state != arrived_in) {
      continue;
    }

    if ((uxBits & RECORD_BUTTON_BIT) != 0) {
      ESP_LOGI(TAG, "Record button pressed");
      if (state == IDLE) {
        if (!capture_ready) {
          display_show_message("SD scan failed", true);
        } else if (start_capture(&note_counter)) {
          state = RECORDING;
          display_show_recording();
        }
      } else if (state == RECORDING) {
        is_recording = false;
      } else if (state == MAIN_MENU) {
        switch (menu_act()) {
        case MENU_INTENT_SYNC:
          menu_exit();
          if (sync_start() == ESP_OK) {
            state = SYNCING;
          } else {
            ESP_LOGE(TAG, "Failed to start sync task");
            display_show_idle(battery_level(), true);
            state = IDLE;
          }
          break;
        case MENU_INTENT_SLEEP:
          ESP_LOGI(TAG, "Entering deep sleep");
          enter_deep_sleep();
          break;
        case MENU_INTENT_POWER_OFF:
          menu_exit();
          power_off(); // returns only on USB, where the rail is not ours to cut
          display_show_message("Unplug USB\nto power off", true);
          state = IDLE;
          break;
        case MENU_INTENT_NONE:
          ESP_LOGW(TAG, "No action for selected menu item");
          break;
        }
      }
      // SYNCING: press dropped (no sync cancel in v1).
    } else if ((uxBits & MENU_BUTTON_BIT) != 0) {
      if (state == IDLE) {
        state = MAIN_MENU;
        menu_enter();
      } else if (state == MAIN_MENU) {
        menu_step();
      }
    }
  }
}
