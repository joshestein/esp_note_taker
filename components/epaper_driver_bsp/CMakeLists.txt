idf_component_register(
  SRCS "epaper_driver_bsp.cpp"
  PRIV_REQUIRES
  driver
  esp_driver_gpio
  esp_driver_spi
  REQUIRES
  driver
  INCLUDE_DIRS "./")
