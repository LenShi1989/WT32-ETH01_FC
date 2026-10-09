// WT32-ETH01 飛行控制板
//   MPU-6050 姿態 → 自主平衡 PID → 4 顆電調
//   遙控指令：UDP（RJ45 接 900MHz WiFi 模組，或板載 WiFi）
//   網頁設定：SPIFFS 上的 index.html / style.css / app.js
//
// 板子：WT32-ETH01 Ethernet Module，Partition Scheme：Default 4MB with spiffs
#include "config.h"
#include "settings.h"
#include "flight.h"
#include "net.h"
#include "link.h"
#include "web.h"
#include <SPIFFS.h>

void setup() {
  Flight::beginMotors();  // 先讓電調收到最低油門
  Serial.begin(115200);
  Serial.printf("\n[SYS] %s v%s\n", DEVICE_NAME, FW_VERSION);

  Settings::begin();
  if (!SPIFFS.begin(true)) Serial.println("[SYS] SPIFFS 掛載失敗");

  Flight::begin(Settings::flight);
  Net::begin();
  Link::begin();
  Web::begin();
}

void loop() {
  Net::loop();
  Link::loop();
  Web::loop();

  float trimRoll, trimPitch;
  if (Flight::takeCalibration(trimRoll, trimPitch)) {
    Settings::flight.trimRoll = trimRoll;
    Settings::flight.trimPitch = trimPitch;
    Settings::saveFlight();
    Serial.printf("[FC] 水平校正完成 roll=%.2f pitch=%.2f\n", trimRoll, trimPitch);
  }
  delay(1);
}
