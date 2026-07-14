#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include "esp_err.h"

// Wires both buttons to the app event group. The bits they raise are declared in
// app_events.h, alongside every other event source.
esp_err_t button_init(void);

#endif
