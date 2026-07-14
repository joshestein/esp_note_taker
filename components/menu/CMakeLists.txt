idf_component_register(
  SRCS "menu.c"
  REQUIRES display_bsp esp_timer
  PRIV_REQUIRES app_events
  INCLUDE_DIRS "./")
