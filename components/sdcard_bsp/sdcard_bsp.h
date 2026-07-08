#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H

#include "esp_err.h"

esp_err_t sdcard_init(void);
int sdcard_scan_max(void);
float sdcard_get_value(void);

#endif
