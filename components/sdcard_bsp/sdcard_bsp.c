#include "sdcard_bsp.h"
#include "dirent.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard_bsp";

#define SDMMC_D0_PIN GPIO_NUM_40
#define SDMMC_CLK_PIN GPIO_NUM_39
#define SDMMC_CMD_PIN GPIO_NUM_41

#define SDlist "/sdcard" // Directory, similar to a standard

sdmmc_card_t *card_host = NULL;

esp_err_t sdcard_init(void) {
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024 * 3;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  slot_config.clk = SDMMC_CLK_PIN;
  slot_config.cmd = SDMMC_CMD_PIN;
  slot_config.d0 = SDMMC_D0_PIN;

  return esp_vfs_fat_sdmmc_mount(SDlist, &host, &slot_config, &mount_config,
                                 &card_host);
}

int sdcard_scan_max(void) {
  DIR *dir = opendir(SDlist);
  struct dirent *entry;
  int max = 0;

  if (dir == NULL) {
    ESP_LOGE(TAG, "Failed to open %s to scan for existing recordings", SDlist);
    abort();
  }

  while ((entry = readdir(dir)) != NULL) {
    int num;
    if (sscanf(entry->d_name, "note_%d.wav", &num) == 1) {
      if (num > max) {
        max = num;
      }
    }
  }

  closedir(dir);
  return max;
}

float sdcard_GetValue(void) {
  if (card_host != NULL) {
    return (float)(card_host->csd.capacity) / 2048 / 1024; // G
  } else
    return 0;
}
