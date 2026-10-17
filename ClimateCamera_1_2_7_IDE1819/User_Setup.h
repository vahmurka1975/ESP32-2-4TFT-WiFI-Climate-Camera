#pragma once
// ESP32-Climate-Camera 1.2.7: ILI9341 (variant 2) SPI, native 240x320.
// setRotation(3) + invertDisplay(true) in firmware produces a 320x240 landscape display.
#define USER_SETUP_INFO "ClimateCamera 1.2.7 ILI9341_2 SPI CS19"
#define ILI9341_2_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO -1
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   19
#define TFT_DC   25
#define TFT_RST  13
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  10000000
// No touch, SD-card or backlight GPIO is assigned by this setup.
// GPIO19 must NOT be wired to the display SDO/MISO pin.
