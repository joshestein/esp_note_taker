#ifndef AUDIO_BSP_H
#define AUDIO_BSP_H

#include "esp_err.h"

esp_err_t audio_bsp_init(void);
esp_err_t audio_bsp_record_start(void);
esp_err_t audio_bsp_record_stop(void);
esp_err_t audio_bsp_record(void *data, size_t len);

#endif
