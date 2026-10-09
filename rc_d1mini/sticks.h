#pragma once
#include <Arduino.h>

namespace Sticks {

struct Values {
  uint16_t thr;               // 0 ~ 1000
  int16_t roll, pitch, yaw;   // -500 ~ 500
  bool arm;
  uint16_t raw[4];            // 油門 / roll / pitch / yaw ADC 原始值
};

void begin();                 // 開機取回中搖桿的中點，請勿碰觸搖桿
Values read();

}
