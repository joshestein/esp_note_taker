idf_component_register(
    SRCS "main.c"
    REQUIRES audio_bsp button_input config display_bsp menu sdcard_bsp wav_writer
    PRIV_REQUIRES esp_driver_gpio
    INCLUDE_DIRS ".")
