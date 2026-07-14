#include "app_events.h"

static EventGroupHandle_t group = NULL;

esp_err_t app_events_init(void) {
  group = xEventGroupCreate();
  return (group != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_events_set(EventBits_t bits) { xEventGroupSetBits(group, bits); }

void app_events_clear(EventBits_t bits) { xEventGroupClearBits(group, bits); }

EventBits_t app_events_wait(void) {
  return xEventGroupWaitBits(group, APP_EVENT_BITS,
                             pdTRUE,  /* Clear before returning. */
                             pdFALSE, /* Any bit will do, not all of them. */
                             portMAX_DELAY);
}
