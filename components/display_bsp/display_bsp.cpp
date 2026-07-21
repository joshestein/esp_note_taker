#include "display_bsp.h"

#include "config.h"
#include "epaper_driver_bsp.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display_bsp";

// LVGL port tunables (from the Waveshare 1.54" example user_config.h).
#define LVGL_TICK_PERIOD_MS 2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1

// The flush glue renders LVGL's RGB565 buffer and thresholds each pixel to
// black/white, so keep LV_COLOR_DEPTH at 16 to match this arithmetic.
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUFF_SIZE (EPD_WIDTH * EPD_HEIGHT * BYTES_PER_PIXEL)

static epaper_driver_display *driver = NULL;
static SemaphoreHandle_t lvgl_mux = NULL;

// Held so display_show_deep_sleep can force a synchronous refresh through it.
static lv_display_t *display = NULL;

// Screens built once at init and swapped with lv_screen_load, rather than
// rebuilt on every flip. Cheaper, and avoids reallocating objects on the flush
// path.
static lv_obj_t *idle_screen = NULL;
static lv_obj_t *recording_screen = NULL;
static lv_obj_t *deep_sleep_screen = NULL;

// The Battery ring's five segments, held on the (build-once) idle_screen so the
// level can be re-applied on each Idle repaint without rebuilding the screen.
#define BATTERY_SEGMENTS 5
#define RING_DIAMETER 190
#define RING_GAP_DEG 16
#define RING_START_DEG 270 // 12 o'clock (LVGL 0deg is 3 o'clock, +90 per quarter)
#define RING_WIDTH_EMPTY 2
#define RING_WIDTH_FULL 9
static lv_obj_t *battery_segments[BATTERY_SEGMENTS];

// The two exceptions to the build-once rule: the Menu's cards are rebuilt on
// every paint (the Selection moves) and the message screen's text changes. Held
// so the previous one can be freed after the new one is loaded.
static lv_obj_t *menu_screen = NULL;
static lv_obj_t *message_screen = NULL;

// Read and cleared by flush_cb. Set under the LVGL lock before the screen load
// that triggers the flush -- the flush itself runs later, on the LVGL task.
static bool full_refresh_pending = false;

static bool lvgl_lock(int timeout_ms) {
  TickType_t ticks =
      (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(lvgl_mux, ticks) == pdTRUE;
}

static void lvgl_unlock(void) { xSemaphoreGive(lvgl_mux); }

// LVGL v9 flush callback, lifted from 10_LVGL_V9_Test. Converts the rendered
// RGB565 area to 1-bit black/white and pushes it via a partial refresh.
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  uint16_t *buffer = (uint16_t *)px;
  driver->EPD_Clear();
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      uint8_t color = (*buffer < 0x7fff) ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE;
      driver->EPD_DrawColorPixel(x, y, color);
      buffer++;
    }
  }
  if (full_refresh_pending) {
    full_refresh_pending = false;
    // EPD_Init_Partial() left the panel on the partial LUT for good, so
    // EPD_DisplayPartBaseImage() alone would drive the full update sequence
    // through a partial waveform -- no flash, no ghost clear. Reload the full
    // LUT, push (writing both the new-image and old-image RAMs, which also
    // re-establishes the base the next partials diff against), then swap back.
    driver->EPD_Init();
    driver->EPD_DisplayPartBaseImage();
    driver->EPD_Init_Partial();
  } else {
    driver->EPD_DisplayPart();
  }
  lv_disp_flush_ready(disp);
}

static void tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

