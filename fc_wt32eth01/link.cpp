// 遙控連線：接收遙控器 UDP 控制封包、回傳遙測
#include "link.h"
#include "config.h"
#include "protocol.h"
#include "flight.h"
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {
WiFiUDP udp;
IPAddress peerIp;
uint16_t peerPort = 0;
uint32_t lastRx = 0, lastTelem = 0;
uint16_t lastSeq = 0;
uint32_t rxCount = 0, rxRate = 0, rateStamp = 0, badCount = 0;

bool peerActive(uint32_t now) {
  return peerPort != 0 && now - lastRx < LINK_PEER_HOLD_MS;
}

void sendTelemetry() {
  Flight::State st = Flight::getState();
  TelemPacket t{};
  t.magic = LINK_MAGIC_TELEM;
  t.version = LINK_VERSION;
  t.flags = (st.armed ? TELEM_FLAG_ARMED : 0) | (st.failsafe ? TELEM_FLAG_FAILSAFE : 0) |
            (st.imuOk ? TELEM_FLAG_IMU_OK : 0) |
            (st.lockout || st.cal == Flight::CAL_RUNNING ? TELEM_FLAG_LOCKOUT : 0);
  t.ackSeq = lastSeq;
  t.roll = (int16_t)lroundf(st.roll * 10);
  t.pitch = (int16_t)lroundf(st.pitch * 10);
  t.yawRate = (int16_t)lroundf(st.rate[2]);
  t.vbat = (uint16_t)(st.vbat * 1000);
  linkSeal(t);
  udp.beginPacket(peerIp, peerPort);
  udp.write((const uint8_t *)&t, sizeof(t));
  udp.endPacket();
}
}  // namespace

namespace Link {

void begin() {
  udp.begin(LINK_UDP_PORT);
}

void loop() {
  uint32_t now = millis();
  int len;
  while ((len = udp.parsePacket()) > 0) {
    uint8_t buf[64];
    int n = udp.read(buf, sizeof(buf));
    if (len != (int)sizeof(CtrlPacket) || n != len) {
      badCount++;
      continue;
    }
    CtrlPacket p;
    memcpy(&p, buf, sizeof(p));
    if (!linkCheck(p, LINK_MAGIC_CTRL)) {
      badCount++;
      continue;
    }
    // 鎖定第一個遙控器，斷線一段時間後才接受其他來源
    if (peerActive(now) && udp.remoteIP() != peerIp) continue;
    peerIp = udp.remoteIP();
    peerPort = udp.remotePort();
    lastRx = now;
    lastSeq = p.seq;
    rxCount++;

    Flight::Command cmd;
    cmd.throttle = min<uint16_t>(p.throttle, 1000);
    cmd.roll = constrain(p.roll, -500, 500);
    cmd.pitch = constrain(p.pitch, -500, 500);
    cmd.yaw = constrain(p.yaw, -500, 500);
    cmd.armSwitch = p.flags & CTRL_FLAG_ARM;
    cmd.stamp = now ? now : 1;
    Flight::setCommand(cmd);
  }

  if (now - rateStamp >= 1000) {
    rxRate = rxCount;
    rxCount = 0;
    rateStamp = now;
  }

  if (peerActive(now) && now - lastTelem >= TELEMETRY_PERIOD_MS) {
    lastTelem = now;
    sendTelemetry();
  }
}

void fillStatus(JsonObject out) {
  uint32_t now = millis();
  out["port"] = LINK_UDP_PORT;
  out["peer"] = peerActive(now) ? peerIp.toString() : String();
  out["rate"] = rxRate;
  out["bad"] = badCount;
  out["age"] = lastRx ? now - lastRx : -1;
}

}  // namespace Link
