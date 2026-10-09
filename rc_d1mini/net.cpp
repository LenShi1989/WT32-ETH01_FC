#include "net.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <ESPmDNS.h>

namespace {
bool apActive = false;
uint32_t staDownSince = 0;   // STA 未連線起算時間
uint32_t staUpSince = 0;     // STA 連線起算時間
String apSsid;

void startAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  apActive = true;
  Serial.printf("[NET] AP 開啟：%s  %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("[NET] STA 已連線，關閉 AP");
}

void connectSta() {
  if (Settings::net.wifiSsid.isEmpty()) return;
  WiFi.begin(Settings::net.wifiSsid.c_str(), Settings::net.wifiPass.c_str());
  staDownSince = millis();
  Serial.printf("[NET] 連線 WiFi：%s\n", Settings::net.wifiSsid.c_str());
}

void onEvent(arduino_event_id_t event, arduino_event_info_t) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    Serial.printf("[NET] WiFi IP：%s\n", WiFi.localIP().toString().c_str());
}
}  // namespace

namespace Net {

void begin() {
  Network.onEvent(onEvent);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  apSsid = String(AP_SSID_PREFIX) + suffix;

  WiFi.persistent(false);
  WiFi.setHostname(MDNS_HOST);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // 關閉省電，降低遙控延遲
  if (Settings::net.wifiSsid.isEmpty()) {
    startAp();
  } else {
    WiFi.mode(WIFI_STA);
    connectSta();
  }

  if (MDNS.begin(MDNS_HOST)) MDNS.addService("http", "tcp", 80);
}

void loop() {
  uint32_t now = millis();
  if (WiFi.isConnected()) {
    if (!staUpSince) staUpSince = now;
    staDownSince = 0;
    if (apActive && now - staUpSince > AP_LINGER_MS) stopAp();
  } else {
    staUpSince = 0;
    if (!staDownSince) staDownSince = now;
    if (!apActive && now - staDownSince > STA_CONNECT_TIMEOUT_MS) startAp();
  }
}

void applyWifi() {
  WiFi.disconnect(false, false);
  if (!apActive) WiFi.mode(WIFI_STA);
  connectSta();
}

void clearWifi() {
  WiFi.disconnect(false, true);
  startAp();
}

void startScan() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
}

void fillScan(JsonObject out) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    out["status"] = "scanning";
    return;
  }
  if (n < 0) {
    out["status"] = "idle";
    return;
  }
  out["status"] = "done";
  JsonArray list = out["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["ch"] = WiFi.channel(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
}

void fillStatus(JsonObject wifi) {
  bool staUp = WiFi.isConnected();
  wifi["mode"] = apActive ? "AP+STA" : "STA";
  JsonObject sta = wifi["sta"].to<JsonObject>();
  sta["configured"] = !Settings::net.wifiSsid.isEmpty();
  sta["ssid"] = Settings::net.wifiSsid;
  sta["connected"] = staUp;
  sta["mac"] = WiFi.macAddress();
  if (staUp) {
    sta["ip"] = WiFi.localIP().toString();
    sta["gw"] = WiFi.gatewayIP().toString();
    sta["mask"] = WiFi.subnetMask().toString();
    sta["dns"] = WiFi.dnsIP().toString();
    sta["rssi"] = WiFi.RSSI();
    sta["ch"] = WiFi.channel();
  }
  JsonObject ap = wifi["ap"].to<JsonObject>();
  ap["active"] = apActive;
  ap["ssid"] = apSsid;
  if (apActive) {
    ap["ip"] = WiFi.softAPIP().toString();
    ap["clients"] = WiFi.softAPgetStationNum();
  }
}

}  // namespace Net
