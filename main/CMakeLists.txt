idf_component_register(
    SRCS "main.c"
    REQUIRES audio_bsp button_input sdcard_bsp wav_writer
    INCLUDE_DIRS ".")
