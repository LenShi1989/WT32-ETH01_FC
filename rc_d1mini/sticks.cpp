#include "sticks.h"
#include "config.h"

namespace {
int center[3] = {2048, 2048, 2048};  // roll / pitch / yaw
const uint8_t centerPins[3] = {PIN_ROLL, PIN_PITCH, PIN_YAW};

int readAvg(uint8_t pin) {
  int sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(pin);
  return sum / 4;
}

int16_t mapCentered(int raw, int mid, bool invert) {
  int v = raw - mid;
  if (abs(v) <= STICK_DEADBAND) return 0;
  v = v > 0 ? v - STICK_DEADBAND : v + STICK_DEADBAND;
  int out = constrain((long)v * 500 / (STICK_RAW_SPAN - STICK_DEADBAND), -500, 500);
  return invert ? -out : out;
}
}  // namespace

namespace Sticks {

void begin() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_ARM, INPUT_PULLUP);
  for (int a = 0; a < 3; a++) {
    long sum = 0;
    for (int i = 0; i < 32; i++) {
      sum += readAvg(centerPins[a]);
      delay(2);
    }
    center[a] = sum / 32;
  }
  Serial.printf("[RC] 搖桿中點 roll=%d pitch=%d yaw=%d\n", center[0], center[1], center[2]);
}

Values read() {
  Values v;
  v.raw[0] = readAvg(PIN_THROTTLE);
  v.raw[1] = readAvg(PIN_ROLL);
  v.raw[2] = readAvg(PIN_PITCH);
  v.raw[3] = readAvg(PIN_YAW);

  long t = (long)(v.raw[0] - THROTTLE_RAW_MIN) * 1000 / (THROTTLE_RAW_MAX - THROTTLE_RAW_MIN);
  t = constrain(t, 0, 1000);
  v.thr = INVERT_THROTTLE ? 1000 - t : t;
  v.roll = mapCentered(v.raw[1], center[0], INVERT_ROLL);
  v.pitch = mapCentered(v.raw[2], center[1], INVERT_PITCH);
  v.yaw = mapCentered(v.raw[3], center[2], INVERT_YAW);
  v.arm = digitalRead(PIN_ARM) == LOW;
  return v;
}

}  // namespace Sticks
