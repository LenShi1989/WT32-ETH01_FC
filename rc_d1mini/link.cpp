// 遙控連線：獨立任務以 50Hz 讀搖桿、送 UDP 控制封包並接收飛控遙測，
// 不受網頁伺服器阻塞影響。
#include "link.h"
#include "config.h"
#include "protocol.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {
WiFiUDP udp;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// 以下由 mux 保護
Sticks::Values lastSticks{};
TelemPacket lastTelem{};
uint32_t lastTelemAt = 0;
IPAddress fcIp;                // 回應遙測的飛控
IPAddress fixedIp;             // 使用者指定的飛控 IP
bool useFixed = false;
uint16_t targetPort = LINK_FC_PORT;
IPAddress currentTarget(255, 255, 255, 255);
uint32_t txRate = 0, rxRate = 0;

void loadTarget() {
  IPAddress ip;
  bool fixed = !Settings::net.targetIp.isEmpty() && ip.fromString(Settings::net.targetIp);
  portENTER_CRITICAL(&mux);
  useFixed = fixed;
  fixedIp = ip;
  targetPort = Settings::net.targetPort;
  portEXIT_CRITICAL(&mux);
}

void linkTask(void *) {
  uint16_t seq = 0;
  uint32_t txCount = 0, rxCount = 0, rateStamp = millis();
  const TickType_t period = pdMS_TO_TICKS(1000 / LINK_SEND_HZ);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, period);
    uint32_t now = millis();

    // ---- 接收遙測 ----
    int len;
    while ((len = udp.parsePacket()) > 0) {
      uint8_t buf[64];
      int n = udp.read(buf, sizeof(buf));
      if (len != (int)sizeof(TelemPacket) || n != len) continue;
      TelemPacket t;
      memcpy(&t, buf, sizeof(t));
      if (!linkCheck(t, LINK_MAGIC_TELEM)) continue;
      IPAddress from = udp.remoteIP();
      portENTER_CRITICAL(&mux);
      bool accept = !useFixed || from == fixedIp;
      if (accept) {
        lastTelem = t;
        lastTelemAt = now ? now : 1;
        fcIp = from;
      }
      portEXIT_CRITICAL(&mux);
      if (accept) rxCount++;
    }

    // ---- 決定目標位址：指定 IP > 自動學到的飛控 IP > 廣播 ----
    Sticks::Values s = Sticks::read();
    IPAddress target;
    uint16_t port;
    portENTER_CRITICAL(&mux);
    lastSticks = s;
    bool fcAlive = lastTelemAt && now - lastTelemAt < LINK_RELEARN_MS;
    if (useFixed) target = fixedIp;
    else if (fcAlive) target = fcIp;
    else target = IPAddress(255, 255, 255, 255);
    currentTarget = target;
    port = targetPort;
    portEXIT_CRITICAL(&mux);

    // ---- 送出控制封包 ----
    if (WiFi.isConnected() || WiFi.softAPgetStationNum() > 0) {
      CtrlPacket p{};
      p.magic = LINK_MAGIC_CTRL;
      p.version = LINK_VERSION;
      p.seq = seq++;
      if (!s.calibrating) {  // 校正中送出油門 0、回中、未解鎖
        p.flags = s.arm ? CTRL_FLAG_ARM : 0;
        p.throttle = s.thr;
        p.roll = s.roll;
        p.pitch = s.pitch;
        p.yaw = s.yaw;
      }
      linkSeal(p);
      if (udp.beginPacket(target, port)) {
        udp.write((const uint8_t *)&p, sizeof(p));
        if (udp.endPacket()) txCount++;
      }
    }

    if (now - rateStamp >= 1000) {
      portENTER_CRITICAL(&mux);
      txRate = txCount;
      rxRate = rxCount;
      portEXIT_CRITICAL(&mux);
      txCount = 0;
      rxCount = 0;
      rateStamp = now;
    }
  }
}
}  // namespace

namespace Link {

void begin() {
  loadTarget();
  udp.begin(LINK_LOCAL_PORT);
  xTaskCreatePinnedToCore(linkTask, "link", 4096, nullptr, 3, nullptr, 1);
}

void applyTarget() {
  loadTarget();
}

Telem telem() {
  Telem r{};
  portENTER_CRITICAL(&mux);
  TelemPacket t = lastTelem;
  uint32_t at = lastTelemAt;
  portEXIT_CRITICAL(&mux);
  uint32_t now = millis();
  r.age = at ? now - at : UINT32_MAX;
  r.ok = at && r.age < LINK_TIMEOUT_MS;
  r.armed = t.flags & TELEM_FLAG_ARMED;
  r.failsafe = t.flags & TELEM_FLAG_FAILSAFE;
  r.imuOk = t.flags & TELEM_FLAG_IMU_OK;
  r.lockout = t.flags & TELEM_FLAG_LOCKOUT;
  r.roll = t.roll / 10.0f;
  r.pitch = t.pitch / 10.0f;
  r.yawRate = t.yawRate;
  r.vbat = t.vbat / 1000.0f;
  return r;
}

Sticks::Values sticks() {
  portENTER_CRITICAL(&mux);
  Sticks::Values s = lastSticks;
  portEXIT_CRITICAL(&mux);
  return s;
}

Stats stats() {
  Stats r;
  portENTER_CRITICAL(&mux);
  r.target = currentTarget;
  r.port = targetPort;
  r.fixed = useFixed;
  r.fcIp = fcIp;
  r.fcKnown = lastTelemAt != 0;
  r.txRate = txRate;
  r.rxRate = rxRate;
  portEXIT_CRITICAL(&mux);
  r.broadcast = !r.fixed && r.target == IPAddress(255, 255, 255, 255);
  return r;
}

String targetText() {
  Stats st = stats();
  String s = st.target.toString() + ":" + st.port;
  if (!st.fixed) s += st.broadcast ? "（廣播）" : "（自動）";
  return s;
}

String fcIpText() {
  Stats st = stats();
  return st.fcKnown ? st.fcIp.toString() : String();
}

void fillStatus(JsonObject out) {
  Sticks::Values s = sticks();
  Telem t = telem();
  JsonObject sk = out["sticks"].to<JsonObject>();
  sk["thr"] = s.thr;
  sk["roll"] = s.roll;
  sk["pitch"] = s.pitch;
  sk["yaw"] = s.yaw;
  JsonArray raw = out["raw"].to<JsonArray>();
  for (int i = 0; i < 4; i++) raw.add(s.raw[i]);
  out["arm"] = s.arm;

  JsonObject l = out["link"].to<JsonObject>();
  l["ok"] = t.ok;
  l["target"] = targetText();
  l["fcIp"] = fcIpText();
  Stats st = stats();
  l["txRate"] = st.txRate;
  l["rxRate"] = st.rxRate;
  if (t.age != UINT32_MAX) l["age"] = t.age;

  JsonObject tm = out["telem"].to<JsonObject>();
  tm["armed"] = t.armed;
  tm["failsafe"] = t.failsafe;
  tm["imuOk"] = t.imuOk;
  tm["lockout"] = t.lockout;
  tm["roll"] = t.roll;
  tm["pitch"] = t.pitch;
  tm["yawRate"] = t.yawRate;
  tm["vbat"] = t.vbat;
}

}  // namespace Link
