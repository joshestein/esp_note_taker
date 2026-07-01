#ifndef BUTTON_H
#define BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

EventGroupHandle_t button_init(void);

#endif
