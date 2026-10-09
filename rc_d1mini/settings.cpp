#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace {
constexpr uint16_t STICK_CAL_VERSION = 1;
constexpr uint16_t DO_CFG_VERSION = 1;
Preferences prefs;
}

namespace Settings {
NetConfig net;
UserConfig user;
StickCal sticks;
DisplayConfig display;
DoConfig outputs;

StickCal stickDefaults() {
  StickCal c{};
  c.version = STICK_CAL_VERSION;
  c.calibrated = false;
  c.deadband = STICK_DEADBAND;
  c.axis[0] = {THROTTLE_RAW_MIN, (THROTTLE_RAW_MIN + THROTTLE_RAW_MAX) / 2, THROTTLE_RAW_MAX, INVERT_THROTTLE};
  c.axis[1] = {2048 - STICK_RAW_SPAN, 2048, 2048 + STICK_RAW_SPAN, INVERT_ROLL};
  c.axis[2] = {2048 - STICK_RAW_SPAN, 2048, 2048 + STICK_RAW_SPAN, INVERT_PITCH};
  c.axis[3] = {2048 - STICK_RAW_SPAN, 2048, 2048 + STICK_RAW_SPAN, INVERT_YAW};
  return c;
}

DoConfig outputDefaults() {
  DoConfig c{};
  c.version = DO_CFG_VERSION;
  const int8_t pins[DO_COUNT] = {DO_DEFAULT_PIN1, DO_DEFAULT_PIN2};
  for (int i = 0; i < DO_COUNT; i++) {
    DoChannel &d = c.ch[i];
    snprintf(d.name, sizeof(d.name), "DO%d", i + 1);
    d.pin = pins[i];
    d.activeLow = false;
    d.mode = DO_SWITCH;
    d.pulseMs = 1000;
    for (int s = 0; s < DO_SCHEDULES; s++) d.sched[s] = {false, 0x7F, 8 * 60, 17 * 60};
  }
  return c;
}

void begin() {
  prefs.begin("cfg", false);
  net.wifiSsid = prefs.getString("wifi_ssid", "");
  net.wifiPass = prefs.getString("wifi_pass", "");
  net.targetIp = prefs.getString("target_ip", "");
  net.targetPort = prefs.getUShort("target_port", LINK_FC_PORT);
  user.user = prefs.getString("user", "");
  user.pass = prefs.getString("pass", "");

  display.mode = prefs.getUChar("oled_mode", DISPLAY_MAIN);
  display.rotateSec = prefs.getUChar("oled_rot", 5);
  if (display.mode > DISPLAY_AUTO) display.mode = DISPLAY_MAIN;
  display.rotateSec = constrain(display.rotateSec, 2, 30);

  outputs = outputDefaults();
  if (prefs.getBytesLength("do") == sizeof(DoConfig)) {
    DoConfig c;
    prefs.getBytes("do", &c, sizeof(c));
    if (c.version == DO_CFG_VERSION) outputs = c;
  }

  sticks = stickDefaults();
  if (prefs.getBytesLength("sticks") == sizeof(StickCal)) {
    StickCal c;
    prefs.getBytes("sticks", &c, sizeof(c));
    if (c.version == STICK_CAL_VERSION) sticks = c;
  }
}

void saveDisplay() {
  prefs.putUChar("oled_mode", display.mode);
  prefs.putUChar("oled_rot", display.rotateSec);
}

void saveOutputs() {
  prefs.putBytes("do", &outputs, sizeof(outputs));
}

void saveSticks() {
  prefs.putBytes("sticks", &sticks, sizeof(sticks));
}

void saveWifi() {
  prefs.putString("wifi_ssid", net.wifiSsid);
  prefs.putString("wifi_pass", net.wifiPass);
}

void saveTarget() {
  prefs.putString("target_ip", net.targetIp);
  prefs.putUShort("target_port", net.targetPort);
}

void saveUser() {
  prefs.putString("user", user.user);
  prefs.putString("pass", user.pass);
}
}
