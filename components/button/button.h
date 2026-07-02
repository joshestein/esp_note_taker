#ifndef BUTTON_H
#define BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BOOT_BUTTON_BIT (1 << 0)
#define POWER_BUTTON_BIT (1 << 1)

EventGroupHandle_t button_init(void);

#endif
