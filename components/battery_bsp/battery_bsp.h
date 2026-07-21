#ifndef BATTERY_BSP_H
#define BATTERY_BSP_H

#include "esp_err.h"

// Coarse battery level, read as the LiPo terminal voltage on ADC1 channel 3
// (GPIO4, 1:2 divider). The ordinal doubles as the count of filled ring
// segments: UNKNOWN=0 (all outline) through FULL=5. Keep this order.
typedef enum {
  BATTERY_LEVEL_UNKNOWN = 0,
  BATTERY_LEVEL_CRITICAL,
  BATTERY_LEVEL_LOW,
  BATTERY_LEVEL_MEDIUM,
  BATTERY_LEVEL_HIGH,
  BATTERY_LEVEL_FULL,
} battery_level_t;

// Set up the ADC oneshot unit + calibration. Call once at boot, before
// battery_level().
esp_err_t battery_init(void);

// Sample the battery and map it to a level. Averages several reads to kill
// jitter; on a read error returns the last successful level (UNKNOWN before the
// first success), never a false Critical. Not live: caller decides when to
// sample (see CONTEXT.md "Battery ring").
battery_level_t battery_level(void);

#endif

