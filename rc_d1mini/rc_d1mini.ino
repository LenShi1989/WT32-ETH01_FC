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
#include "outputs.h"
#include <SPIFFS.h>

// ---------------------------------------------------------------------------
// 韌體版本
//   1.2.0  2 組 DO 輸出（開關／定時排程／點動），腳位可在網頁設定
//   1.1.0  搖桿校正、OLED 畫面切換、初始免登入、三種主題
//   1.0.0  初版：搖桿 UDP 遙控、OLED、WiFi 設定、OTA、帳密設定
// ---------------------------------------------------------------------------
const char FW_VERSION[] = "1.2.0";
const char FW_BUILD[] = __DATE__ " " __TIME__;

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[SYS] %s v%s (%s)\n", DEVICE_NAME, FW_VERSION, FW_BUILD);

  Display::begin();
  Display::showMessage("RC starting...", "Hands off sticks");
  Settings::begin();
  Outputs::begin(Settings::outputs);  // DO 開機一律為「關」
  if (!SPIFFS.begin(true)) Serial.println("[SYS] SPIFFS 掛載失敗");

  Sticks::begin(Settings::sticks);  // 套用搖桿校正（未校正時取中點）
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
