#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define RECORD_BUTTON_BIT (1 << 0)
#define MENU_BUTTON_BIT (1 << 1)   // Menu Button short press (navigation)
#define CAPTURE_ENDED_BIT (1 << 2) // not set by a button, used by the recording task when it finishes
#define MENU_EXIT_BIT (1 << 3)     // Menu Button long press ~1s: exit Menu toward Idle
#define MENU_TIMEOUT_BIT (1 << 4)  // not set by a button, set by the menu's inactivity timer
#define SYNC_PROGRESS_BIT (1 << 5) // not set by a button, set by the sync task on each phase change
#define SYNC_ENDED_BIT (1 << 6)    // not set by a button, set by the sync task when it finishes

esp_err_t button_init(EventGroupHandle_t *out_button_group);

#endif
