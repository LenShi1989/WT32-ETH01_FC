// 網頁伺服器：SPIFFS 靜態網頁 + JSON API + OTA。設定帳號後所有路徑都需 HTTP Basic 登入。
#include "web.h"
#include "config.h"
#include "settings.h"
#include "flight.h"
#include "net.h"
#include "link.h"
#include <WebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <ArduinoJson.h>

namespace {
WebServer server(80);
uint32_t rebootAt = 0;
bool otaOk = false;
bool otaAuthed = false;
bool otaIsFs = false;
String otaMsg;

// SPIFFS 沒有網頁檔時的救援頁面：可直接上傳 SPIFFS 映像
const char FALLBACK_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="zh-Hant"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>上傳網頁檔</title>
<body style="font-family:sans-serif;max-width:520px;margin:40px auto;padding:0 16px">
<h2>SPIFFS 中找不到 index.html</h2><p>請上傳 SPIFFS 映像檔 (.bin) 或韌體。</p>
<form method="POST" action="/api/ota?type=fs" enctype="multipart/form-data">
<p><b>SPIFFS 映像：</b><input type="file" name="file" accept=".bin"></p><button>上傳 SPIFFS</button></form>
<form method="POST" action="/api/ota?type=fw" enctype="multipart/form-data">
<p><b>韌體：</b><input type="file" name="file" accept=".bin"></p><button>上傳韌體</button></form>
</body></html>)HTML";

// 未設定帳號時不需登入
bool authRequired() {
  return !Settings::user.user.isEmpty();
}

bool authOk() {
  return !authRequired() || server.authenticate(Settings::user.user.c_str(), Settings::user.pass.c_str());
}

bool auth() {
  if (authOk()) return true;
  server.requestAuthentication(BASIC_AUTH, DEVICE_NAME);
  return false;
}

void sendJson(JsonDocument &doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendOk(const char *msg = "ok") {
  JsonDocument doc;
  doc["ok"] = true;
  doc["msg"] = msg;
  sendJson(doc);
}

void sendError(int code, const String &msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["msg"] = msg;
  sendJson(doc, code);
}

// 飛行中禁止會中斷連線或重新啟動的操作
bool refuseIfArmed() {
  if (!Flight::isArmed()) return false;
  sendError(409, "飛行中（已解鎖）無法執行，請先上鎖");
  return true;
}

void scheduleReboot() {
  Flight::setLockout(true);
  rebootAt = millis() + 800;
}

String contentType(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".ico")) return "image/x-icon";
  return "application/octet-stream";
}

float argFloat(const char *name, float def) {
  if (!server.hasArg(name)) return def;
  float v = server.arg(name).toFloat();
  return isfinite(v) ? v : def;
}

// ---------------------------------------------------------------------------
void handleStatic() {
  if (!auth()) return;
  String path = server.uri();
  if (path.endsWith("/")) path += "index.html";
  if (SPIFFS.exists(path)) {
    File f = SPIFFS.open(path, "r");
    server.sendHeader("Cache-Control", "no-cache");
    server.streamFile(f, contentType(path));
    f.close();
  } else if (path == "/index.html") {
    server.send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void handleInfo() {
  if (!auth()) return;
  JsonDocument doc;
  doc["device"] = "FC";
  doc["name"] = DEVICE_NAME;
  doc["fw"] = FW_VERSION;
  doc["user"] = Settings::user.user;
  doc["authRequired"] = authRequired();
  JsonObject f = doc["features"].to<JsonObject>();
  f["eth"] = true;
  f["pid"] = true;
  f["flight"] = true;
  sendJson(doc);
}

void fillFlight(JsonObject o) {
  Flight::State st = Flight::getState();
  o["armed"] = st.armed;
  o["failsafe"] = st.failsafe;
  o["imuOk"] = st.imuOk;
  o["linkOk"] = st.linkOk;
  o["lockout"] = st.lockout;
  static const char *const calNames[] = {"idle", "running", "done", "failed"};
  o["cal"] = calNames[st.cal];
  o["roll"] = roundf(st.roll * 10) / 10;
  o["pitch"] = roundf(st.pitch * 10) / 10;
  o["yawRate"] = roundf(st.rate[2] * 10) / 10;
  JsonArray m = o["motors"].to<JsonArray>();
  for (int i = 0; i < 4; i++) m.add(st.motor[i]);
  o["vbat"] = roundf(st.vbat * 100) / 100;
  o["loopUs"] = st.loopUs;
  o["loopMaxUs"] = st.loopMaxUs;
  o["disarmReason"] = st.disarmReason ? st.disarmReason : "";
  Link::fillStatus(o["link"].to<JsonObject>());
}

void handleStatus() {
  if (!auth()) return;
  JsonDocument doc;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  Net::fillStatus(doc["wifi"].to<JsonObject>(), doc["eth"].to<JsonObject>());

  JsonObject fs = doc["fs"].to<JsonObject>();
  fs["total"] = SPIFFS.totalBytes();
  fs["used"] = SPIFFS.usedBytes();
  JsonArray files = fs["files"].to<JsonArray>();
  File root = SPIFFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    JsonObject o = files.add<JsonObject>();
    o["name"] = String(f.path());
    o["size"] = f.size();
  }

  fillFlight(doc["flight"].to<JsonObject>());
  sendJson(doc);
}

void handleFlight() {
  if (!auth()) return;
  JsonDocument doc;
  fillFlight(doc.to<JsonObject>());
  sendJson(doc);
}

void handleWifiScanStart() {
  if (!auth()) return;
  Net::startScan();
  sendOk();
}

void handleWifiScanResult() {
  if (!auth()) return;
  JsonDocument doc;
  Net::fillScan(doc.to<JsonObject>());
  sendJson(doc);
}

void handleWifiSave() {
  if (!auth() || refuseIfArmed()) return;
  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32) return sendError(400, "SSID 長度需為 1~32 字元");
  String pass = server.arg("pass");
  if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) return sendError(400, "密碼需為 8~63 字元（開放網路請留空）");
  Settings::net.wifiSsid = ssid;
  Settings::net.wifiPass = pass;
  Settings::saveWifi();
  sendOk("已儲存，正在連線…");
  Net::applyWifi();
}

