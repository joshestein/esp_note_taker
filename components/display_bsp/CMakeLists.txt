idf_component_register(
    SRCS "display_bsp.cpp"
    INCLUDE_DIRS "."
    REQUIRES epaper_driver_bsp lvgl config
    PRIV_REQUIRES driver esp_timer esp_driver_gpio)
