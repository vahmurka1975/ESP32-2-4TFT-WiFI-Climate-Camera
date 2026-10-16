#pragma once
// ДО прошивки обязательно измените пароли и сохраните копию этого файла как Secrets.h!
// При пробросе порта в интернет НЕ оставляйте стандартные пароли!

// --- Подключение к роутеру TP-Link EC225-G5 (сеть 2.4 ГГц) ---
#define CC_WIFI_SSID "YOUR_WIFI_SSID"
#define CC_WIFI_PASS "YOUR_WIFI_PASSWORD"

// --- Статический локальный IP-адрес ESP32 в сети роутера ---
// 1 = назначить фиксированный локальный IP (рекомендуется для проброса портов)
// 0 = получать по DHCP (тогда настройте Address Reservation в TP-Link EC225-G5)
#define CC_USE_STATIC_IP 1
#define CC_STATIC_IP      192, 168, 0, 150
#define CC_STATIC_GATEWAY 192, 168, 0, 1
#define CC_STATIC_SUBNET  255, 255, 255, 0
#define CC_STATIC_DNS     192, 168, 0, 1

// --- Резервная точка доступа (SoftAP) ---
// 1 = включать точку доступа ТОЛЬКО если пропала связь с роутером >45 с (рекомендуется)
// 0 = держать точку доступа включенной всегда (режим WIFI_AP_STA)
#define CC_AP_FALLBACK_ONLY 1
#define CC_AP_SSID "ClimateCamera-AP"
#define CC_AP_PASS "Climate120!"

// --- Учётные записи веб-интерфейса (HTTP Basic Auth) ---
// Полный доступ (мониторинг + изменение уставок + управление + OTA):
#define CC_ADMIN_USER "admin"
#define CC_ADMIN_PASS "ChangeMe_Admin_2026!"

// Доступ ТОЛЬКО ДЛЯ МОНИТОРИНГА (чтение показателей и журнала без права управления):
#define CC_VIEWER_USER "viewer"
#define CC_VIEWER_PASS "ChangeMe_View_2026!"
