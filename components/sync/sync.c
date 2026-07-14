#include "sync.h"
#include "app_events.h"
#include "config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "wifi_secrets.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sync";

#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_MAX_RETRIES 3
#define MDNS_RESOLVE_TIMEOUT_MS 3000
#define HTTP_TIMEOUT_MS 10000

// Streamed from SD straight to the socket, so RAM stays flat regardless of how
// long the memo is (ADR 0006).
#define UPLOAD_CHUNK_SIZE 4096

#define MAX_NAME_LEN 32
// Captures uploaded per session. Anything beyond this goes on the next sync --
// free, because the protocol is a resumable sequence of per-file commits.
#define MAX_PER_SYNC 64

// Local to the Wi-Fi bring-up. Distinct from the app's button_group: these bits
// are an implementation detail of connecting, not something main.c reacts to.
#define WIFI_GOT_IP_BIT (1 << 0)
#define WIFI_FAILED_BIT (1 << 1)

static EventGroupHandle_t wifi_events = NULL;
static sync_result_t result;

// esp_netif_init() and the default event loop cannot be reliably torn down, so
// the one-time scaffolding is done once and kept. esp_wifi_init/deinit cycles
// per sync.
static bool netif_ready = false;
static int retries = 0;

// Set before tearing the radio down. Without this the handler
// reconnects into the stop we are in the middle of performing.
static volatile bool explicit_stop_requested = false;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (explicit_stop_requested) {
      return; // we asked for this one
    }
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
  ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
  esp_netif_create_default_wifi_sta();

  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          &wifi_event_handler, NULL, NULL),
      TAG, "wifi handler");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                          &wifi_event_handler, NULL, NULL),
      TAG, "ip handler");

  netif_ready = true;
  return ESP_OK;
}

// Joins the network, or gives up. Bounded: never hangs the sync task.
static esp_err_t wifi_connect(void) {
  ESP_RETURN_ON_ERROR(netif_init_once(), TAG, "netif init");

  retries = 0;
  explicit_stop_requested = false;
  xEventGroupClearBits(wifi_events, WIFI_GOT_IP_BIT | WIFI_FAILED_BIT);

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

  wifi_config_t wifi_cfg = {};
  strlcpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
  strlcpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD,
          sizeof(wifi_cfg.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

  // Powers the radio.
  esp_err_t err = esp_wifi_start();
  if (err != ESP_OK) {
    esp_wifi_deinit();
    ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
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
// esp_wifi_stop() disconnects on its own, so no explicit disconnect first.
static void wifi_disconnect(void) {
  explicit_stop_requested = true;
  esp_wifi_stop();
  esp_wifi_deinit();
  ESP_LOGI(TAG, "Radio off");
}

// Resolve the Companion's per-owner mDNS hostname to an address, bounded. The
// hostname is hardcoded (wifi_secrets.h).
// Writes the dotted-quad into `out` for the HTTP client to use as the host.
static esp_err_t resolve_companion(char *out, size_t len) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    return err;
  }

  esp_ip4_addr_t addr = {};
  err = mdns_query_a(COMPANION_HOSTNAME, MDNS_RESOLVE_TIMEOUT_MS, &addr);
  mdns_free();

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not resolve %s.local: %s", COMPANION_HOSTNAME,
             esp_err_to_name(err));
    return err;
  }

  snprintf(out, len, IPSTR, IP2STR(&addr));
  ESP_LOGI(TAG, "Companion %s.local is at %s", COMPANION_HOSTNAME, out);
  return ESP_OK;
}

static void set_phase(sync_phase_t phase) {
  result.phase = phase;
  app_events_set(SYNC_PROGRESS_BIT);
}

// --- Upload phase ------------------------------------------------------------

// One sync task, never re-entrant, so these are static rather than malloc'd.
// The cap is free: sync is a resumable sequence of independent per-file
// commits, so anything past MAX_PER_SYNC simply goes on the next sync.
typedef char capture_name_t[MAX_NAME_LEN];
static capture_name_t names[MAX_PER_SYNC];
static uint8_t chunk[UPLOAD_CHUNK_SIZE];

