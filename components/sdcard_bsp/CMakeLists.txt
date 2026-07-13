idf_component_register(
  SRCS "sdcard_bsp.c"
  PRIV_REQUIRES config fatfs sdmmc
  INCLUDE_DIRS "./")
