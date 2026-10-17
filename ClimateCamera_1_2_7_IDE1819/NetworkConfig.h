#pragma once
// Для личных настроек скопируйте Secrets.example.h в Secrets.h.
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

#ifndef CC_HOSTNAME
#define CC_HOSTNAME "climate-camera"
#endif

// Внутренний HTTP-порт контроллера (по умолчанию 80).
// На роутере TP-Link EC225-G5 рекомендуется пробрасывать внешний порт (например, 8080)
// на внутренний порт 80 этого контроллера.
#ifndef CC_HTTP_PORT
#define CC_HTTP_PORT 80
#endif

// Значения по умолчанию для обратной совместимости со старыми Secrets.h
#ifndef CC_USE_STATIC_IP
#define CC_USE_STATIC_IP 0
#endif
#ifndef CC_STATIC_IP
#define CC_STATIC_IP 192, 168, 0, 150
#endif
#ifndef CC_STATIC_GATEWAY
#define CC_STATIC_GATEWAY 192, 168, 0, 1
#endif
#ifndef CC_STATIC_SUBNET
#define CC_STATIC_SUBNET 255, 255, 255, 0
#endif
#ifndef CC_STATIC_DNS
#define CC_STATIC_DNS 192, 168, 0, 1
#endif
#ifndef CC_AP_FALLBACK_ONLY
#define CC_AP_FALLBACK_ONLY 0
#endif
#ifndef CC_VIEWER_USER
#define CC_VIEWER_USER "viewer"
#endif
#ifndef CC_VIEWER_PASS
#define CC_VIEWER_PASS ""
#endif

// Защита от перебора паролей при внешнем доступе через интернет
#ifndef CC_AUTH_MAX_FAILS
#define CC_AUTH_MAX_FAILS 5
#endif
#ifndef CC_AUTH_LOCKOUT_MS
#define CC_AUTH_LOCKOUT_MS 60000UL
#endif

// Настройки NTP и часового пояса для экрана-заставки с часами (v1.2.7)
// Москва (UTC+3): "MSK-3" | Екатеринбург (UTC+5): "<+05>-5" | Новосибирск (UTC+7): "<+07>-7"
#ifndef CC_TZ_INFO
#define CC_TZ_INFO "MSK-3"
#endif
#ifndef CC_NTP_SERVER_1
#define CC_NTP_SERVER_1 "pool.ntp.org"
#endif
#ifndef CC_NTP_SERVER_2
#define CC_NTP_SERVER_2 "ru.pool.ntp.org"
#endif
#ifndef CC_NTP_SERVER_3
#define CC_NTP_SERVER_3 "time.nist.gov"
#endif
