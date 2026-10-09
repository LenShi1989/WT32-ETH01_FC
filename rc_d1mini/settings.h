#pragma once
#include <Arduino.h>

struct NetConfig {
  String wifiSsid, wifiPass;
  String targetIp;       // 空字串 = 自動（廣播）
  uint16_t targetPort;
};

struct UserConfig {
  String user, pass;
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

namespace Settings {
extern NetConfig net;
extern UserConfig user;
extern StickCal sticks;

void begin();
void saveWifi();
void saveTarget();
void saveUser();
void saveSticks();
StickCal stickDefaults();
}
