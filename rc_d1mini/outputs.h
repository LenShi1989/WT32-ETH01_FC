#pragma once
#include <Arduino.h>
#include "settings.h"

// DO 輸出（繼電器）：開關、定時排程、點動三種模式
namespace Outputs {

struct State {
  bool on;                // 目前輸出（邏輯值，已考慮觸發準位）
  uint32_t pulseLeftMs;   // 點動剩餘時間
};

void begin(const DoConfig &cfg);   // 先把腳位設成「關」再啟動輸出任務
void setConfig(const DoConfig &cfg);
bool setSwitch(int ch, bool on);   // 僅「開關」模式有效
bool trigger(int ch);              // 僅「點動」模式有效
State state(int ch);

bool timeValid();                  // 系統時間是否已校正（NTP 或瀏覽器）
bool isAllowedPin(int pin);
const int8_t *allowedPins(int &count);

}
