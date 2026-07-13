#ifndef DISPLAY_BSP_H
#define DISPLAY_BSP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Powers the panel (EPD_PWR_PIN LOW), inits the driver with a full refresh,
// starts LVGL and its handler task, and paints the initial Idle screen.
// Best-effort: the caller must NOT abort a Capture on failure -- the LED
// Recording Indicator is the primary tell (see CONTEXT.md, ADR 0005).
esp_err_t display_init(void);

// Paint the Recording Screen (filled circle + "recording"), partial refresh.
void display_show_recording(void);

// Paint the minimal Idle screen. Defaults to a partial refresh, but a full refresh can be requested
void display_show_idle(bool full_refresh);

// Paint the Menu: every label as a card, the one at `selected` filled, the rest
// outlined. The caller owns the labels and their order.
void display_show_menu(const char *const *labels, int count, int selected);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_BSP_H
