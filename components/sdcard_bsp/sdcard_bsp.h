#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H

#include "esp_err.h"

esp_err_t sdcard_init(void);
float sdcard_GetValue(void);

#endif
