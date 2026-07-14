#ifndef SYNC_H
#define SYNC_H

#include "esp_err.h"

typedef enum {
  SYNC_PHASE_CONNECTING = 0,
  SYNC_PHASE_RESOLVING,
  SYNC_PHASE_UPLOADING,
  SYNC_PHASE_DOWNLOADING,
  SYNC_PHASE_DONE,
} sync_phase_t;

typedef enum {
  SYNC_OK = 0,
  SYNC_ERR_WIFI,         // could not join the network
  SYNC_ERR_NO_COMPANION, // mDNS did not resolve
  SYNC_ERR_UNAUTHORIZED, // 401: wrong token, permanent until reflash
  SYNC_ERR_INTERNAL,
} sync_error_t;

typedef struct {
  sync_phase_t phase;
  sync_error_t error;
  int uploaded;
  int downloaded;
  int failed; // per-file failures, skipped and left for the next sync
} sync_result_t;

esp_err_t sync_init(void);

// Spawns the sync task and returns immediately
esp_err_t sync_start(void);

// Snapshot of the task's progress
sync_result_t sync_get_result(void);

#endif
