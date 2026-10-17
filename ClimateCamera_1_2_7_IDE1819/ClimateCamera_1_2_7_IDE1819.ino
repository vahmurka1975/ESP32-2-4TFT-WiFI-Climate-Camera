/*
 * ESP32-Climate-Camera v1.2.7
 * ESP32 Dev Module / Arduino-ESP32 2.0.17 / TFT_eSPI / ILI9341_2 320x240.
 * Климат и TFT: Arduino loop (Core 1). Wi-Fi, HTTP, Remote Monitoring и Web OTA: Core 0.
 * Настройки сборки и настройка роутера TP-Link EC225-G5: README.md в корне архива.
 */
#include "ClimateCamera.h"
void setup() { cameraSetup(); }
void loop()  { cameraLoop(); }
