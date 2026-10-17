#include "NetworkServer.h"
#include "ClimateCamera.h"
#include "FirmwareVersion.h"
#include "NetworkConfig.h"
#include "WebUI.h"
#include "FaviconAssets.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static WebServer server(CC_HTTP_PORT);
static TaskHandle_t networkHandle=nullptr;
static char csrfToken[33]={0};
static bool restartPending=false;
static uint32_t restartAt=0;
static bool apActive=false;

enum class AuthRole : uint8_t { None=0, Viewer=1, Admin=2 };

struct AuthGuard {
  IPAddress lastIp;
  uint8_t fails=0;
  uint32_t lockedUntilMs=0;
};
static AuthGuard authGuard;

struct OtaSession {
  bool authorized=false, maintenance=false, begun=false;
  bool failed=false, finished=false, headerChecked=false;
  size_t bytes=0;
  uint16_t errorCode=400;
  uint32_t lastChunkMs=0;
  String message;
};
static OtaSession ota;

static void sendSecurityHeaders() {
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate");
  server.sendHeader("X-Frame-Options","DENY");
  server.sendHeader("X-Content-Type-Options","nosniff");
  server.sendHeader("Referrer-Policy","no-referrer");
}

static void noCache() {
  sendSecurityHeaders();
}

static void sendProgmemAsset(const char *mime, const uint8_t *data, size_t len) {
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.send_P(200, mime, reinterpret_cast<const char *>(data), len);
}

static void sendMessage(uint16_t code,bool ok,const char *message) {
  String body=ok?"{\"ok\":true,\"message\":\"":"{\"ok\":false,\"message\":\"";
  for(const char *p=message;*p;++p) {
    if(*p=='\\' || *p=='\"') body+='\\';
    if((uint8_t)*p>=0x20) body+=*p;
  }
  body+="\"}";
  noCache();
  server.send(code,"application/json; charset=utf-8",body);
}

static AuthRole checkCredentials() {
  if(server.authenticate(CC_ADMIN_USER,CC_ADMIN_PASS)) return AuthRole::Admin;
  if(strlen(CC_VIEWER_USER)>0 && strlen(CC_VIEWER_PASS)>0 &&
     server.authenticate(CC_VIEWER_USER,CC_VIEWER_PASS)) {
    return AuthRole::Viewer;
  }
  return AuthRole::None;
}

static bool authorized(bool mutation=false, AuthRole *outRole=nullptr) {
  uint32_t now=millis();
  IPAddress remoteIp=server.client().remoteIP();
  if(authGuard.fails>=CC_AUTH_MAX_FAILS && remoteIp==authGuard.lastIp) {
    if((int32_t)(authGuard.lockedUntilMs-now)>0) {
      sendMessage(429,false,"Слишком много неверных попыток входа. Подождите 60 секунд.");
      return false;
    }
    authGuard.fails=0;
  }

  AuthRole role=checkCredentials();
  if(role==AuthRole::None) {
    if(remoteIp==authGuard.lastIp) {
      if(authGuard.fails<255) ++authGuard.fails;
    } else {
      authGuard.lastIp=remoteIp;
      authGuard.fails=1;
    }
    if(authGuard.fails>=CC_AUTH_MAX_FAILS) {
      authGuard.lockedUntilMs=now+CC_AUTH_LOCKOUT_MS;
    }
    server.requestAuthentication(BASIC_AUTH,"ESP32 Climate Camera");
    return false;
  }

  if(remoteIp==authGuard.lastIp) authGuard.fails=0;
  if(outRole) *outRole=role;

  if(mutation) {
    if(role!=AuthRole::Admin) {
      sendMessage(403,false,"Учётная запись только для мониторинга (чтение)");
      return false;
    }
    if(server.header("X-Climate-Token")!=csrfToken) {
      sendMessage(403,false,"Missing or invalid X-Climate-Token");
      return false;
    }
  }
  return true;
}

static void appendJsonString(String &out,const char *value) {
  out+='"';
  const char hex[]="0123456789abcdef";
  for(const unsigned char *p=(const unsigned char *)value;*p;++p) {
    if(*p=='"' || *p=='\\') {out+='\\'; out+=(char)*p;}
    else if(*p<0x20) {out+="\\u00";out+=hex[*p>>4];out+=hex[*p&15];}
    else out+=(char)*p;
  }
  out+='"';
}

