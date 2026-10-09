#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace {
Preferences prefs;
}

namespace Settings {
NetConfig net;
UserConfig user;

void begin() {
  prefs.begin("cfg", false);
  net.wifiSsid = prefs.getString("wifi_ssid", "");
  net.wifiPass = prefs.getString("wifi_pass", "");
  net.targetIp = prefs.getString("target_ip", "");
  net.targetPort = prefs.getUShort("target_port", LINK_FC_PORT);
  user.user = prefs.getString("user", DEFAULT_USER);
  user.pass = prefs.getString("pass", DEFAULT_PASS);
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
