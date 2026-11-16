#include "sync.h"
#include "app_events.h"
#include "config.h"
#include "esp_check.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "wifi_secrets.h"
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "sync";

#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_MAX_RETRIES 3
// Cap on scan records pulled when picking a network.
#define WIFI_SCAN_MAX_AP 10
#define MDNS_RESOLVE_TIMEOUT_MS 3000
#define HTTP_TIMEOUT_MS 10000

// Streamed from SD straight to the socket, so RAM stays flat regardless of how
// long the memo is (ADR 0006).
#define UPLOAD_CHUNK_SIZE 4096

#define MAX_NAME_LEN 32
// Captures uploaded per session. Anything beyond this goes on the next sync --
// free, because the protocol is a resumable sequence of per-file commits.
#define MAX_PER_SYNC 64

// Local to the Wi-Fi bring-up. Distinct from the app event group: these bits are
// an implementation detail of connecting, not something main.c reacts to.
#define WIFI_GOT_IP_BIT (1 << 0)
#define WIFI_FAILED_BIT (1 << 1)

static EventGroupHandle_t wifi_events = NULL;
static sync_result_t result;

// Saved networks (wifi_secrets.h). At sync start we scan and join the
// strongest of these actually in range.
// `username` set: 802.1X (PEAP/TTLS), `password` is the account password and
// `ca_cert` (PEM, the RADIUS server's CA) is required. NULL: WPA-PSK/open.
typedef struct {
  const char *ssid;
  const char *password;
  const char *username;
  const char *ca_cert;
} wifi_network_t;
static const wifi_network_t wifi_networks[] = WIFI_NETWORKS;
#define WIFI_NETWORK_COUNT (sizeof(wifi_networks) / sizeof(wifi_networks[0]))

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
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
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

// Scans nearby APs and returns the index into wifi_networks of the strongest
// saved network that is actually in range, and that AP's BSSID and channel.
// Returns -1 if none of ours are present (a distinct, non-transient failure:
// wrong place, or add this network), and -2 on a scan/radio error. Requires the
// radio already started.
static int pick_known_network(uint8_t bssid[6], uint8_t *channel) {
  wifi_scan_config_t scan_cfg = {}; // active scan, all channels, hidden skipped
  if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
    ESP_LOGE(TAG, "Wi-Fi scan failed");
    return -2;
  }

  uint16_t found = 0;
  esp_wifi_scan_get_ap_num(&found);
  if (found == 0) {
    return -1;
  }
  if (found > WIFI_SCAN_MAX_AP) {
    found = WIFI_SCAN_MAX_AP;
  }

  wifi_ap_record_t *records = malloc(found * sizeof(wifi_ap_record_t));
  if (records == NULL) {
    esp_wifi_clear_ap_list();
    return -2;
  }
  uint16_t n = found;
  // Frees the driver's internal AP list even when we ask for fewer than found.
  if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
    free(records);
    return -2;
  }

  int best = -1;
  int8_t best_rssi = INT8_MIN;
  uint8_t best_channel = 0;
  for (uint16_t i = 0; i < n; i++) {
    for (size_t k = 0; k < WIFI_NETWORK_COUNT; k++) {
      if (strcmp((const char *)records[i].ssid, wifi_networks[k].ssid) == 0 &&
          records[i].rssi > best_rssi) {
        best_rssi = records[i].rssi;
        best_channel = records[i].primary;
        memcpy(bssid, records[i].bssid, 6);
        best = (int)k;
      }
    }
  }
  free(records);
  if (best >= 0) {
    *channel = best_channel;
    ESP_LOGI(TAG, "Best: %s, RSSI %d dBm, channel %u, bssid " MACSTR,
             wifi_networks[best].ssid, best_rssi, best_channel, MAC2STR(bssid));
  }
  return best;
}

// EAP credentials are global supplicant state, not part of wifi_config_t, so
// clear them on every join or a later PSK join still tries EAP.
// Cert expiry is not checked (IDF default): the clock is unset at sync time.
static esp_err_t configure_enterprise(const wifi_network_t *net) {
  esp_eap_client_clear_identity();
  esp_eap_client_clear_username();
  esp_eap_client_clear_password();
  esp_eap_client_clear_ca_cert();

  if (net->username == NULL) {
    return esp_wifi_sta_enterprise_disable();
  }
  // Without the CA, any AP with this SSID can harvest the credentials.
  ESP_RETURN_ON_FALSE(net->ca_cert != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "%s: 802.1X network has no CA cert", net->ssid);

  const unsigned char *user = (const unsigned char *)net->username;
  int user_len = (int)strlen(net->username);
  ESP_RETURN_ON_ERROR(esp_eap_client_set_identity(user, user_len), TAG,
                      "eap identity");
  ESP_RETURN_ON_ERROR(esp_eap_client_set_username(user, user_len), TAG,
                      "eap username");
  ESP_RETURN_ON_ERROR(
      esp_eap_client_set_password((const unsigned char *)net->password,
                                  (int)strlen(net->password)),
      TAG, "eap password");

  // PEM length must include the NUL. The pointer is kept, not copied: fine
  // for a string literal.
  ESP_RETURN_ON_ERROR(
      esp_eap_client_set_ca_cert((const unsigned char *)net->ca_cert,
                                 (int)strlen(net->ca_cert) + 1),
      TAG, "eap ca cert");

  return esp_wifi_sta_enterprise_enable();
}

