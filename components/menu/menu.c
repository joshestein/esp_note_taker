#include "menu.h"

#include "app_events.h"
#include "display_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "menu";

// ~30s of no button press closes the Menu.
#define MENU_TIMEOUT_US (30 * 1000 * 1000)

static const char *const CARD_LABELS[] = {
    "Sync",
    "Sleep",
};
#define CARD_COUNT ((int)(sizeof(CARD_LABELS) / sizeof(CARD_LABELS[0])))

static esp_timer_handle_t timeout_timer = NULL;
static int selection = 0;

// Runs on the esp_timer task, not an ISR.
// main.c blocks on the app event group, so the timeout arrives by the same path
// as a button press.
static void timeout_cb(void *arg) {
  ESP_LOGI(TAG, "Menu timed out");
  app_events_set(MENU_TIMEOUT_BIT);
}

static void restart_timeout(void) {
  esp_timer_stop(timeout_timer);
  esp_timer_start_once(timeout_timer, MENU_TIMEOUT_US);
}

esp_err_t menu_init(void) {
  const esp_timer_create_args_t timer_args = {
      .callback = &timeout_cb,
      .name = "menu_timeout",
  };
  return esp_timer_create(&timer_args, &timeout_timer);
}

void menu_enter(void) {
  selection = 0;
  display_show_menu(CARD_LABELS, CARD_COUNT, selection);
  restart_timeout();
}

void menu_step(void) {
  selection = (selection + 1) % CARD_COUNT;
  display_show_menu(CARD_LABELS, CARD_COUNT, selection);
  restart_timeout();
}

menu_intent_t menu_act(void) {
  esp_timer_stop(timeout_timer);

  switch (selection) {
  case 0:
    return MENU_INTENT_SYNC;
  case 1:
    return MENU_INTENT_SLEEP;
  default:
    ESP_LOGE(TAG, "No action for card %d", selection);
    return MENU_INTENT_NONE;
  }
}

void menu_exit(void) {
  esp_timer_stop(timeout_timer);
  app_events_clear(MENU_TIMEOUT_BIT);
}