static void handleEvents() {
  if(!authorized())return;
  static CameraEvent records[CAMERA_EVENT_CAPACITY];
  size_t count=cameraReadEvents(records,CAMERA_EVENT_CAPACITY);
  CameraSnapshot s={}; cameraReadSnapshot(s);
  String body; body.reserve(512+count*(CAMERA_EVENT_TEXT_SIZE+80));
  char b[144];
  snprintf(b,sizeof(b),"{\"version\":\"" FW_VERSION "\",\"capacity\":%u,\"count\":%u,\"uptimeSec\":%lu,\"events\":[",
           CAMERA_EVENT_CAPACITY,(unsigned)count,(unsigned long)s.uptimeSec);
  body=b;
  for(size_t i=0;i<count;++i) {
    if(i)body+=',';
    snprintf(b,sizeof(b),"{\"id\":%lu,\"uptimeMs\":%lu,\"uptimeSec\":%lu,\"message\":",
             (unsigned long)records[i].id,(unsigned long)records[i].at,(unsigned long)(records[i].at/1000));
    body+=b;appendJsonString(body,records[i].text);body+='}';
  }
  body+="]}";noCache();server.send(200,"application/json; charset=utf-8",body);
}

static String configJson(const Config &c) {
  char b[640];
  snprintf(b,sizeof(b),
    "{\"enabled\":%s,\"tempTarget\":%.3f,\"tempHyst\":%.3f,\"humTarget\":%.3f,\"humHyst\":%.3f,"
    "\"fanWorkMin\":%u,\"fanPauseMin\":%u,\"fanTargetRPM\":%u,\"tempOffset\":%.3f,"
    "\"humOffset\":%.3f,\"pidKp\":%.4f,\"pidKi\":%.4f}",
    c.enabled?"true":"false",c.tempTarget,c.tempHyst,c.humTarget,c.humHyst,
    (unsigned)c.fanWorkMin,(unsigned)c.fanPauseMin,(unsigned)c.fanTargetRPM,
    c.tempOffset,c.humOffset,c.pidKp,c.pidKi);
  return String(b);
}

static void handleStatus() {
  AuthRole role=AuthRole::None;
  if(!authorized(false,&role))return;
  CameraSnapshot s={};
  if(!cameraReadSnapshot(s)) {sendMessage(503,false,"No telemetry yet");return;}
  char t[24]="null",h[24]="null";
  if(s.sensorOk && isfinite(s.temp))snprintf(t,sizeof(t),"%.2f",s.temp);
  if(s.sensorOk && isfinite(s.hum))snprintf(h,sizeof(h),"%.2f",s.hum);
  char b[1000];
  snprintf(b,sizeof(b),
    "{\"version\":\"" FW_VERSION "\",\"temp\":%s,\"hum\":%s,\"rpm\":%.1f,\"pwm\":%.1f,"
    "\"enabled\":%s,\"sensorOk\":%s,\"pwmReady\":%s,\"fanTachFault\":%s,"
    "\"fridge\":%s,\"humid\":%s,\"fan\":%s,\"otaMode\":%s,\"uiEditing\":%s,"
    "\"fridgeWait\":%s,\"fridgeWaitSec\":%lu,\"fridgeRunSec\":%lu,\"uptimeSec\":%lu,",
    t,h,isfinite(s.rpm)?s.rpm:0.0f,isfinite(s.pwm)?s.pwm:0.0f,
    s.config.enabled?"true":"false",s.sensorOk?"true":"false",s.pwmReady?"true":"false",s.fanTachFault?"true":"false",
    s.fridge?"true":"false",s.humid?"true":"false",s.fan?"true":"false",s.otaMode?"true":"false",s.uiEditing?"true":"false",
    s.fridgeWaitMs?"true":"false",(unsigned long)((s.fridgeWaitMs+999)/1000),(unsigned long)(s.fridgeRunMs/1000),(unsigned long)s.uptimeSec);
  String json;json.reserve(1900);json=b;
  json+="\"sensorAgeSec\":";
  if(s.sensorAgeMs==UINT32_MAX)json+="null";
  else {char age[24];snprintf(age,sizeof(age),"%lu",(unsigned long)(s.sensorAgeMs/1000));json+=age;}
  json+=",\"sensorDegraded\":";json+=s.sensorDegraded?"true":"false";
  char failures[16];snprintf(failures,sizeof(failures),"%u",s.sensorFailures);
  json+=",\"sensorFailures\":";json+=failures;
  json+=",\"sensorFailureLimit\":3,\"sensorState\":";
  appendJsonString(json,s.sensorOk?(s.sensorDegraded?"degraded":"ok"):(s.sensorAgeMs==UINT32_MAX?"starting":"failed"));
  json+=",\"sensorErrorCode\":";appendJsonString(json,s.sensorErrorCode);
  json+=",\"sensorError\":";appendJsonString(json,s.sensorError);
  bool wifiConn=(WiFi.status()==WL_CONNECTED);
  json+=",\"wifiConnected\":";json+=wifiConn?"true":"false";
  char rssiBuf[16];snprintf(rssiBuf,sizeof(rssiBuf),"%d",wifiConn?(int)WiFi.RSSI():0);
  json+=",\"rssi\":";json+=rssiBuf;
  json+=",\"staIP\":\"";json+=WiFi.localIP().toString();
  json+="\",\"gatewayIP\":\"";json+=WiFi.gatewayIP().toString();
  json+="\",\"mac\":\"";json+=WiFi.macAddress();
  json+="\",\"apIP\":\"";json+=apActive?WiFi.softAPIP().toString():String("выкл");
  json+="\",\"readOnly\":";json+=(role!=AuthRole::Admin)?"true":"false";
  json+=",\"token\":\"";json+=(role==AuthRole::Admin)?csrfToken:"";
  time_t nowEpoch=time(nullptr);
  struct tm ti={};
  if(nowEpoch>1700000000L && localtime_r(&nowEpoch,&ti)) {
    char tbuf[16];
    snprintf(tbuf,sizeof(tbuf),"\"%02d:%02d:%02d\"",ti.tm_hour,ti.tm_min,ti.tm_sec);
    json+="\",\"time\":";json+=tbuf;
  } else {
    json+="\",\"time\":null";
  }
  json+=",\"config\":";json+=configJson(s.config);json+="}";
  noCache();server.send(200,"application/json; charset=utf-8",json);
}

