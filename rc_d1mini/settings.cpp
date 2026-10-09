#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace {
constexpr uint16_t STICK_CAL_VERSION = 1;
Preferences prefs;
}

namespace Settings {
NetConfig net;
UserConfig user;
StickCal sticks;
DisplayConfig display;

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

void begin() {
  prefs.begin("cfg", false);
  net.wifiSsid = prefs.getString("wifi_ssid", "");
  net.wifiPass = prefs.getString("wifi_pass", "");
  net.targetIp = prefs.getString("target_ip", "");
  net.targetPort = prefs.getUShort("target_port", LINK_FC_PORT);
  user.user = prefs.getString("user", DEFAULT_USER);
  user.pass = prefs.getString("pass", DEFAULT_PASS);

  display.mode = prefs.getUChar("oled_mode", DISPLAY_MAIN);
  display.rotateSec = prefs.getUChar("oled_rot", 5);
  if (display.mode > DISPLAY_AUTO) display.mode = DISPLAY_MAIN;
  display.rotateSec = constrain(display.rotateSec, 2, 30);

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
