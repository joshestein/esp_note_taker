idf_component_register(
  SRCS "sync.c"
  REQUIRES button_input
  PRIV_REQUIRES config esp_wifi esp_event esp_netif esp_http_client json nvs_flash
  INCLUDE_DIRS "./")