static void handleGetConfig() {
  if(!authorized())return;
  CameraSnapshot s={};if(!cameraReadSnapshot(s)){sendMessage(503,false,"No telemetry yet");return;}
  noCache();server.send(200,"application/json; charset=utf-8",configJson(s.config));
}

static bool numberArg(const char *name,double low,double high,bool integer,double &out) {
  String text=server.arg(name);text.trim();
  if(text.length()==0 || text.length()>32)return false;
  char *end=nullptr;errno=0;
  double v=strtod(text.c_str(),&end);
  if(errno==ERANGE || end==text.c_str() || *end!='\0' || !isfinite(v) || v<low || v>high)return false;
  if(integer && floor(v)!=v)return false;
  out=v;return true;
}

static bool knownField(const String &n) {
  static const char *fields[]={"tempTarget","tempHyst","humTarget","humHyst","fanWorkMin","fanPauseMin","fanTargetRPM","tempOffset","humOffset","pidKp","pidKi","save"};
  for(const char *field:fields)if(n==field)return true;
  return false;
}

static void handleSetConfig() {
  if(!authorized(true))return;
  CameraSnapshot state={};if(!cameraReadSnapshot(state)){sendMessage(503,false,"No telemetry yet");return;}
  if(state.otaMode || state.uiEditing){sendMessage(409,false,"OTA or local editing active");return;}
  for(int i=0;i<server.args();++i)if(!knownField(server.argName(i))){sendMessage(400,false,"Unknown config field");return;}
  CameraCommand cmd={};cmd.kind=CameraCommandKind::SetConfig;cmd.config=state.config;cmd.save=false;
  unsigned changed=0;double value=0;
#define READ_FLOAT(name,lo,hi) if(server.hasArg(#name)){if(!numberArg(#name,lo,hi,false,value)){sendMessage(400,false,"Invalid " #name);return;}cmd.config.name=(float)value;++changed;}
#define READ_INT(name,lo,hi) if(server.hasArg(#name)){if(!numberArg(#name,lo,hi,true,value)){sendMessage(400,false,"Invalid " #name);return;}cmd.config.name=(uint16_t)value;++changed;}
  READ_FLOAT(tempTarget,2,30) READ_FLOAT(tempHyst,0.2,5)
  READ_FLOAT(humTarget,30,98) READ_FLOAT(humHyst,1,10)
  READ_INT(fanWorkMin,1,180) READ_INT(fanPauseMin,0,360) READ_INT(fanTargetRPM,300,1500)
  READ_FLOAT(tempOffset,-5,5) READ_FLOAT(humOffset,-15,15)
  READ_FLOAT(pidKp,0,1) READ_FLOAT(pidKi,0,1)
#undef READ_FLOAT
#undef READ_INT
  if(!changed){sendMessage(400,false,"No settings supplied");return;}
  if(server.hasArg("save")) {
    String save=server.arg("save");
    if(save!="0" && save!="1"){sendMessage(400,false,"save must be 0 or 1");return;}
    cmd.save=save=="1";
  }
  CameraReply reply={};cameraSubmitCommand(cmd,reply);
  sendMessage(reply.httpStatus,reply.ok,reply.message);
}