// Scans, joins the strongest saved network in range, or gives up. Sets
// result.error on every failure path. Bounded: never hangs the sync task.
// Teardown of the radio on failure is left to wifi_disconnect() at sync end.
static esp_err_t wifi_connect(void) {
  ESP_RETURN_ON_ERROR(netif_init_once(), TAG, "netif init");

  retries = 0;
  explicit_stop_requested = false;
  xEventGroupClearBits(wifi_events, WIFI_GOT_IP_BIT | WIFI_FAILED_BIT);

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  // Powers the radio. Started before any network is configured so we can scan
  // first and pick the strongest saved network that is actually in range.
  esp_err_t err = esp_wifi_start();
  if (err != ESP_OK) {
    esp_wifi_deinit();
    ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
    result.error = SYNC_ERR_WIFI;
    return err;
  }

  // Modem sleep delays TCP ACKs and throttles the upload. The radio is up only
  // for the sync: higher draw for a shorter time.
  esp_wifi_set_ps(WIFI_PS_NONE);

  uint8_t bssid[6] = {};
  uint8_t channel = 0;
  int net = pick_known_network(bssid, &channel);
  if (net < 0) {
    result.error = (net == -1) ? SYNC_ERR_NO_KNOWN_WIFI : SYNC_ERR_WIFI;
    ESP_LOGE(TAG, "%s",
             net == -1 ? "No saved network in range" : "Wi-Fi scan error");
    return ESP_FAIL;
  }

  const wifi_network_t *chosen = &wifi_networks[net];
  bool enterprise = chosen->username != NULL;

  wifi_config_t wifi_cfg = {};
  strlcpy((char *)wifi_cfg.sta.ssid, chosen->ssid, sizeof(wifi_cfg.sta.ssid));
  // Pin the AP the scan chose. Unpinned, the driver joins the first AP it
  // hears with this SSID, not the strongest (3x slower uploads measured on a
  // multi-AP network). Retries reuse the same AP.
  memcpy(wifi_cfg.sta.bssid, bssid, sizeof(wifi_cfg.sta.bssid));
  wifi_cfg.sta.bssid_set = true;
  wifi_cfg.sta.channel = channel;
  if (!enterprise) {
    strlcpy((char *)wifi_cfg.sta.password, chosen->password,
            sizeof(wifi_cfg.sta.password));
  }
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

  err = configure_enterprise(chosen);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "802.1X setup failed: %s", esp_err_to_name(err));
    result.error = SYNC_ERR_WIFI;
    return err;
  }
  ESP_LOGI(TAG, "Joining %s%s", chosen->ssid, enterprise ? " (802.1X)" : "");

  esp_wifi_connect();

  EventBits_t bits = xEventGroupWaitBits(
      wifi_events, WIFI_GOT_IP_BIT | WIFI_FAILED_BIT, pdTRUE, pdFALSE,
      pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

  if ((bits & WIFI_GOT_IP_BIT) == 0) {
    ESP_LOGE(TAG, "Wi-Fi connect failed (%s)",
             (bits & WIFI_FAILED_BIT) ? "gave up" : "timed out");
    result.error = SYNC_ERR_WIFI;
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

// Owned by the sync task: allocated when it starts, freed when it ends. Held for
// the seconds a sync runs rather than for the life of the device, which would
// cost ~6 KB of DRAM permanently for a feature used once in a while. Safe to
// keep as file statics because the sync task is never re-entrant.
//
// The MAX_PER_SYNC cap is free: sync is a resumable sequence of independent
// per-file commits, so anything past it simply goes on the next sync.
typedef char capture_name_t[MAX_NAME_LEN];
static capture_name_t *names; // MAX_PER_SYNC entries
static uint8_t *chunk;        // UPLOAD_CHUNK_SIZE bytes

static bool alloc_buffers(void) {
  names = malloc(sizeof(capture_name_t) * MAX_PER_SYNC);
  chunk = malloc(UPLOAD_CHUNK_SIZE);
  return names != NULL && chunk != NULL;
}

static void free_buffers(void) {
  free(names);
  free(chunk);
  names = NULL;
  chunk = NULL;
}

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

  int64_t t_start = esp_timer_get_time();

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
  while ((read = fread(chunk, 1, UPLOAD_CHUNK_SIZE, file)) > 0) {
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

  // ~95% of this is the network write (measured), so a slow rate means the
  // link: weak AP or busy channel.
  int64_t ms = (esp_timer_get_time() - t_start) / 1000;
  ESP_LOGI(TAG, "%s: %ld KB in %lld ms (%lld KB/s)", name, size / 1024, ms,
           ms > 0 ? (int64_t)size / ms * 1000 / 1024 : 0);

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
  char path[sizeof(TRANSCRIPTS_DIR) + 1 + MAX_NAME_LEN];
  snprintf(path, sizeof(path), "%s/%s", TRANSCRIPTS_DIR, name);
  // Existence, not contents: no FILE handle to allocate against the VFS's
  // 5-file budget, and no open/close of a file we never read.
  return access(path, F_OK) == 0;
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

  char final_path[sizeof(TRANSCRIPTS_DIR) + 1 + MAX_NAME_LEN];
  char part_path[sizeof(final_path) + sizeof(".part")];
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
  while ((read = esp_http_client_read(client, (char *)chunk,
                                      UPLOAD_CHUNK_SIZE)) > 0) {
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

  esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGW(TAG, "Transcript list unavailable (status %d)", status);
    esp_http_client_close(client);
    return;
  }

  // The body is ["note_0003.txt",...] and names hold no quotes or escapes, so
  // a name is whatever sits between a quote pair. Scanned as it streams, since
  // the list outgrows any buffer. Only missing names take a slot, so each sync
  // catches up by up to MAX_PER_SYNC however long the list.
  int count = 0;
  char name[MAX_NAME_LEN];
  int len = -1; // -1: outside quotes. MAX_NAME_LEN: too long to keep.
  int read;
  while (count < MAX_PER_SYNC &&
         (read = esp_http_client_read(client, (char *)chunk,
                                      UPLOAD_CHUNK_SIZE)) > 0) {
    for (int i = 0; i < read && count < MAX_PER_SYNC; i++) {
      const char c = (char)chunk[i];
      if (c != '"') {
        if (len >= 0 && len < MAX_NAME_LEN) {
          name[len++] = c;
        }
        continue;
      }
      if (len < 0) {
        len = 0;
        continue;
      }
      const bool fits = len < MAX_NAME_LEN;
      if (fits) {
        name[len] = '\0';
      }
      len = -1;

      // A name from the network becomes a path: refuse one that climbs out of
      // TRANSCRIPTS_DIR or that would truncate into another name.
      if (!fits || strchr(name, '/') != NULL || strstr(name, "..") != NULL) {
        ESP_LOGW(TAG, "Refusing suspicious transcript name");
        continue;
      }
      if (!have_transcript(name)) {
        strlcpy(names[count++], name, MAX_NAME_LEN);
      }
    }
  }
  esp_http_client_close(client);

  // A failed fetch is not a sync failure: the next sync asks again.
  for (int i = 0; i < count; i++) {
    if (download_transcript(client, base_url, names[i])) {
      result.downloaded++;
      ESP_LOGI(TAG, "Downloaded %s", names[i]);
    }
  }
}

static void sync_task(void *arg) {
  result = (sync_result_t){};
  char companion_ip[16] = {};
  char base_url[48];
  esp_http_client_handle_t client = NULL;

  if (!alloc_buffers()) {
    ESP_LOGE(TAG, "Cannot allocate sync buffers");
    result.error = SYNC_ERR_INTERNAL;
    goto done;
  }

  set_phase(SYNC_PHASE_CONNECTING);
  if (wifi_connect() != ESP_OK) {
    goto done; // session-level: wifi_connect set the error
  }

  set_phase(SYNC_PHASE_RESOLVING);
  if (resolve_companion(companion_ip, sizeof(companion_ip)) != ESP_OK) {
    result.error = SYNC_ERR_NO_COMPANION;
    goto done; // session-level: no point retrying files
  }

  snprintf(base_url, sizeof(base_url), "http://%s:%d", companion_ip,
           COMPANION_PORT);

  // One client handle for the whole session: the bearer token is set once here
  // and rides every request, since the Companion 401s anything without it. The
  // socket is not held open between requests.
  esp_http_client_config_t http_cfg = {
      .url = base_url,
      .timeout_ms = HTTP_TIMEOUT_MS,
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
  free_buffers();

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