static int compare_names(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

// Unsynced Captures are the *.wav still at the top level: sync moves each one
// into SYNCED_DIR on a 200, so presence at the top level IS the unsynced flag.
// No index, no state file, nothing to corrupt.
//
// Sorted so uploads go oldest-first (note_NNNN is zero-padded, so lexical order
// is chronological).
static int list_unsynced(void) {
  DIR *dir = opendir(SD_MOUNT_POINT);
  if (dir == NULL) {
    ESP_LOGE(TAG, "Cannot open %s", SD_MOUNT_POINT);
    return 0;
  }

  int count = 0;
  struct dirent *entry;
  while (count < MAX_PER_SYNC && (entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_DIR) {
      continue; // skips synced/ and transcripts/
    }
    const char *dot = strrchr(entry->d_name, '.');
    if (dot == NULL || strcasecmp(dot, ".wav") != 0) {
      continue;
    }
    strlcpy(names[count], entry->d_name, MAX_NAME_LEN);
    count++;
  }
  closedir(dir);

  qsort(names, count, sizeof(capture_name_t), compare_names);
  return count;
}

// Returns the HTTP status, or a negative value if the request never completed.
static int post_capture(esp_http_client_handle_t client, const char *base_url,
                        const char *path, const char *name) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    ESP_LOGE(TAG, "Cannot open %s", path);
    return -1;
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);

  char url[128];
  snprintf(url, sizeof(url), "%s/captures/%s", base_url, name);
  esp_http_client_set_url(client, url);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "audio/wav");

  // Declaring the length up front means no chunked transfer-encoding, and lets
  // the Companion stream straight to disk (ADR 0006).
  esp_err_t err = esp_http_client_open(client, size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Connect failed for %s: %s", name, esp_err_to_name(err));
    fclose(file);
    return -1;
  }

  int status = -1;
  bool sent_all = true;
  size_t read;
  while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
    if (esp_http_client_write(client, (const char *)chunk, read) != (int)read) {
      ESP_LOGE(TAG, "Write failed for %s", name);
      sent_all = false;
      break;
    }
  }

  // Blocks until the Companion has fsync'd and renamed the file, so a 200
  // genuinely means committed -- which is what makes the local rename safe.
  if (sent_all && esp_http_client_fetch_headers(client) >= 0) {
    status = esp_http_client_get_status_code(client);
  }

  fclose(file);
  esp_http_client_close(client);
  return status;
}

// Uploads every unsynced Capture, oldest-first. Returns false only on a
// session-level failure (401) -- a per-file failure is counted and skipped, so
// that one unsendable Capture cannot become a poison pill that aborts every
// future sync and silently blocks everything behind it (see CONTEXT.md).
static bool upload_captures(esp_http_client_handle_t client,
                            const char *base_url) {
  const int count = list_unsynced();

  for (int i = 0; i < count; i++) {
    const char *name = names[i];
    char from[64], to[80];
    snprintf(from, sizeof(from), "%s/%s", SD_MOUNT_POINT, name);
    snprintf(to, sizeof(to), "%s/%s", SYNCED_DIR, name);

    const int status = post_capture(client, base_url, from, name);

    if (status == 401) {
      ESP_LOGE(TAG, "Unauthorized: token mismatch");
      result.error = SYNC_ERR_UNAUTHORIZED;
      return false; // permanent until reflash; every other file would 401 too
    }

    // A 200 means the Companion has committed the file, so filing it away
    // locally is safe. If the rename itself fails we re-upload next sync --
    // idempotent by filename, so harmless, but worth counting as a failure.
    if (status == 200 && rename(from, to) == 0) {
      result.uploaded++;
      ESP_LOGI(TAG, "Uploaded %s", name);
    } else {
      ESP_LOGW(TAG, "Upload of %s failed (status %d), leaving unsynced", name,
               status);
      result.failed++;
    }
  }

  return true;
}