static void lvgl_task(void *arg) {
  uint32_t delay_ms = LVGL_TASK_MAX_DELAY_MS;
  for (;;) {
    if (lvgl_lock(-1)) {
      delay_ms = lv_timer_handler();
      lvgl_unlock();
    }
    if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
      delay_ms = LVGL_TASK_MAX_DELAY_MS;
    } else if (delay_ms < LVGL_TASK_MIN_DELAY_MS) {
      delay_ms = LVGL_TASK_MIN_DELAY_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

// --- Screen builders ---------------------------------------------------------
// The panel is 1-bit: flush_cb thresholds each RGB565 pixel to black/white, so
// everything here is black-on-white. Screens are built once and cached.

// Paint a screen's background solid white so thresholding yields a clean field.
static void set_white_background(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
}

// Build the five ring segments as background-only arcs (no knob, no value
// indicator) around the panel edge. Fill state is set later by
// apply_battery_level, which flips each segment's arc width.
static void build_battery_ring(lv_obj_t *scr) {
  for (int i = 0; i < BATTERY_SEGMENTS; i++) {
    lv_obj_t *seg = lv_arc_create(scr);
    lv_obj_set_size(seg, RING_DIAMETER, RING_DIAMETER);
    lv_obj_center(seg);
    lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(seg, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(seg, lv_color_black(), LV_PART_MAIN);
    // Only the background arc is drawn; kill the value indicator.
    lv_arc_set_value(seg, 0);
    lv_obj_set_style_arc_width(seg, 0, LV_PART_INDICATOR);

    // Rotate so segment 0 begins at 12 o'clock; rotation offsets the angles
    // without letting any segment's range wrap past 360.
    lv_arc_set_rotation(seg, RING_START_DEG);
    const int start = i * 360 / BATTERY_SEGMENTS + RING_GAP_DEG / 2;
    const int end = (i + 1) * 360 / BATTERY_SEGMENTS - RING_GAP_DEG / 2;
    lv_arc_set_bg_angles(seg, start, end);

    battery_segments[i] = seg;
  }
}

// Fill the first `level` segments thick, the rest thin. Both are black (the
// panel is 1-bit); thickness is the only cue, so count the fat segments.
static void apply_battery_level(int level) {
  if (level < 0) {
    level = 0;
  } else if (level > BATTERY_SEGMENTS) {
    level = BATTERY_SEGMENTS;
  }
  for (int i = 0; i < BATTERY_SEGMENTS; i++) {
    const int width = (i < level) ? RING_WIDTH_FULL : RING_WIDTH_EMPTY;
    lv_obj_set_style_arc_width(battery_segments[i], width, LV_PART_MAIN);
  }
}

static void build_idle_screen(int level) {
  idle_screen = lv_obj_create(NULL);
  set_white_background(idle_screen);

  build_battery_ring(idle_screen);
  apply_battery_level(level);

  lv_obj_t *label = lv_label_create(idle_screen);
  lv_label_set_text(label, "ready");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
}

// Rebuilt on every paint, since the text changes. Returns a detached screen; the
// caller loads it and frees the previous one.
static lv_obj_t *build_message_screen(const char *text) {
  lv_obj_t *scr = lv_obj_create(NULL);
  set_white_background(scr);
  lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *label = lv_label_create(scr);
  lv_label_set_text(label, text);
  // Wrap rather than run off the panel: a Sync result can be as long as
  // "3 up, 1 down, 2 failed".
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, EPD_WIDTH - 24);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);

  return scr;
}

// Unlike the Idle and Recording screens, this one is rebuilt on every paint --
// the Selection moves, so the fill/outline split changes. Returns a detached
// screen; the caller loads it and frees the previous one.
static lv_obj_t *build_menu_screen(const char *const *labels, int count,
                                   int selected) {
  lv_obj_t *scr = lv_obj_create(NULL);
  set_white_background(scr);
  lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_row(scr, 10, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < count; i++) {
    const bool is_selected = (i == selected);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 160, 44);
    lv_obj_set_style_radius(card, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        card, is_selected ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, is_selected ? 0 : 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, labels[i]);
    lv_obj_set_style_text_color(
        label, is_selected ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    lv_obj_center(label);
  }

  lv_obj_t *hint = lv_label_create(scr);
  lv_label_set_text(hint, "hold to exit");
  lv_obj_set_style_text_color(hint, lv_color_black(), LV_PART_MAIN);

  return scr;
}

static void build_recording_screen(void) {
  recording_screen = lv_obj_create(NULL);
  set_white_background(recording_screen);

  // Filled black circle, nudged up so the label has room beneath it.
  lv_obj_t *circle = lv_obj_create(recording_screen);
  lv_obj_set_size(circle, 80, 80);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(circle, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
  lv_obj_align(circle, LV_ALIGN_CENTER, 0, -24);

  lv_obj_t *label = lv_label_create(recording_screen);
  lv_label_set_text(label, "REC");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_align_to(label, circle, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
}

static void build_deep_sleep_screen(void) {
  deep_sleep_screen = lv_obj_create(NULL);
  set_white_background(deep_sleep_screen);

  lv_obj_t *label = lv_label_create(deep_sleep_screen);
  lv_label_set_text(label, "Zzz...");
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(label);
}

esp_err_t display_init(int initial_level) {
  // 1. Power the panel. EPD_PWR_PIN is active-low (LOW = on); without this the
  //    driver's BUSY wait never releases.
  gpio_config_t pwr = {};
  pwr.mode = GPIO_MODE_OUTPUT;
  pwr.pin_bit_mask = 1ULL << EPD_PWR_PIN;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&pwr));
  gpio_set_level((gpio_num_t)EPD_PWR_PIN, 0);

  // 2. Construct the driver and enter partial-refresh mode. The order matters:
  //    partial refresh (EPD_DisplayPart, used by flush_cb) diffs the new-image
  //    RAM against the panel's OLD-image RAM (cmd 0x26). Only
  //    EPD_DisplayPartBaseImage populates that old RAM, and it does so with the
  //    full waveform (EPD_TurnOnDisplay, 0xc7) -- giving both the clean boot
  //    flash and the base image. EPD_Init_Partial then swaps the LUT to the
  //    partial waveform, so it must run AFTER the base image is laid.
  custom_lcd_spi_t cfg = {
      .cs = EPD_CS_PIN,
      .dc = EPD_DC_PIN,
      .rst = EPD_RST_PIN,
      .busy = EPD_BUSY_PIN,
      .mosi = EPD_MOSI_PIN,
      .scl = EPD_SCK_PIN,
      .spi_host = EPD_SPI_NUM,
      .buffer_len = EPD_BUFFER_LEN,
  };
  driver = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, cfg);
  driver->EPD_Init();
  driver->EPD_Clear();
  driver->EPD_DisplayPartBaseImage();
  driver->EPD_Init_Partial();

  // 3. Bring up LVGL and register the display.
  lv_init();
  lv_display_t *disp = lv_display_create(EPD_WIDTH, EPD_HEIGHT);
  display = disp;
  lv_display_set_flush_cb(disp, flush_cb);

  uint8_t *buf = (uint8_t *)heap_caps_malloc(BUFF_SIZE, MALLOC_CAP_SPIRAM);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer");
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_buffers(disp, buf, NULL, BUFF_SIZE, LV_DISPLAY_RENDER_MODE_FULL);

  esp_timer_create_args_t tick_args = {};
  tick_args.callback = &tick_cb;
  tick_args.name = "lvgl_tick";
  esp_timer_handle_t tick_timer = NULL;
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_create(&tick_args, &tick_timer));
  ESP_ERROR_CHECK_WITHOUT_ABORT(
      esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

  lvgl_mux = xSemaphoreCreateMutex();
  if (lvgl_mux == NULL) {
    ESP_LOGE(TAG, "Failed to create LVGL mutex");
    return ESP_FAIL;
  }
  xTaskCreatePinnedToCore(lvgl_task, "LVGL", 8 * 1024, NULL, 4, NULL, 1);

  if (lvgl_lock(-1)) {
    build_idle_screen(initial_level);
    build_recording_screen();
    build_deep_sleep_screen();
    lv_screen_load(idle_screen);
    lvgl_unlock();
  }
  return ESP_OK;
}

