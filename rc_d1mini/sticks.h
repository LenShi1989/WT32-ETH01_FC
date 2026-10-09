#pragma once
#include <Arduino.h>
#include "settings.h"

namespace Sticks {

struct Values {
  uint16_t thr;               // 0 ~ 1000
  int16_t roll, pitch, yaw;   // -500 ~ 500
  bool arm;
  bool calibrating;           // 校正中：Link 會改送油門 0、未解鎖
  uint16_t raw[4];            // 油門 / roll / pitch / yaw ADC 原始值
};

// 套用校正資料；尚未校正時取回中搖桿的中點（請勿碰觸搖桿）
void begin(const StickCal &cal);
Values read();

StickCal getCal();
void setCal(const StickCal &cal);

// 校正流程：start → 把搖桿推到各方向底端 → 放開回中 → finish
void startCalibration();
bool finishCalibration(StickCal &out, String &err);  // 失敗時維持校正狀態，可再試
void cancelCalibration();
bool isCalibrating();
void seenRange(uint16_t lo[4], uint16_t hi[4]);       // 校正期間記錄到的最小 / 最大值

StickCal autoCenter(StickCal cal);  // 以目前位置作為 roll / pitch / yaw 中點

}