void handleWifiClear() {
  if (!auth() || refuseIfArmed()) return;
  Settings::net.wifiSsid = "";
  Settings::net.wifiPass = "";
  Settings::saveWifi();
  sendOk("已清除 WiFi 連線設定");
  Net::clearWifi();
}

void handleEthSave() {
  if (!auth() || refuseIfArmed()) return;
  bool dhcp = server.arg("dhcp") == "1";
  if (!dhcp) {
    IPAddress a;
    if (!a.fromString(server.arg("ip"))) return sendError(400, "IP 位址格式錯誤");
    if (!a.fromString(server.arg("gw"))) return sendError(400, "閘道格式錯誤");
    if (!a.fromString(server.arg("mask"))) return sendError(400, "子網路遮罩格式錯誤");
    if (!server.arg("dns").isEmpty() && !a.fromString(server.arg("dns"))) return sendError(400, "DNS 格式錯誤");
    Settings::net.ethIp = server.arg("ip");
    Settings::net.ethGw = server.arg("gw");
    Settings::net.ethMask = server.arg("mask");
    Settings::net.ethDns = server.arg("dns");
  }
  Settings::net.ethDhcp = dhcp;
  Settings::saveEth();
  sendOk("已儲存，裝置重新啟動中…");
  scheduleReboot();
}

void fillPid(JsonObject o, const FlightConfig &c) {
  auto gains = [&](const char *k, const PidGains &g) {
    JsonObject a = o[k].to<JsonObject>();
    a["kp"] = g.kp;
    a["ki"] = g.ki;
    a["kd"] = g.kd;
  };
  gains("roll", c.roll);
  gains("pitch", c.pitch);
  gains("yaw", c.yaw);
  o["levelKp"] = c.levelKp;
  o["maxAngle"] = c.maxAngle;
  o["maxYawRate"] = c.maxYawRate;
  o["iLimit"] = c.iLimit;
  o["trimRoll"] = c.trimRoll;
  o["trimPitch"] = c.trimPitch;
}

void handlePidGet() {
  if (!auth()) return;
  JsonDocument doc;
  fillPid(doc.to<JsonObject>(), Settings::flight);
  fillPid(doc["defaults"].to<JsonObject>(), Settings::flightDefaults());
  sendJson(doc);
}

