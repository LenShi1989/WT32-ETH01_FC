#include "sticks.h"
#include "config.h"

namespace {
constexpr int MIN_TRAVEL = 1000;     // 校正時每軸至少要有的行程（ADC 值）
constexpr int MIN_CENTER_GAP = 300;  // 中點到兩端至少要有的距離

const uint8_t pins[4] = {PIN_THROTTLE, PIN_ROLL, PIN_PITCH, PIN_YAW};
const char *const axisNames[4] = {"油門", "Roll", "Pitch", "Yaw"};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
// 以下由 mux 保護
StickCal cal;
bool calibrating = false;
uint16_t seenLo[4], seenHi[4];
uint16_t lastRaw[4];

int readAvg(uint8_t pin) {
  int sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(pin);
  return sum / 4;
}

// 兩端各留約 3% 讓搖桿推到底時確實達到滿量程
int shrink(int span) {
  return max(span - span / 32, 1);
}

uint16_t mapThrottle(int raw, const StickAxisCal &a) {
  int lo = a.min, hi = a.max;
  int margin = (hi - lo) / 64;
  lo += margin;
  hi -= margin;
  long t = hi > lo ? (long)(raw - lo) * 1000 / (hi - lo) : 0;
  t = constrain(t, 0, 1000);
  return a.invert ? 1000 - t : t;
}

int16_t mapCentered(int raw, const StickAxisCal &a, int deadband) {
  int v = raw - a.center;
  if (abs(v) <= deadband) return 0;
  int span = v > 0 ? a.max - a.center : a.center - a.min;
  span = shrink(span - deadband);
  v = v > 0 ? v - deadband : v + deadband;
  int out = constrain((long)v * 500 / span, -500, 500);
  return a.invert ? -out : out;
}
}  // namespace

namespace Sticks {

StickCal autoCenter(StickCal c) {
  for (int a = 1; a < 4; a++) {
    long sum = 0;
    for (int i = 0; i < 32; i++) {
      sum += readAvg(pins[a]);
      delay(2);
    }
    int mid = sum / 32;
    int lo = c.axis[a].center - c.axis[a].min, hi = c.axis[a].max - c.axis[a].center;
    c.axis[a].center = mid;
    c.axis[a].min = max(mid - lo, 0);
    c.axis[a].max = min(mid + hi, 4095);
  }
  return c;
}

void begin(const StickCal &c) {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_ARM, INPUT_PULLUP);
  StickCal use = c.calibrated ? c : autoCenter(c);
  setCal(use);
  Serial.printf("[RC] 搖桿%s 中點 roll=%u pitch=%u yaw=%u\n", c.calibrated ? "（已校正）" : "（自動取中點）",
                use.axis[1].center, use.axis[2].center, use.axis[3].center);
}

Values read() {
  Values v;
  for (int a = 0; a < 4; a++) v.raw[a] = readAvg(pins[a]);
  v.arm = digitalRead(PIN_ARM) == LOW;

  StickCal c;
  portENTER_CRITICAL(&mux);
  c = cal;
  v.calibrating = calibrating;
  for (int a = 0; a < 4; a++) {
    lastRaw[a] = v.raw[a];
    if (calibrating) {
      seenLo[a] = min(seenLo[a], v.raw[a]);
      seenHi[a] = max(seenHi[a], v.raw[a]);
    }
  }
  portEXIT_CRITICAL(&mux);

  v.thr = mapThrottle(v.raw[0], c.axis[0]);
  v.roll = mapCentered(v.raw[1], c.axis[1], c.deadband);
  v.pitch = mapCentered(v.raw[2], c.axis[2], c.deadband);
  v.yaw = mapCentered(v.raw[3], c.axis[3], c.deadband);
  return v;
}

StickCal getCal() {
  portENTER_CRITICAL(&mux);
  StickCal c = cal;
  portEXIT_CRITICAL(&mux);
  return c;
}

void setCal(const StickCal &c) {
  portENTER_CRITICAL(&mux);
  cal = c;
  portEXIT_CRITICAL(&mux);
}

void startCalibration() {
  portENTER_CRITICAL(&mux);
  calibrating = true;
  for (int a = 0; a < 4; a++) {
    seenLo[a] = 4095;
    seenHi[a] = 0;
  }
  portEXIT_CRITICAL(&mux);
}

bool finishCalibration(StickCal &out, String &err) {
  uint16_t lo[4], hi[4], now[4];
  StickCal c;
  portENTER_CRITICAL(&mux);
  bool running = calibrating;
  c = cal;
  for (int a = 0; a < 4; a++) {
    lo[a] = seenLo[a];
    hi[a] = seenHi[a];
    now[a] = lastRaw[a];
  }
  portEXIT_CRITICAL(&mux);

  if (!running) {
    err = "尚未開始校正";
    return false;
  }
  for (int a = 0; a < 4; a++) {
    if (hi[a] < lo[a] || hi[a] - lo[a] < MIN_TRAVEL) {
      err = String(axisNames[a]) + " 行程不足，請把搖桿推到兩端底";
      return false;
    }
  }
  for (int a = 1; a < 4; a++) {
    if (now[a] - lo[a] < MIN_CENTER_GAP || hi[a] - now[a] < MIN_CENTER_GAP) {
      err = String(axisNames[a]) + " 沒有回中，請放開搖桿後再按完成";
      return false;
    }
  }

  c.calibrated = true;
  for (int a = 0; a < 4; a++) {
    c.axis[a].min = lo[a];
    c.axis[a].max = hi[a];
    c.axis[a].center = a == 0 ? (lo[a] + hi[a]) / 2 : now[a];
  }
  portENTER_CRITICAL(&mux);
  cal = c;
  calibrating = false;
  portEXIT_CRITICAL(&mux);
  out = c;
  return true;
}

void cancelCalibration() {
  portENTER_CRITICAL(&mux);
  calibrating = false;
  portEXIT_CRITICAL(&mux);
}

bool isCalibrating() {
  portENTER_CRITICAL(&mux);
  bool r = calibrating;
  portEXIT_CRITICAL(&mux);
  return r;
}

void seenRange(uint16_t lo[4], uint16_t hi[4]) {
  portENTER_CRITICAL(&mux);
  for (int a = 0; a < 4; a++) {
    lo[a] = seenLo[a];
    hi[a] = seenHi[a];
  }
  portEXIT_CRITICAL(&mux);
}

}  // namespace Sticks