// The one blocking paint. Every other display_show_* hands the screen to LVGL
// and returns, leaving the flush to the LVGL task -- fine, because something
// else is always coming. Nothing is coming after this one: the caller cuts power
// as soon as it returns, so an async flush would park the panel half-written,
// and that garbage is what persists (e-paper holds its image with no power).
// lv_refr_now renders and runs flush_cb on THIS task, and flush_cb blocks on the
// panel's BUSY line, so this returns only once the panel has finished updating.
// Full refresh, because there is no next refresh to clean up partial ghosting.
void display_show_deep_sleep(void) {
  if (lvgl_lock(-1)) {
    full_refresh_pending = true;
    lv_screen_load(deep_sleep_screen);
    lv_refr_now(display);
    lvgl_unlock();
  }
}

void display_show_recording(void) {
  if (lvgl_lock(-1)) {
    full_refresh_pending = false;
    lv_screen_load(recording_screen);
    lvgl_unlock();
  }
}

void display_show_idle(int level, bool full_refresh) {
  if (lvgl_lock(-1)) {
    apply_battery_level(level);
    full_refresh_pending = full_refresh;
    lv_screen_load(idle_screen);
    lv_obj_invalidate(idle_screen);
    lvgl_unlock();
  }
}

void display_show_message(const char *text, bool full_refresh) {
  if (lvgl_lock(-1)) {
    full_refresh_pending = full_refresh;
    lv_obj_t *previous = message_screen;
    message_screen = build_message_screen(text);
    lv_screen_load(message_screen);
    if (previous != NULL) {
      lv_obj_delete(previous);
    }
    lvgl_unlock();
  }
}

void display_show_menu(const char *const *labels, int count, int selected) {
  if (lvgl_lock(-1)) {
    full_refresh_pending = false;
    // Load the new screen before freeing the old one -- the old one is still
    // the active screen until lv_screen_load returns.
    lv_obj_t *previous = menu_screen;
    menu_screen = build_menu_screen(labels, count, selected);
    lv_screen_load(menu_screen);
    if (previous != NULL) {
      lv_obj_delete(previous);
    }
    lvgl_unlock();
  }
}