void handlePidSave() {
  if (!auth()) return;
  FlightConfig c = Settings::flight;
  auto readGains = [&](const char *axis, PidGains &g) {
    String p(axis);
    g.kp = constrain(argFloat((p + "_kp").c_str(), g.kp), 0.0f, 20.0f);
    g.ki = constrain(argFloat((p + "_ki").c_str(), g.ki), 0.0f, 50.0f);
    g.kd = constrain(argFloat((p + "_kd").c_str(), g.kd), 0.0f, 2.0f);
  };
  readGains("roll", c.roll);
  readGains("pitch", c.pitch);
  readGains("yaw", c.yaw);
  c.levelKp = constrain(argFloat("levelKp", c.levelKp), 0.0f, 20.0f);
  c.maxAngle = constrain(argFloat("maxAngle", c.maxAngle), 5.0f, 60.0f);
  c.maxYawRate = constrain(argFloat("maxYawRate", c.maxYawRate), 30.0f, 720.0f);
  c.iLimit = constrain(argFloat("iLimit", c.iLimit), 0.0f, 400.0f);
  Settings::flight = c;
  Settings::saveFlight();
  Flight::setConfig(c);
  sendOk("PID 已套用並儲存");
}

void handleCalibrate() {
  if (!auth() || refuseIfArmed()) return;
  if (!Flight::startCalibration()) return sendError(409, "IMU 異常，無法校正");
  sendOk("校正中，請保持機身水平靜止約 2 秒");
}

void handleUserSave() {
  if (!auth()) return;
  String u = server.arg("user"), p = server.arg("pass");
  u.trim();
  if (u.isEmpty() || u.length() > 32) return sendError(400, "帳號長度需為 1~32 字元");
  if (p.length() < 4 || p.length() > 64) return sendError(400, "密碼長度需為 4~64 字元");
  if (u.indexOf(':') >= 0) return sendError(400, "帳號不可包含冒號");
  Settings::user.user = u;
  Settings::user.pass = p;
  Settings::saveUser();
  sendOk("已更新，請使用新帳號密碼重新登入");
}

void handleUserClear() {
  if (!auth()) return;
  Settings::user.user = "";
  Settings::user.pass = "";
  Settings::saveUser();
  sendOk("已清除帳密，之後開啟網頁不需登入");
}

void handleReboot() {
  if (!auth() || refuseIfArmed()) return;
  sendOk("重新啟動中…");
  scheduleReboot();
}

// ---------------------------------------------------------------------------
// OTA：/api/ota?type=fw|fs，multipart 上傳
void handleOtaUpload() {
  HTTPUpload &up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      otaAuthed = authOk();
      otaOk = false;
      otaMsg = "";
      if (!otaAuthed) return;
      if (Flight::isArmed()) {
        otaMsg = "飛行中（已解鎖）無法更新";
        return;
      }
      Flight::setLockout(true);
      otaIsFs = server.arg("type") == "fs";
      if (otaIsFs) SPIFFS.end();
      Serial.printf("[OTA] 開始：%s (%s)\n", up.filename.c_str(), otaIsFs ? "SPIFFS" : "韌體");
      otaOk = Update.begin(UPDATE_SIZE_UNKNOWN, otaIsFs ? U_SPIFFS : U_FLASH);
      if (!otaOk) otaMsg = Update.errorString();
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (otaOk && Update.write(up.buf, up.currentSize) != up.currentSize) {
        otaOk = false;
        otaMsg = Update.errorString();
      }
      break;
    case UPLOAD_FILE_END:
      if (otaOk && !Update.end(true)) {
        otaOk = false;
        otaMsg = Update.errorString();
      }
      if (otaOk) Serial.printf("[OTA] 完成：%u bytes\n", up.totalSize);
      break;
    case UPLOAD_FILE_ABORTED:
      Update.abort();
      otaOk = false;
      otaMsg = "上傳中斷";
      break;
  }
}

void handleOtaDone() {
  if (!otaAuthed) {
    server.requestAuthentication(BASIC_AUTH, DEVICE_NAME);
    return;
  }
  if (otaOk) {
    sendOk("更新成功，裝置重新啟動中…");
    scheduleReboot();
    return;
  }
  if (Update.isRunning()) Update.abort();
  if (otaIsFs) SPIFFS.begin(true);
  Flight::setLockout(false);
  sendError(500, "更新失敗：" + (otaMsg.isEmpty() ? String("未收到檔案") : otaMsg));
}
}  // namespace

namespace Web {

void begin() {
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/flight", HTTP_GET, handleFlight);
  server.on("/api/wifi/scan", HTTP_POST, handleWifiScanStart);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScanResult);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/eth", HTTP_POST, handleEthSave);
  server.on("/api/pid", HTTP_GET, handlePidGet);
  server.on("/api/pid", HTTP_POST, handlePidSave);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/user", HTTP_POST, handleUserSave);
  server.on("/api/user/clear", HTTP_POST, handleUserClear);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.onNotFound(handleStatic);
  server.begin();
}

void loop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    Serial.println("[SYS] 重新啟動");
    delay(50);
    ESP.restart();
  }
}

}  // namespace Web
