#pragma once
#include <Arduino.h>

struct PidGains {
  float kp, ki, kd;
};

// 以二進位整塊存進 NVS，修改欄位時請遞增 FLIGHT_CFG_VERSION
struct FlightConfig {
  uint16_t version;
  PidGains roll, pitch, yaw;   // 角速度環（輸出單位：微秒 / 每 度/秒 誤差）
  float levelKp;               // 角度環 P（度 -> 度/秒）
  float maxAngle;              // 搖桿滿舵對應傾角（度）
  float maxYawRate;            // 搖桿滿舵對應偏航角速度（度/秒）
  float iLimit;                // 積分上限（微秒）
  float trimRoll, trimPitch;   // 水平校正（度）
};

struct NetConfig {
  String wifiSsid, wifiPass;
  bool ethDhcp;
  String ethIp, ethGw, ethMask, ethDns;
};

struct UserConfig {
  String user, pass;   // user 為空 = 不需登入
};

namespace Settings {
extern NetConfig net;
extern UserConfig user;
extern FlightConfig flight;

void begin();
void saveWifi();
void saveEth();
void saveUser();
void saveFlight();
FlightConfig flightDefaults();
}
