#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// The bits live here, not in any one producer's header, so that naming an event
// never costs a dependency on an unrelated driver.
#define RECORD_BUTTON_BIT (1 << 0) // Record Button, short press
#define MENU_BUTTON_BIT (1 << 1)   // Menu Button, short press: step the Selection
#define MENU_EXIT_BIT (1 << 2)     // Menu Button, long press ~1s: exit Menu toward Idle
#define MENU_TIMEOUT_BIT (1 << 3)  // the Menu's inactivity timer fired
#define CAPTURE_ENDED_BIT (1 << 4) // the record task has stopped
#define SYNC_PROGRESS_BIT (1 << 5) // the sync task entered a new phase
#define SYNC_ENDED_BIT (1 << 6)    // the sync task finished

#define APP_EVENT_BITS                                                          \
  (RECORD_BUTTON_BIT | MENU_BUTTON_BIT | MENU_EXIT_BIT | MENU_TIMEOUT_BIT |     \
   CAPTURE_ENDED_BIT | SYNC_PROGRESS_BIT | SYNC_ENDED_BIT)

// Must run before any producer is initialised.
esp_err_t app_events_init(void);

void app_events_set(EventBits_t bits);
void app_events_clear(EventBits_t bits);

// Blocks until at least one event bit is set, clears the bits it returns, and
// returns them. The only consumer is main.c's loop.
EventBits_t app_events_wait(void);

#endif