static void handleControl() {
  if(!authorized(true))return;
  if(server.args()!=1 || !server.hasArg("action")){sendMessage(400,false,"Supply action only");return;}
  String action=server.arg("action");CameraCommand cmd={};
  if(action=="on")cmd.kind=CameraCommandKind::SystemOn;
  else if(action=="off")cmd.kind=CameraCommandKind::SystemOff;
  else if(action=="ack")cmd.kind=CameraCommandKind::Acknowledge;
  else if(action=="save")cmd.kind=CameraCommandKind::Save;
  else {sendMessage(400,false,"Unknown action");return;}
  CameraReply reply={};cameraSubmitCommand(cmd,reply);sendMessage(reply.httpStatus,reply.ok,reply.message);
}

static void failOta(uint16_t code,const char *message) {
  if(!ota.failed){ota.errorCode=code;ota.message=message;}
  ota.failed=true;
  if(ota.begun){Update.abort();ota.begun=false;}
}

static void releaseOta() {
  if(!ota.maintenance)return;
  CameraCommand cmd={};cmd.kind=CameraCommandKind::LeaveOta;CameraReply reply={};
  if(!cameraSubmitCommand(cmd,reply)||!reply.ok)Serial.println("OTA release not acknowledged; outputs remain stopped");
  ota.maintenance=false;
}

static void uploadOta() {
  HTTPUpload &upload=server.upload();
  if(upload.status==UPLOAD_FILE_START) {
    if(Update.isRunning()) Update.abort();
    ota=OtaSession();ota.lastChunkMs=millis();
    ota.authorized=(checkCredentials()==AuthRole::Admin) && server.header("X-Climate-Token")==csrfToken;
    if(!ota.authorized){failOta(403,"Unauthorized upload (admin account required)");return;}
    String filename=upload.filename;filename.toLowerCase();
    if(!filename.endsWith(".bin")){failOta(400,"Expected application .bin");return;}
    const esp_partition_t *part=esp_ota_get_next_update_partition(nullptr);
    if(!part){failOta(409,"No OTA application partition; flash correct partition table by USB");return;}
    CameraCommand cmd={};cmd.kind=CameraCommandKind::EnterOta;CameraReply reply={};
    if(!cameraSubmitCommand(cmd,reply,3000)||!reply.ok){failOta(503,"Outputs-off acknowledgement failed; nothing written");return;}
    ota.maintenance=true;
    if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH)){
      char errBuf[160];
      snprintf(errBuf,sizeof(errBuf),"Update.begin failed (%s); freeHeap=%u, maxAlloc=%u, partSize=%u",
               Update.errorString(),(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMaxAllocHeap(),(unsigned)part->size);
      Serial.println(errBuf);
      failOta(500,errBuf);
      releaseOta();
      return;
    }
    ota.begun=true;
  } else if(upload.status==UPLOAD_FILE_WRITE) {
    ota.lastChunkMs=millis();
    if(ota.failed || !ota.authorized || !ota.begun)return;
    if(!ota.headerChecked) {
      if(upload.currentSize==0)return;
      ota.headerChecked=true;
      if(upload.buf[0]!=0xE9){failOta(400,"Not an ESP32 application image");return;}
    }
    const esp_partition_t *part=esp_ota_get_next_update_partition(nullptr);
    if(part && ota.bytes+upload.currentSize>part->size){failOta(413,"Image exceeds OTA partition size");return;}
    if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize){failOta(500,"Flash write failed");return;}
    ota.bytes+=upload.currentSize;
  } else if(upload.status==UPLOAD_FILE_END) {
    ota.lastChunkMs=millis();
    if(ota.failed || !ota.authorized || !ota.begun)return;
    if(ota.bytes<4096 || !Update.end(true)){failOta(400,"Image verification failed");return;}
    ota.begun=false;ota.finished=true;
  } else if(upload.status==UPLOAD_FILE_ABORTED) {
    failOta(400,"Upload aborted");releaseOta();
  }
}