// --- Download phase ----------------------------------------------------------

// A .part is the wreckage of a download interrupted by a dead battery. Cleared
// at sync start so it can never be mistaken for a real Transcript.
static void clean_stray_parts(void) {
  DIR *dir = opendir(TRANSCRIPTS_DIR);
  if (dir == NULL) {
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *dot = strrchr(entry->d_name, '.');
    if (dot != NULL && strcmp(dot, ".part") == 0) {
      // Sized from the types, not guessed
      char path[sizeof(TRANSCRIPTS_DIR) + 1 + sizeof(entry->d_name)];
      snprintf(path, sizeof(path), "%s/%s", TRANSCRIPTS_DIR, entry->d_name);
      ESP_LOGW(TAG, "Removing stray %s", path);
      remove(path);
    }
  }
  closedir(dir);
}

static bool have_transcript(const char *name) {
  char path[80];
  snprintf(path, sizeof(path), "%s/%s", TRANSCRIPTS_DIR, name);
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    return false;
  }
  fclose(file);
  return true;
}

// GET one Transcript, streamed to a .part and renamed only after a clean read to
// EOF. That atomicity is what makes the presence-diff safe: a truncated .txt
// would otherwise look like "already have it" forever (ADR 0006).
static bool download_transcript(esp_http_client_handle_t client,
                                const char *base_url, const char *name) {
  char url[128];
  snprintf(url, sizeof(url), "%s/transcripts/%s", base_url, name);
  esp_http_client_set_url(client, url);
  esp_http_client_set_method(client, HTTP_METHOD_GET);

  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "Connect failed for %s", name);
    return false;
  }

  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    // 404 just means not transcribed yet: skip, try again next sync.
    ESP_LOGW(TAG, "Transcript %s not ready (status %d)", name, status);
    esp_http_client_close(client);
    return false;
  }

  char part_path[88], final_path[80];
  snprintf(final_path, sizeof(final_path), "%s/%s", TRANSCRIPTS_DIR, name);
  snprintf(part_path, sizeof(part_path), "%s.part", final_path);

  FILE *file = fopen(part_path, "wb");
  if (file == NULL) {
    ESP_LOGE(TAG, "Cannot open %s", part_path);
    esp_http_client_close(client);
    return false;
  }

  bool ok = true;
  int read;
  while ((read = esp_http_client_read(client, (char *)chunk, sizeof(chunk))) >
         0) {
    if (fwrite(chunk, 1, read, file) != (size_t)read) {
      ESP_LOGE(TAG, "Write failed for %s", part_path);
      ok = false;
      break;
    }
  }
  if (read < 0) {
    ok = false; // connection died mid-body
  }

  fclose(file);
  esp_http_client_close(client);

  if (!ok || rename(part_path, final_path) != 0) {
    remove(part_path); // never leave a truncated .txt behind
    return false;
  }
  return true;
}

