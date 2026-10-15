# ESP32-Climate-Camera v1.2.2 — ILI9341 SPI 2.4" 320×240

Релиз прошивки **v1.2.2** для климатической камеры на базе ESP32 и дисплея **2.4" TFT SPI 240×320 (ILI9341)** в альбомной ориентации **320×240**.

## Что изменено в версии 1.2.2
* **Версия прошивки:** обновлена до `1.2.2` (`FirmwareVersion.h`, `WebUI.h`, `ClimateCamera_1_2_2_IDE1819.ino`).
* **Драйвер матрицы:** переключён на `ILI9341_2_DRIVER` в `User_Setup.h` для корректной кадровой развёртки на 2.4" модулях ILI9341 новой ревизии.
* **Цветопередача (устранение негатива):** добавлены `#define TFT_INVERSION_ON` в `User_Setup.h` и вызов `tft.invertDisplay(true)` в `initDisplay()` и `recoverDisplay()`.
* **Ориентация экрана:** установлено значение `DISPLAY_ROTATION = 3` (прямая альбомная ориентация 320×240 для `ILI9341_2_DRIVER`).
* **Веб-интерфейс и OTA:** заголовки и описание совместимости обновлены на `ESP32 · ILI9341 320×240 · v1.2.2`.
* **Структура проекта для Arduino IDE 1.8.19:** каталог скетча и `.ino`-файл переименованы в `ClimateCamera_1_2_2_IDE1819/ClimateCamera_1_2_2_IDE1819.ino`.

## Подключение TFT 2.4" ILI9341
| Контакт TFT | ESP32 | Примечание |
| --- | --- | --- |
| SCK / CLK | GPIO18 | VSPI SCLK |
| SDI / MOSI | GPIO23 | VSPI MOSI |
| CS | GPIO19 | Обязателен для 2.4" SPI модуля |
| DC / RS | GPIO25 | Команда / данные |
| RESET / RST | GPIO13 | Аппаратный сброс |
| SDO / MISO | Не подключать | GPIO19 занят линией CS |
| GND | Общая земля | — |

Питание и LED/BL — по схеме конкретного модуля. Логические уровни ESP32 — 3.3 В. Сенсорный контроллер и SD-карта не используются.

## Сборка в Arduino IDE 1.8.19
1. Откройте `ClimateCamera_1_2_2_IDE1819/ClimateCamera_1_2_2_IDE1819.ino`.
2. Выберите плату **ESP32 Dev Module** (пакет Arduino-ESP32 `2.0.17`), библиотеку **TFT_eSPI** `2.5.43` и схему разделов с поддержкой OTA (например, **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**).
3. Скопируйте файл `TFT_eSPI_setup/User_Setup.h` (или `ClimateCamera_1_2_2_IDE1819/User_Setup.h`) в папку установленной библиотеки `Arduino/libraries/TFT_eSPI/`, заменив стандартный `User_Setup.h`.
4. Проверьте настройки сети и паролей в `ClimateCamera_1_2_2_IDE1819/Secrets.h`.
5. Если в вашем корпусе дисплей установлен верхтормашками, измените `DISPLAY_ROTATION` с `3` на `1` в `ClimateCamera.cpp`.

## Сборка в PlatformIO
В корне репозитория находится готовый `platformio.ini`. Команда сборки:
```bash
pio run
```
Настройки `User_Setup.h` подхватываются автоматически через флаги сборки `-DUSER_SETUP_LOADED=1` и `-include`.
