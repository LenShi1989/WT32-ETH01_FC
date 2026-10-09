#pragma once
#include <Arduino.h>

struct NetConfig {
  String wifiSsid, wifiPass;
  String targetIp;       // 空字串 = 自動（廣播）
  uint16_t targetPort;
};

struct UserConfig {
  String user, pass;   // user 為空 = 不需登入
};

// 搖桿校正。軸順序：0 油門、1 Roll、2 Pitch、3 Yaw（與 Sticks::Values.raw 相同）
// 以二進位整塊存進 NVS，修改欄位時請遞增 STICK_CAL_VERSION
struct StickAxisCal {
  uint16_t min, center, max;  // ADC 原始值 0~4095
  bool invert;
};

struct StickCal {
  uint16_t version;
  bool calibrated;            // false = 尚未校正，開機時自動取中點
  uint16_t deadband;          // 回中搖桿的中點死區（ADC 值）
  StickAxisCal axis[4];
};

// OLED 顯示
enum DisplayMode : uint8_t { DISPLAY_MAIN = 0, DISPLAY_LINK = 1, DISPLAY_AUTO = 2 };

struct DisplayConfig {
  uint8_t mode;          // DisplayMode
  uint8_t rotateSec;     // 自動輪播間隔（秒）
};

// DO 輸出。以二進位整塊存進 NVS，修改欄位時請遞增 DO_CFG_VERSION
constexpr int DO_COUNT = 2;
constexpr int DO_SCHEDULES = 4;     // 每組 DO 的排程數

enum DoMode : uint8_t { DO_SWITCH = 0, DO_SCHEDULE = 1, DO_PULSE = 2 };

struct DoSchedule {
  bool enabled;
  uint8_t days;                     // bit0 = 週日 … bit6 = 週六
  uint16_t onMin, offMin;           // 一天中的分鐘數 0~1439；off < on 表示跨午夜
};

struct DoChannel {
  char name[24];                    // UTF-8
  int8_t pin;
  bool activeLow;                   // true = 低電位觸發（多數繼電器模組）
  uint8_t mode;                     // DoMode
  uint32_t pulseMs;                 // 點動保持時間
  DoSchedule sched[DO_SCHEDULES];
};

struct DoConfig {
  uint16_t version;
  DoChannel ch[DO_COUNT];
};

namespace Settings {
extern NetConfig net;
extern UserConfig user;
extern StickCal sticks;
extern DisplayConfig display;
extern DoConfig outputs;

void begin();
void saveWifi();
void saveTarget();
void saveUser();
void saveSticks();
void saveDisplay();
void saveOutputs();
DoConfig outputDefaults();
StickCal stickDefaults();
}
