idf_component_register(
  SRCS "sync.c"
  PRIV_REQUIRES config app_events esp_wifi wpa_supplicant esp_event esp_netif esp_http_client esp_timer nvs_flash
  INCLUDE_DIRS "./")
