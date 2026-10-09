#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace {
constexpr uint16_t FLIGHT_CFG_VERSION = 1;
Preferences prefs;
}

namespace Settings {
NetConfig net;
UserConfig user;
FlightConfig flight;

FlightConfig flightDefaults() {
  FlightConfig c{};
  c.version = FLIGHT_CFG_VERSION;
  c.roll = {1.0f, 5.0f, 0.05f};
  c.pitch = c.roll;
  c.yaw = {3.0f, 2.0f, 0.0f};
  c.levelKp = 4.0f;
  c.maxAngle = 25.0f;
  c.maxYawRate = 150.0f;
  c.iLimit = 150.0f;
  c.trimRoll = 0;
  c.trimPitch = 0;
  return c;
}

void begin() {
  prefs.begin("cfg", false);

  net.wifiSsid = prefs.getString("wifi_ssid", "");
  net.wifiPass = prefs.getString("wifi_pass", "");
  net.ethDhcp = prefs.getBool("eth_dhcp", true);
  net.ethIp = prefs.getString("eth_ip", "192.168.1.50");
  net.ethGw = prefs.getString("eth_gw", "192.168.1.1");
  net.ethMask = prefs.getString("eth_mask", "255.255.255.0");
  net.ethDns = prefs.getString("eth_dns", "192.168.1.1");

  user.user = prefs.getString("user", "");
  user.pass = prefs.getString("pass", "");

  flight = flightDefaults();
  if (prefs.getBytesLength("flight") == sizeof(FlightConfig)) {
    FlightConfig c;
    prefs.getBytes("flight", &c, sizeof(c));
    if (c.version == FLIGHT_CFG_VERSION) flight = c;
  }
}

void saveWifi() {
  prefs.putString("wifi_ssid", net.wifiSsid);
  prefs.putString("wifi_pass", net.wifiPass);
}

void saveEth() {
  prefs.putBool("eth_dhcp", net.ethDhcp);
  prefs.putString("eth_ip", net.ethIp);
  prefs.putString("eth_gw", net.ethGw);
  prefs.putString("eth_mask", net.ethMask);
  prefs.putString("eth_dns", net.ethDns);
}

void saveUser() {
  prefs.putString("user", user.user);
  prefs.putString("pass", user.pass);
}

void saveFlight() {
  prefs.putBytes("flight", &flight, sizeof(flight));
}
}