static void finishOtaRequest() {
  if(!authorized(true)) {
    if(ota.maintenance) {
      if(ota.begun) {
        Update.abort();
        ota.begun=false;
      }
      releaseOta();
    }
    return;
  }
  if(ota.finished && !ota.failed) {
    sendMessage(200,true,"Update written; rebooting");
    server.client().flush();
    restartPending=true;restartAt=millis()+2000;
  } else {
    if(!ota.failed)failOta(400,"No completed application upload");
    releaseOta();sendMessage(ota.errorCode,false,ota.message.c_str());
  }
}

static void setupRoutes() {
  const char *headers[]={"X-Climate-Token"};server.collectHeaders(headers,1);
  server.on("/",HTTP_GET,[](){if(!authorized())return;noCache();server.send_P(200,"text/html; charset=utf-8",INDEX_HTML);});
  server.on("/favicon.ico",HTTP_GET,[](){sendProgmemAsset("image/x-icon",FAVICON_ICO,FAVICON_ICO_LEN);});
  server.on("/favicon-16x16.png",HTTP_GET,[](){sendProgmemAsset("image/png",FAVICON_16_PNG,FAVICON_16_PNG_LEN);});
  server.on("/favicon-32x32.png",HTTP_GET,[](){sendProgmemAsset("image/png",FAVICON_32_PNG,FAVICON_32_PNG_LEN);});
  server.on("/favicon-96x96.png",HTTP_GET,[](){sendProgmemAsset("image/png",FAVICON_96_PNG,FAVICON_96_PNG_LEN);});
  server.on("/apple-touch-icon.png",HTTP_GET,[](){sendProgmemAsset("image/png",APPLE_TOUCH_ICON_PNG,APPLE_TOUCH_ICON_PNG_LEN);});
  server.on("/web-app-manifest-192x192.png",HTTP_GET,[](){sendProgmemAsset("image/png",WEB_APP_192_PNG,WEB_APP_192_PNG_LEN);});
  server.on("/web-app-manifest-512x512.png",HTTP_GET,[](){sendProgmemAsset("image/png",WEB_APP_512_PNG,WEB_APP_512_PNG_LEN);});
  server.on("/site.webmanifest",HTTP_GET,[](){
    server.sendHeader("Cache-Control","public, max-age=86400");
    server.sendHeader("X-Content-Type-Options","nosniff");
    server.send_P(200,"application/manifest+json; charset=utf-8",SITE_WEBMANIFEST);
  });
  server.on("/api/time",HTTP_GET,[](){
    if(!authorized())return;
    time_t e=time(nullptr);
    struct tm ti={};
    char body[112];
    noCache();
    if(e>1700000000L && localtime_r(&e,&ti))
      snprintf(body,sizeof(body),
               "{\"ok\":true,\"tz\":\"%s\",\"time\":\"%02d:%02d:%02d\",\"iso\":\"%04d-%02d-%02dT%02d:%02d:%02d\"}",
               CC_TZ_INFO,ti.tm_hour,ti.tm_min,ti.tm_sec,
               ti.tm_year+1900,ti.tm_mon+1,ti.tm_mday,
               ti.tm_hour,ti.tm_min,ti.tm_sec);
    else
      snprintf(body,sizeof(body),"{\"ok\":false,\"message\":\"NTP not synced yet\"}");
    server.send(200,"application/json; charset=utf-8",body);
  });
  server.on("/api/logout",HTTP_GET,[](){
    noCache();
    server.sendHeader("WWW-Authenticate","Basic realm=\"ESP32 Climate Camera\"");
    server.send(401,"application/json; charset=utf-8","{\"ok\":true,\"loggedOut\":true}");
  });
  server.on("/api/reauth",HTTP_GET,[](){
    noCache();
    if(!server.hasArg("user")) {
      server.requestAuthentication(BASIC_AUTH,"ESP32 Climate Camera");
      return;
    }
    AuthRole role=checkCredentials();
    if(role==AuthRole::None ||
       (server.arg("user")=="admin" && role!=AuthRole::Admin) ||
       (server.arg("user")=="viewer" && role!=AuthRole::Viewer)) {
      server.requestAuthentication(BASIC_AUTH,"ESP32 Climate Camera");
      return;
    }
    sendMessage(200,true,"Учётная запись переключена");
  });
  server.on("/api/status",HTTP_GET,handleStatus);
  server.on("/api/events",HTTP_GET,handleEvents);
  server.on("/api/config",HTTP_GET,handleGetConfig);
  server.on("/api/config",HTTP_POST,handleSetConfig);
  server.on("/api/control",HTTP_POST,handleControl);
  server.on("/update",HTTP_GET,[](){
    AuthRole role=AuthRole::None;
    if(!authorized(false,&role))return;
    if(role!=AuthRole::Admin){sendMessage(403,false,"Для обновления OTA требуется учётная запись администратора");return;}
    noCache();server.send_P(200,"text/html; charset=utf-8",UPDATE_HTML);
  });
  server.on("/update",HTTP_POST,finishOtaRequest,uploadOta);
  server.onNotFound([](){if(!authorized())return;sendMessage(404,false,"Not found");});
  server.begin();
}

