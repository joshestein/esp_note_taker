#include "sync.h"

#include "button_input.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_secrets.h"
#include <string.h>

static const char *TAG = "sync";

#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_MAX_RETRIES 3

// Local to the Wi-Fi bring-up. Distinct from the app's button_group: these bits
// are an implementation detail of connecting, not something main.c reacts to.
#define WIFI_GOT_IP_BIT (1 << 0)
#define WIFI_FAILED_BIT (1 << 1)

static EventGroupHandle_t button_group = NULL;
static EventGroupHandle_t wifi_events = NULL;
static sync_result_t result;

// esp_netif_init() and the default event loop cannot be reliably torn down, so
// the one-time scaffolding is done once and kept. esp_wifi_init/deinit cycles
// per sync.
static bool netif_ready = false;
static int retries = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (retries < WIFI_MAX_RETRIES) {
      retries++;
      ESP_LOGW(TAG, "Disconnected, retry %d/%d", retries, WIFI_MAX_RETRIES);
      esp_wifi_connect();
    } else {
      xEventGroupSetBits(wifi_events, WIFI_FAILED_BIT);
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_events, WIFI_GOT_IP_BIT);
  }
}

// One-time: NVS, TCP/IP stack, event loop, default STA netif, event handlers.
// Runs on the first sync, not at boot.
static esp_err_t netif_init_once(void) {
  if (netif_ready) {
    return ESP_OK;
  }

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    return err;
  }

  err = esp_netif_init();
  if (err != ESP_OK) {
    return err;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    return err;
  }
  esp_netif_create_default_wifi_sta();

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL, NULL);
  if (err != ESP_OK) {
    return err;
  }
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler, NULL, NULL);
  if (err != ESP_OK) {
    return err;
  }

  netif_ready = true;
  return ESP_OK;
}

// Joins the network, or gives up. Bounded: never hangs the sync task.
static esp_err_t wifi_connect(void) {
  esp_err_t err = netif_init_once();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(err));
    return err;
  }

  retries = 0;
  xEventGroupClearBits(wifi_events, WIFI_GOT_IP_BIT | WIFI_FAILED_BIT);

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK) {
    return err;
  }

  wifi_config_t wifi_cfg = {};
  strlcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
  strlcpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD,
          sizeof(wifi_cfg.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

  // Powers the radio.
  err = esp_wifi_start();
  if (err != ESP_OK) {
    esp_wifi_deinit();
    return err;
  }

  EventBits_t bits = xEventGroupWaitBits(
      wifi_events, WIFI_GOT_IP_BIT | WIFI_FAILED_BIT, pdTRUE, pdFALSE,
      pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

  if ((bits & WIFI_GOT_IP_BIT) == 0) {
    ESP_LOGE(TAG, "Wi-Fi connect failed (%s)",
             (bits & WIFI_FAILED_BIT) ? "gave up" : "timed out");
    return ESP_FAIL;
  }
  return ESP_OK;
}

// Radio off. Leaves the netif and event loop standing (see netif_init_once).
static void wifi_disconnect(void) {
  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  ESP_LOGI(TAG, "Radio off");
}

static void set_phase(sync_phase_t phase) {
  result.phase = phase;
  xEventGroupSetBits(button_group, SYNC_PROGRESS_BIT);
}

static void sync_task(void *arg) {
  result = (sync_result_t){.phase = SYNC_PHASE_CONNECTING, .error = SYNC_OK};

  set_phase(SYNC_PHASE_CONNECTING);
  if (wifi_connect() != ESP_OK) {
    result.error = SYNC_ERR_WIFI;
  }

  // TODO: mDNS resolve, upload phase, download phase.

  wifi_disconnect();

  result.phase = SYNC_PHASE_DONE;
  xEventGroupSetBits(button_group, SYNC_ENDED_BIT);
  vTaskDelete(NULL);
}

esp_err_t sync_init(EventGroupHandle_t group) {
  button_group = group;
  wifi_events = xEventGroupCreate();
  return (wifi_events != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sync_start(void) {
  BaseType_t err = xTaskCreate(sync_task, "sync_task", 8192, NULL, 5, NULL);
  return (err == pdPASS) ? ESP_OK : ESP_FAIL;
}

sync_result_t sync_get_result(void) { return result; }
