#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define RECORD_BUTTON_BIT (1 << 0)
#define POWER_BUTTON_BIT (1 << 1)

EventGroupHandle_t button_init(void);

#endif