static void startFallbackAp() {
  if(apActive)return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  if(WiFi.softAP(CC_AP_SSID,CC_AP_PASS)) {
    apActive=true;
    Serial.printf("SoftAP active: http://%s/\n",WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("SoftAP start failed: check password (8..63 characters)");
  }
}

static void stopFallbackAp() {
  if(!apActive || !CC_AP_FALLBACK_ONLY)return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive=false;
  Serial.println("SoftAP disabled (STA connected)");
}

static void applyStaConfig() {
#if CC_USE_STATIC_IP
  IPAddress localIp(CC_STATIC_IP);
  IPAddress gateway(CC_STATIC_GATEWAY);
  IPAddress subnet(CC_STATIC_SUBNET);
  IPAddress dns(CC_STATIC_DNS);
  if(!WiFi.config(localIp,gateway,subnet,dns)) {
    Serial.println("Static IP configuration failed, falling back to DHCP");
  } else {
    Serial.printf("Static IP configured: %s (GW: %s)\n",localIp.toString().c_str(),gateway.toString().c_str());
  }
#endif
}

static void restartMdns() {
  MDNS.end();
  if(MDNS.begin(CC_HOSTNAME))MDNS.addService("http","tcp",CC_HTTP_PORT);
}

static void networkTask(void *) {
  for(unsigned i=0;i<4;++i)snprintf(csrfToken+8*i,9,"%08lx",(unsigned long)esp_random());
  WiFi.persistent(false);
  WiFi.setHostname(CC_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  if(!CC_AP_FALLBACK_ONLY || strlen(CC_WIFI_SSID)==0) {
    startFallbackAp();
  } else {
    WiFi.mode(WIFI_STA);
  }

  if(strlen(CC_WIFI_SSID)) {
    applyStaConfig();
    WiFi.begin(CC_WIFI_SSID,CC_WIFI_PASS);
  }

  setupRoutes();
  restartMdns();
  Serial.printf("Web UI v%s started on port %u (MAC: %s)\n",FW_VERSION,(unsigned)CC_HTTP_PORT,WiFi.macAddress().c_str());

  bool wasConnected=false;
  uint32_t lastReconnect=millis();
  uint32_t disconnectedSince=millis();

  for(;;) {
    server.handleClient();
    uint32_t now=millis();
    bool connected=(WiFi.status()==WL_CONNECTED);

    if(connected!=wasConnected) {
      wasConnected=connected;
      restartMdns();
      if(connected) {
        configTzTime(CC_TZ_INFO,CC_NTP_SERVER_1,CC_NTP_SERVER_2,CC_NTP_SERVER_3);
        Serial.printf("STA connected! IP: %s | GW: %s | RSSI: %d dBm | NTP TZ=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      (int)WiFi.RSSI(),CC_TZ_INFO);
        stopFallbackAp();
      } else {
        disconnectedSince=now;
        Serial.println("STA disconnected from router");
      }
    }

    if(!connected && CC_AP_FALLBACK_ONLY && (uint32_t)(now-disconnectedSince)>=45000) {
      startFallbackAp();
    }

    if(strlen(CC_WIFI_SSID) && !connected && (uint32_t)(now-lastReconnect)>=30000) {
      lastReconnect=now;
      applyStaConfig();
      WiFi.begin(CC_WIFI_SSID,CC_WIFI_PASS);
    }

    if(ota.maintenance && !ota.finished && (uint32_t)(now-ota.lastChunkMs)>15000) {
      failOta(408,"Upload timed out; system remains off");
      releaseOta();
    }
    if(restartPending && (int32_t)(now-restartAt)>=0)ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool startNetworkTask() {
  if(networkHandle)return true;
  return xTaskCreatePinnedToCore(networkTask,"ClimateNet",12288,nullptr,1,&networkHandle,0)==pdPASS;
}
