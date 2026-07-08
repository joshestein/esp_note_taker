#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

static FILE *wav_file = NULL;
static uint32_t bytes_written = 0;

static const uint16_t bits = 16; // Bits per sample
static const uint16_t num_channels = 1; // Mono
static const uint32_t sample_rate = 16000;
static const uint32_t byte_rate = sample_rate * bits * num_channels / 8;
static const uint16_t block_align = num_channels * bits / 8;

esp_err_t wav_open(const char *path) {
  wav_file = fopen(path, "wb");
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
  fwrite(&byte_rate, 1, 4, wav_file);
  fwrite(&block_align, 1, 2, wav_file);
  fwrite(&bits, 1, 2, wav_file);
  fwrite("data", 1, 4, wav_file);
  fwrite(&placeholder, 1, 4, wav_file); // Placeholder for Subchunk2Size

  if (ferror(wav_file)) {
    fclose(wav_file);
    wav_file = NULL;
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t wav_write(const void *data, size_t len) {
    if (wav_file == NULL) return ESP_ERR_INVALID_STATE;

    size_t bytes_attempted_written = fwrite(data, 1, len, wav_file);
    if (bytes_attempted_written != len) {
        return ESP_FAIL;
    }

    bytes_written += len;
    return ESP_OK;
}

esp_err_t wav_close(void) {
    if (wav_file == NULL) return ESP_ERR_INVALID_STATE;

    fseek(wav_file, 4, SEEK_SET);
    // Total file size - 8 bytes (RIFF header and size field) = 44 - 8 + bytes_written = 36 + bytes_written
    uint32_t chunk_size = 36 + bytes_written;
    fwrite(&chunk_size, 1, 4, wav_file);

    // Total data size = bytes_written
    fseek(wav_file, 40, SEEK_SET);
    fwrite(&bytes_written, 1, 4, wav_file);

    fclose(wav_file);

    // Reset for next file
    wav_file = NULL;
    bytes_written = 0;

    return ESP_OK;
}
