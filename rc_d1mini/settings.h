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

namespace Settings {
extern NetConfig net;
extern UserConfig user;

void begin();
void saveWifi();
void saveTarget();
void saveUser();
}
