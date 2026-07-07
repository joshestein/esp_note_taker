idf_component_register(
  SRCS "audio_bsp.c"
  PRIV_REQUIRES config driver esp_driver_i2c esp_driver_i2s esp_codec_dev
  INCLUDE_DIRS "./")
