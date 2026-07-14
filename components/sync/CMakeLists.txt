idf_component_register(
  SRCS "sync.c"
  PRIV_REQUIRES config app_events esp_wifi esp_event esp_netif esp_http_client nvs_flash
  INCLUDE_DIRS "./")
