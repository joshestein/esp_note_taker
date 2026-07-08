#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define RECORD_BUTTON_BIT (1 << 0)
#define POWER_BUTTON_BIT (1 << 1)
#define CAPTURE_ENDED_BIT (1 << 2) // not set by a button, used by the recording task when it finishes

esp_err_t button_init(EventGroupHandle_t *out_button_group);

#endif