// Fetch the Companion's list of ready Transcripts and pull the ones we lack.
static void download_transcripts(esp_http_client_handle_t client,
                                 const char *base_url) {
  char url[128];
  snprintf(url, sizeof(url), "%s/transcripts", base_url);
  esp_http_client_set_url(client, url);
  esp_http_client_set_method(client, HTTP_METHOD_GET);

  if (esp_http_client_open(client, 0) != ESP_OK) {
    ESP_LOGE(TAG, "Cannot fetch transcript list");
    return;
  }

  const int content_length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200 || content_length <= 0 ||
      content_length >= (int)sizeof(chunk)) {
    ESP_LOGW(TAG, "Transcript list unavailable (status %d, length %d)", status,
             content_length);
    esp_http_client_close(client);
    return;
  }

  const int read = esp_http_client_read(client, (char *)chunk, content_length);
  esp_http_client_close(client);
  if (read != content_length) {
    ESP_LOGE(TAG, "Short read on transcript list");
    return;
  }
  chunk[read] = '\0';

  // The body is a bare array of quoted filenames -- ["note_0003.txt",...] --
  // and the names are note_NNNN.txt: no escapes, no commas, no unicode. So "the
  // strings are what sits between quote pairs" is a complete description of the
  // format, and a JSON parser would be doing nothing a scan cannot.
  int count = 0;
  char *cursor = (char *)chunk;
  while (count < MAX_PER_SYNC) {
    char *start = strchr(cursor, '"');
    if (start == NULL) {
      break;
    }
    char *end = strchr(start + 1, '"');
    if (end == NULL) {
      break;
    }
    *end = '\0';
    const char *name = start + 1;
    cursor = end + 1;

    // The name arrives over the network and becomes a path below. Refuse
    // anything that could climb out of TRANSCRIPTS_DIR, and anything long
    // enough to truncate into a path buffer (two long names could otherwise
    // truncate to the same file).
    if (strlen(name) >= MAX_NAME_LEN || strchr(name, '/') != NULL ||
        strstr(name, "..") != NULL) {
      ESP_LOGW(TAG, "Refusing suspicious transcript name");
      continue;
    }
    strlcpy(names[count], name, MAX_NAME_LEN);
    count++;
  }

  for (int i = 0; i < count; i++) {
    if (have_transcript(names[i])) {
      continue;
    }
    if (download_transcript(client, base_url, names[i])) {
      result.downloaded++;
      ESP_LOGI(TAG, "Downloaded %s", names[i]);
    }
    // A transcript we could not fetch is not counted as a failure: it is not
    // ours yet. It stays on the Companion and we ask again next sync.
  }
}

static void sync_task(void *arg) {
  result = (sync_result_t){.phase = SYNC_PHASE_CONNECTING, .error = SYNC_OK};
  char companion_ip[16] = {};
  char base_url[48];
  esp_http_client_handle_t client = NULL;

  set_phase(SYNC_PHASE_CONNECTING);
  if (wifi_connect() != ESP_OK) {
    result.error = SYNC_ERR_WIFI;
    goto done; // session-level: nothing to talk to
  }

  set_phase(SYNC_PHASE_RESOLVING);
  if (resolve_companion(companion_ip, sizeof(companion_ip)) != ESP_OK) {
    result.error = SYNC_ERR_NO_COMPANION;
    goto done; // session-level: no point retrying files
  }

  snprintf(base_url, sizeof(base_url), "http://%s:%d", companion_ip,
           COMPANION_PORT);

  // One keep-alive client for every request in the session: no TCP handshake
  // per file (ADR 0006). The bearer token is set once here and rides every
  // request, since the Companion 401s anything without it.
  esp_http_client_config_t http_cfg = {
      .url = base_url,
      .timeout_ms = HTTP_TIMEOUT_MS,
      .keep_alive_enable = true,
  };
  client = esp_http_client_init(&http_cfg);
  if (client == NULL) {
    result.error = SYNC_ERR_INTERNAL;
    goto done;
  }

  char auth[96];
  snprintf(auth, sizeof(auth), "Bearer %s", COMPANION_TOKEN);
  esp_http_client_set_header(client, "Authorization", auth);

  clean_stray_parts();

  set_phase(SYNC_PHASE_UPLOADING);
  if (!upload_captures(client, base_url)) {
    goto done; // upload_captures set the session-level error
  }

  set_phase(SYNC_PHASE_DOWNLOADING);
  download_transcripts(client, base_url);

done:
  if (client != NULL) {
    esp_http_client_cleanup(client);
  }
  wifi_disconnect();

  result.phase = SYNC_PHASE_DONE;
  app_events_set(SYNC_ENDED_BIT);
  vTaskDelete(NULL);
}

esp_err_t sync_init(void) {
  wifi_events = xEventGroupCreate();
  return (wifi_events != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sync_start(void) {
  BaseType_t err = xTaskCreate(sync_task, "sync_task", 8192, NULL, 5, NULL);
  return (err == pdPASS) ? ESP_OK : ESP_FAIL;
}

sync_result_t sync_get_result(void) { return result; }
