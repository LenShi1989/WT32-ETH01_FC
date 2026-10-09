// 網頁伺服器：SPIFFS 靜態網頁 + JSON API + OTA。設定帳號後所有路徑都需 HTTP Basic 登入。
#include "web.h"
#include "config.h"
#include "settings.h"
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

bool fcFlying() {
  Link::Telem t = Link::telem();
  return t.ok && t.armed;
}

// 飛控解鎖中禁止會中斷遙控連線的操作
bool refuseIfFlying() {
  if (!fcFlying()) return false;
  sendError(409, "飛控已解鎖，無法執行（會中斷遙控），請先上鎖");
  return true;
}

void scheduleReboot() {
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
  doc["device"] = "RC";
  doc["name"] = DEVICE_NAME;
  doc["fw"] = FW_VERSION;
  doc["user"] = Settings::user.user;
  doc["authRequired"] = authRequired();
  JsonObject f = doc["features"].to<JsonObject>();
  f["rc"] = true;
  f["target"] = true;
  f["sticks"] = true;
  f["oled"] = true;
  sendJson(doc);
}

void handleStatus() {
  if (!auth()) return;
  JsonDocument doc;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  Net::fillStatus(doc["wifi"].to<JsonObject>());

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

  Link::fillStatus(doc["rc"].to<JsonObject>());
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
  if (!auth() || refuseIfFlying()) return;
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
  if (!auth() || refuseIfFlying()) return;
  Settings::net.wifiSsid = "";
  Settings::net.wifiPass = "";
  Settings::saveWifi();
  sendOk("已清除 WiFi 連線設定");
  Net::clearWifi();
}

void handleTargetGet() {
  if (!auth()) return;
  JsonDocument doc;
  doc["ip"] = Settings::net.targetIp;
  doc["port"] = Settings::net.targetPort;
  sendJson(doc);
}

void handleTargetSave() {
  if (!auth() || refuseIfFlying()) return;
  String ip = server.arg("ip");
  ip.trim();
  IPAddress a;
  if (!ip.isEmpty() && !a.fromString(ip)) return sendError(400, "IP 位址格式錯誤");
  long port = server.arg("port").toInt();
  if (port < 1 || port > 65535) return sendError(400, "埠號需為 1~65535");
  Settings::net.targetIp = ip;
  Settings::net.targetPort = port;
  Settings::saveTarget();
  Link::applyTarget();
  sendOk("已儲存");
}

// ---------------------------------------------------------------------------
// 搖桿校正
void handleSticksGet() {
  if (!auth()) return;
  StickCal c = Sticks::getCal();
  Sticks::Values v = Link::sticks();
  uint16_t lo[4], hi[4];
  Sticks::seenRange(lo, hi);

  JsonDocument doc;
  doc["calibrating"] = Sticks::isCalibrating();
  doc["calibrated"] = c.calibrated;
  doc["deadband"] = c.deadband;
  JsonArray axes = doc["axes"].to<JsonArray>();
  for (int a = 0; a < 4; a++) {
    JsonObject o = axes.add<JsonObject>();
    o["min"] = c.axis[a].min;
    o["center"] = c.axis[a].center;
    o["max"] = c.axis[a].max;
    o["invert"] = c.axis[a].invert;
    if (hi[a] >= lo[a]) {
      o["seenMin"] = lo[a];
      o["seenMax"] = hi[a];
    }
  }
  JsonObject live = doc["live"].to<JsonObject>();
  JsonArray raw = live["raw"].to<JsonArray>();
  for (int a = 0; a < 4; a++) raw.add(v.raw[a]);
  JsonArray out = live["out"].to<JsonArray>();
  out.add(v.thr);
  out.add(v.roll);
  out.add(v.pitch);
  out.add(v.yaw);
  live["arm"] = v.arm;
  sendJson(doc);
}

void handleSticksStart() {
  if (!auth() || refuseIfFlying()) return;
  Sticks::startCalibration();
  sendOk("校正開始：請把每支搖桿推到各方向底端");
}

void handleSticksFinish() {
  if (!auth()) return;
  StickCal c;
  String err;
  if (!Sticks::finishCalibration(c, err)) return sendError(400, err);
  Settings::sticks = c;
  Settings::saveSticks();
  sendOk("校正完成並已儲存");
}

void handleSticksCancel() {
  if (!auth()) return;
  Sticks::cancelCalibration();
  sendOk("已取消校正");
}

void handleSticksOptions() {
  if (!auth() || refuseIfFlying()) return;
  StickCal c = Sticks::getCal();
  long db = server.arg("deadband").toInt();
  if (db < 0 || db > 400) return sendError(400, "死區需為 0~400");
  c.deadband = db;
  for (int a = 0; a < 4; a++) c.axis[a].invert = server.arg("inv" + String(a)) == "1";
  Sticks::setCal(c);
  // 只更新選項，不把開機自動取得的中點當成校正結果存起來
  Settings::sticks.deadband = c.deadband;
  for (int a = 0; a < 4; a++) Settings::sticks.axis[a].invert = c.axis[a].invert;
  Settings::saveSticks();
  sendOk("已儲存");
}

void handleSticksReset() {
  if (!auth() || refuseIfFlying()) return;
  Sticks::cancelCalibration();
  Settings::sticks = Settings::stickDefaults();
  Settings::saveSticks();
  Sticks::setCal(Sticks::autoCenter(Settings::sticks));
  sendOk("已恢復預設值，並以目前位置作為中點");
}

// ---------------------------------------------------------------------------
// OLED 顯示
void handleDisplayGet() {
  if (!auth()) return;
  JsonDocument doc;
  doc["mode"] = Settings::display.mode;
  doc["rotateSec"] = Settings::display.rotateSec;
  sendJson(doc);
}

void handleDisplaySave() {
  if (!auth()) return;
  long mode = server.arg("mode").toInt();
  long sec = server.arg("rotateSec").toInt();
  if (mode < DISPLAY_MAIN || mode > DISPLAY_AUTO) return sendError(400, "顯示模式錯誤");
  if (sec < 2 || sec > 30) return sendError(400, "輪播間隔需為 2~30 秒");
  Settings::display.mode = mode;
  Settings::display.rotateSec = sec;
  Settings::saveDisplay();
  sendOk("已套用");
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
  if (!auth() || refuseIfFlying()) return;
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
      if (fcFlying()) {
        otaMsg = "飛控已解鎖，無法更新";
        return;
      }
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
  sendError(500, "更新失敗：" + (otaMsg.isEmpty() ? String("未收到檔案") : otaMsg));
}
}  // namespace

namespace Web {

void begin() {
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi/scan", HTTP_POST, handleWifiScanStart);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScanResult);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/target", HTTP_GET, handleTargetGet);
  server.on("/api/target", HTTP_POST, handleTargetSave);
  server.on("/api/sticks", HTTP_GET, handleSticksGet);
  server.on("/api/sticks/start", HTTP_POST, handleSticksStart);
  server.on("/api/sticks/finish", HTTP_POST, handleSticksFinish);
  server.on("/api/sticks/cancel", HTTP_POST, handleSticksCancel);
  server.on("/api/sticks/options", HTTP_POST, handleSticksOptions);
  server.on("/api/sticks/reset", HTTP_POST, handleSticksReset);
  server.on("/api/display", HTTP_GET, handleDisplayGet);
  server.on("/api/display", HTTP_POST, handleDisplaySave);
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
