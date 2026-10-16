#pragma once
// ДО прошивки проверьте параметры вашей домашней сети и пароли!
// При пробросе порта в интернет обязательно замените пароли CC_ADMIN_PASS и CC_VIEWER_PASS.

// --- Подключение к роутеру TP-Link EC225-G5 (сеть 2.4 ГГц) ---
#define CC_WIFI_SSID "Svetlana"
#define CC_WIFI_PASS "89776381197"

// --- Статический локальный IP-адрес ESP32 в сети роутера ---
// 1 = назначить фиксированный локальный IP (для проброса портов на TP-Link EC225-G5)
// 0 = получать IP автоматически по DHCP
#define CC_USE_STATIC_IP 1
#define CC_STATIC_IP      192, 168, 0, 150
#define CC_STATIC_GATEWAY 192, 168, 0, 1
#define CC_STATIC_SUBNET  255, 255, 255, 0
#define CC_STATIC_DNS     192, 168, 0, 1

// --- Резервная точка доступа (SoftAP) ---
// 1 = включать точку доступа только при потере связи с роутером более 45 секунд
// 0 = держать точку доступа включенной постоянно
#define CC_AP_FALLBACK_ONLY 1
#define CC_AP_SSID "ClimateCamera-AP"
#define CC_AP_PASS "Climate120!"

// --- Учётные записи веб-интерфейса (HTTP Basic Auth) ---
// Полный доступ (мониторинг + изменение уставок + управление + OTA):
#define CC_ADMIN_USER "admin"
#define CC_ADMIN_PASS "Climate120!"

// Доступ ТОЛЬКО ДЛЯ МОНИТОРИНГА (безопасный просмотр из интернета без права управления):
#define CC_VIEWER_USER "viewer"
#define CC_VIEWER_PASS "Monitor120!"
