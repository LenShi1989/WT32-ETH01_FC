#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Net {
void begin();
void loop();
void applyWifi();          // 儲存新的 SSID 後呼叫
void clearWifi();          // 清除連線設定並回到 AP 模式
void startScan();
void fillScan(JsonObject out);
void fillStatus(JsonObject wifi, JsonObject eth);
}
