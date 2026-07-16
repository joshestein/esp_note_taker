#include "sdcard_bsp.h"
#include "config.h"
#include "dirent.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "sdmmc_cmd.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "sdcard_bsp";

#define SDMMC_D0_PIN GPIO_NUM_40
#define SDMMC_CLK_PIN GPIO_NUM_39
#define SDMMC_CMD_PIN GPIO_NUM_41

sdmmc_card_t *card_host = NULL;

static void ensure_dir(const char *path) {
  if (mkdir(path, 0777) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "Failed to create %s: %s", path, strerror(errno));
  }
}

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

  esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config,
                                          &mount_config, &card_host);
  if (err != ESP_OK) {
    return err;
  }

  ensure_dir(SYNCED_DIR);
  ensure_dir(TRANSCRIPTS_DIR);
  return ESP_OK;
}

// Highest note_NNNN found in `path`, 0 if the directory is empty, or -1 if the
// directory could not be opened. 
static int scan_dir_max(const char *path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    ESP_LOGE(TAG, "Failed to open %s to scan for existing recordings", path);
    return -1;
  }

  int max = 0;
  struct dirent *entry;
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

// Must scan the Synced folder too, not just the top level. Sync renames each
// uploaded Capture out of the top level and into SYNCED_DIR, so a top-level-only
// scan sees the counter fall back after every sync -- and the next Capture
// reuses a number that is already taken. The protocol is idempotent by filename,
// so re-uploading that name would silently overwrite a different memo on the
// Companion. Scanning both directories keeps the sequence monotonic.
// Returns -1 if either directory failed to open. A partial max is untrustworthy:
// the collapse-after-sync hazard lives in the SYNCED_DIR numbers, so a failed
// synced scan is as dangerous as a failed top-level scan. Fail closed and let the
// caller refuse to Capture rather than reuse a number.
int sdcard_scan_max(void) {
  int top = scan_dir_max(SD_MOUNT_POINT);
  int synced = scan_dir_max(SYNCED_DIR);
  if (top < 0 || synced < 0) {
    return -1;
  }
  return (top > synced) ? top : synced;
}

float sdcard_get_value(void) {
  if (card_host != NULL) {
    return (float)(card_host->csd.capacity) / 2048 / 1024; // G
  } else
    return 0;
}
