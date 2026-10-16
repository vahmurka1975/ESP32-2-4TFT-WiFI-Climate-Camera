# Release Notes — ESP32 Climate Camera v1.2.6

## Что нового в версии `1.2.6`

1. **Интеграция полного пакета иконок Favicon и PWA-манифеста в память `PROGMEM` (`FaviconAssets.h`):**
   * Все графические ресурсы из репозитория [`vahmurka1975/favicon`](https://github.com/vahmurka1975/favicon) (`favicon.ico`, `favicon-16x16.png`, `favicon-32x32.png`, `favicon-96x96.png`, `apple-touch-icon.png`, `web-app-manifest-192x192.png`, `web-app-manifest-512x512.png` и `site.webmanifest`) сконвертированы в константные `PROGMEM`-массивы в заголовочном файле `FaviconAssets.h` (~321 КБ во Flash, **0 байт в оперативной памяти SRAM**).
   * Иконки не требуют загрузки SPIFFS/LittleFS и сохраняются при обычном обновлении прошивки через **Web OTA (`/update`)**.
2. **Новые маршруты в `NetworkServer.cpp` и кэширование браузером:**
   * Добавлена функция `sendProgmemAsset()` и обработчики `GET /favicon.ico`, `/favicon-16x16.png`, `/favicon-32x32.png`, `/favicon-96x96.png`, `/apple-touch-icon.png`, `/web-app-manifest-192x192.png`, `/web-app-manifest-512x512.png` и `/site.webmanifest`.
   * Маршруты иконок отдаются без блокировки окна HTTP Basic Auth (требование мобильных браузеров iOS Safari и Android Chrome для подгрузки значков вкладок и домашнего экрана) и снабжены заголовком `Cache-Control: public, max-age=86400`.
3. **Обновление `WebUI.h` и структуры проекта:**
   * В `<head>` главной страницы (`/`) и страницы обновления (`/update`) добавлены ссылки на все размеры иконок, `apple-touch-icon` и `site.webmanifest` с тёмной темой `#0a1220`.
   * Каталог скетча и главный файл переименованы в `ClimateCamera_1_2_6_IDE1819/ClimateCamera_1_2_6_IDE1819.ino`, версия прошивки в `FirmwareVersion.h` обновлена до `1.2.6`.
   * Сохранены все улучшения предыдущих релизов: выгрузка буфера экрана `153.6 КБ` на время Web OTA (`v1.2.5`), аварийный таймер холодильника `4 мин / 10 мин` при отказе датчика SHT30 (`v1.2.4`) и поддержка удалённого мониторинга через роутер TP-Link EC225-G5 (`v1.2.3`).
