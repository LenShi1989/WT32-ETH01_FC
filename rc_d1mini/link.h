#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "sticks.h"

namespace Link {

struct Telem {
  bool ok;                    // 最近 LINK_TIMEOUT_MS 內收到遙測
  bool armed, failsafe, imuOk, lockout;
  float roll, pitch;          // 度
  int16_t yawRate;            // 度/秒
  float vbat;                 // 伏特
  uint32_t age;               // 距上次遙測 ms
};

void begin();                 // 啟動 50Hz 發送任務
void applyTarget();           // 飛控目標設定變更後呼叫
Telem telem();
Sticks::Values sticks();
String targetText();          // 目前送往的位址
String fcIpText();            // 回應的飛控 IP
void fillStatus(JsonObject out);

}
