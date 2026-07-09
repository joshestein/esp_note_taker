#ifndef CONFIG_H
#define CONFIG_H
#include "driver/gpio.h"
#include "driver/spi_common.h"

#define RECORD_BUTTON GPIO_NUM_0
#define POWER_BUTTON GPIO_NUM_18

#define SAMPLE_RATE 16000

#define I2C_SDA_PIN GPIO_NUM_47
#define I2C_SCL_PIN GPIO_NUM_48
#define I2S_NUM 0
#define I2C_DEV_NUM I2C_NUM_0
#define I2S_MCLK_PIN GPIO_NUM_14
#define I2S_BCLK_PIN GPIO_NUM_15
#define I2S_WS_PIN GPIO_NUM_38
#define I2S_DOUT_PIN GPIO_NUM_45
#define I2S_DIN_PIN GPIO_NUM_16
#define MCLK_MULTIPLE 384
#define PA_CTRL_PIN GPIO_NUM_46

#define LED_PIN GPIO_NUM_3

// E-paper display (Waveshare ESP32-S3-Touch-ePaper-1.54, SSD1681)
#define EPD_CS_PIN GPIO_NUM_11
#define EPD_DC_PIN GPIO_NUM_10
#define EPD_SCK_PIN GPIO_NUM_12
#define EPD_MOSI_PIN GPIO_NUM_13
#define EPD_RST_PIN GPIO_NUM_9
#define EPD_BUSY_PIN GPIO_NUM_8
#define EPD_PWR_PIN GPIO_NUM_6 // panel power gate, active-low (LOW = on)
#define EPD_SPI_NUM SPI2_HOST
#define EPD_WIDTH 200
#define EPD_HEIGHT 200
#define EPD_BUFFER_LEN 5000 // 200*200/8, 1bpp

#endif
