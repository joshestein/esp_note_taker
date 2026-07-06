#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

static FILE *wav_file = NULL;
static const uint16_t bits = 16; // Bits per sample
static const uint16_t num_channels = 1; // Mono
static const uint32_t sample_rate = 16000;

esp_err_t wav_open(const char *path) {
  wav_file = fopen(path, "w");
  if (wav_file == NULL) {
    return ESP_FAIL;
  }

  fwrite("RIFF", 1, 4, wav_file);
  uint32_t placeholder = 0;
  fwrite(&placeholder, 1, 4, wav_file); // Placeholder for ChunkSize
  fwrite("WAVE", 1, 4, wav_file);
  fwrite("fmt ", 1, 4, wav_file);
  uint32_t subchunk1_size = 16; // Subchunk1Size for PCM is 16
  fwrite(&subchunk1_size, 1, 4, wav_file);
  uint16_t audio_format = 1; // AudioFormat for PCM
  fwrite(&audio_format, 1, 2, wav_file);
  fwrite(&num_channels, 1, 2, wav_file);
  fwrite(&sample_rate, 1, 4, wav_file);
  uint32_t byte_rate = sample_rate * bits * num_channels / 8;
  fwrite(&byte_rate, 1, 4, wav_file);
  uint16_t block_align = num_channels * bits / 8;
  fwrite(&block_align, 1, 2, wav_file);
  fwrite(&bits, 1, 2, wav_file);
  fwrite("data", 1, 4, wav_file);
  fwrite(&placeholder, 1, 4, wav_file); // Placeholder for Subchunk2Size
  return ESP_OK;
}

esp_err_t wav_write(const void *data, size_t len) {}

esp_err_t wav_close(void) {}
