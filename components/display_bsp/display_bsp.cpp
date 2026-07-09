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
  driver->EPD_DisplayPart();
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
// Left as stubs on purpose. Fill with the actual LVGL widgets (a filled circle
// via LV_RADIUS_CIRCLE + an lv_label "recording" for the record screen). Decide
// whether to rebuild objects each call or pre-build two screens and swap them
// with lv_screen_load (cheaper per flip).

static void build_recording_screen(void) {
  // TODO: filled circle + "recording" label.
}

static void build_idle_screen(void) {
  // TODO: minimal placeholder (blank or "ready"). Battery glyph is deferred.
}

esp_err_t display_init(void) {
  // 1. Power the panel. EPD_PWR_PIN is active-low (LOW = on); without this the
  //    driver's BUSY wait never releases.
  gpio_config_t pwr = {};
  pwr.mode = GPIO_MODE_OUTPUT;
  pwr.pin_bit_mask = 1ULL << EPD_PWR_PIN;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&pwr));
  gpio_set_level((gpio_num_t)EPD_PWR_PIN, 0);

  // 2. Construct the driver and do one FULL refresh at boot (clears power-off
  //    ghosting and establishes the base image for later partial refreshes).
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
  driver->EPD_Display();
  // TODO: switch into partial mode so subsequent Idle<->Recording flips are
  // fast and flash-free:
  //   driver->EPD_Init_Partial();
  //   driver->EPD_DisplayPartBaseImage();

  // 3. Bring up LVGL and register the display.
  lv_init();
  lv_display_t *disp = lv_display_create(EPD_WIDTH, EPD_HEIGHT);
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
    build_idle_screen();
    lvgl_unlock();
  }
  return ESP_OK;
}

void display_show_recording(void) {
  if (lvgl_lock(-1)) {
    build_recording_screen();
    lvgl_unlock();
  }
}

void display_show_idle(void) {
  if (lvgl_lock(-1)) {
    build_idle_screen();
    lvgl_unlock();
  }
}
