#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#include "esp_err.h"

esp_err_t wav_open(const char *path);
esp_err_t wav_write(const void *data, size_t len);
esp_err_t wav_close(void);

#endif
