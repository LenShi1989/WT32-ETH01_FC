// ESP32 D1 mini 遙控器
//   搖桿 (ADC) + 解鎖開關 → UDP 控制封包 → WiFi（連 900MHz WiFi 模組）→ 飛控
//   SSD1306 OLED 顯示連線與飛控遙測
//   網頁設定：SPIFFS 上的 index.html / style.css / app.js
//
// 板子：WEMOS D1 MINI ESP32，Partition Scheme：Default 4MB with spiffs
// 需要函式庫：ArduinoJson 7.x、Adafruit SSD1306、Adafruit GFX
#include "config.h"
#include "settings.h"
#include "sticks.h"
#include "display.h"
#include "net.h"
#include "link.h"
#include "web.h"
#include <SPIFFS.h>

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[SYS] %s v%s\n", DEVICE_NAME, FW_VERSION);

  Display::begin();
  Display::showMessage("RC starting...", "Hands off sticks");
  Settings::begin();
  if (!SPIFFS.begin(true)) Serial.println("[SYS] SPIFFS 掛載失敗");

  Sticks::begin();   // 取搖桿中點
  Net::begin();
  Link::begin();
  Web::begin();
}

void loop() {
  Net::loop();
  Web::loop();
  Display::loop();
  delay(1);
}
