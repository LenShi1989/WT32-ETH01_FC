// DO 輸出：獨立任務以 50Hz 更新，不受網頁伺服器阻塞影響（點動時間較準確）
#include "outputs.h"
#include "config.h"
#include <time.h>

namespace {
const int8_t ALLOWED[] = DO_ALLOWED_PINS;
constexpr int ALLOWED_COUNT = sizeof(ALLOWED) / sizeof(ALLOWED[0]);
constexpr time_t TIME_VALID_AFTER = 1700000000;  // 2023-11，早於此時間表示尚未校時

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
// 以下由 mux 保護
DoConfig cfg;
bool cfgDirty = false;
bool switchOn[DO_COUNT] = {};
bool pulseActive[DO_COUNT] = {};
uint32_t pulseUntil[DO_COUNT] = {};
bool outOn[DO_COUNT] = {};

// 以下只在輸出任務內使用
int8_t appliedPin[DO_COUNT] = {-1, -1};
bool appliedActiveLow[DO_COUNT] = {};
uint8_t appliedMode[DO_COUNT] = {};

void writePin(int8_t pin, bool activeLow, bool on) {
  if (pin >= 0) digitalWrite(pin, (on != activeLow) ? HIGH : LOW);
}

// 腳位或觸發準位改變時：先放開所有舊腳位，再把新腳位設為輸出「關」
// （分兩輪處理，兩組 DO 互換腳位時才不會把對方剛設定的腳位放掉）
void applyPins(const DoConfig &c) {
  bool changed[DO_COUNT];
  for (int i = 0; i < DO_COUNT; i++) {
    changed[i] = c.ch[i].pin != appliedPin[i] || c.ch[i].activeLow != appliedActiveLow[i];
    if (changed[i] && appliedPin[i] >= 0 && appliedPin[i] != c.ch[i].pin) pinMode(appliedPin[i], INPUT);
  }
  for (int i = 0; i < DO_COUNT; i++) {
    if (!changed[i]) continue;
    const DoChannel &d = c.ch[i];
    if (d.pin >= 0) {
      writePin(d.pin, d.activeLow, false);
      pinMode(d.pin, OUTPUT);
      writePin(d.pin, d.activeLow, false);
    }
    appliedPin[i] = d.pin;
    appliedActiveLow[i] = d.activeLow;
  }
}

bool scheduleActive(const DoChannel &d, const tm &t) {
  int m = t.tm_hour * 60 + t.tm_min;
  int today = t.tm_wday, yesterday = (t.tm_wday + 6) % 7;
  for (const DoSchedule &s : d.sched) {
    if (!s.enabled || s.onMin == s.offMin) continue;
    if (s.onMin < s.offMin) {
      if ((s.days >> today & 1) && m >= s.onMin && m < s.offMin) return true;
    } else {  // 跨午夜：開始日的 on 之後，以及隔天的 off 之前
      if ((s.days >> today & 1) && m >= s.onMin) return true;
      if ((s.days >> yesterday & 1) && m < s.offMin) return true;
    }
  }
  return false;
}

void outputTask(void *) {
  DoConfig c;
  bool sched[DO_COUNT] = {};
  uint32_t lastSchedCheck = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(20));
    uint32_t now = millis();

    bool dirty;
    portENTER_CRITICAL(&mux);
    dirty = cfgDirty;
    cfgDirty = false;
    c = cfg;
    portEXIT_CRITICAL(&mux);
    if (dirty) applyPins(c);

    // 排程每 0.5 秒檢查一次；未校時則排程維持關閉
    if (now - lastSchedCheck >= 500 || dirty) {
      lastSchedCheck = now;
      time_t t = time(nullptr);
      tm lt;
      bool valid = t > TIME_VALID_AFTER && localtime_r(&t, &lt);
      for (int i = 0; i < DO_COUNT; i++) sched[i] = valid && scheduleActive(c.ch[i], lt);
    }

    bool want[DO_COUNT];
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < DO_COUNT; i++) {
      // 切換模式時清除前一個模式的狀態
      if (c.ch[i].mode != appliedMode[i]) {
        switchOn[i] = false;
        pulseActive[i] = false;
      }
      if (pulseActive[i] && (int32_t)(now - pulseUntil[i]) >= 0) pulseActive[i] = false;
      switch (c.ch[i].mode) {
        case DO_SWITCH: want[i] = switchOn[i]; break;
        case DO_PULSE: want[i] = pulseActive[i]; break;
        case DO_SCHEDULE: want[i] = sched[i]; break;
        default: want[i] = false; break;
      }
      outOn[i] = want[i];
    }
    portEXIT_CRITICAL(&mux);

    for (int i = 0; i < DO_COUNT; i++) {
      appliedMode[i] = c.ch[i].mode;
      writePin(appliedPin[i], appliedActiveLow[i], want[i]);
    }
  }
}
}  // namespace

namespace Outputs {

void begin(const DoConfig &c) {
  cfg = c;
  for (int i = 0; i < DO_COUNT; i++) appliedMode[i] = c.ch[i].mode;
  applyPins(c);
  xTaskCreatePinnedToCore(outputTask, "do", 3072, nullptr, 2, nullptr, 1);
}

void setConfig(const DoConfig &c) {
  portENTER_CRITICAL(&mux);
  cfg = c;
  cfgDirty = true;
  portEXIT_CRITICAL(&mux);
}

bool setSwitch(int ch, bool on) {
  if (ch < 0 || ch >= DO_COUNT) return false;
  portENTER_CRITICAL(&mux);
  bool ok = cfg.ch[ch].mode == DO_SWITCH;
  if (ok) switchOn[ch] = on;
  portEXIT_CRITICAL(&mux);
  return ok;
}

bool trigger(int ch) {
  if (ch < 0 || ch >= DO_COUNT) return false;
  portENTER_CRITICAL(&mux);
  bool ok = cfg.ch[ch].mode == DO_PULSE;
  if (ok) {
    pulseActive[ch] = true;
    pulseUntil[ch] = millis() + cfg.ch[ch].pulseMs;
  }
  portEXIT_CRITICAL(&mux);
  return ok;
}

State state(int ch) {
  State s{};
  if (ch < 0 || ch >= DO_COUNT) return s;
  portENTER_CRITICAL(&mux);
  s.on = outOn[ch];
  if (pulseActive[ch]) {
    int32_t left = (int32_t)(pulseUntil[ch] - millis());
    s.pulseLeftMs = left > 0 ? left : 0;
  }
  portEXIT_CRITICAL(&mux);
  return s;
}

bool timeValid() {
  return time(nullptr) > TIME_VALID_AFTER;
}

bool isAllowedPin(int pin) {
  for (int8_t p : ALLOWED)
    if (p == pin) return true;
  return false;
}

const int8_t *allowedPins(int &count) {
  count = ALLOWED_COUNT;
  return ALLOWED;
}

}  // namespace Outputs
