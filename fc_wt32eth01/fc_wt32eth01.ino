// WT32-ETH01 飛行控制板
//   MPU-6050 姿態 → 自主平衡 PID → 4 顆電調
//   遙控指令：UDP（RJ45 接 900MHz WiFi 模組，或板載 WiFi）
//   網頁設定：SPIFFS 上的 index.html / style.css / app.js
//
// 板子：WT32-ETH01 Ethernet Module，Partition Scheme：Default 4MB with spiffs
#include "config.h"
#include "settings.h"
#include "flight.h"
#include "net.h"
#include "link.h"
#include "web.h"
#include <SPIFFS.h>

// ---------------------------------------------------------------------------
// 韌體版本
//   1.1.4  網頁 1.3.2：遙控器 DO 開關模式改為 toggle switch
//   1.1.3  網頁 1.3.1：主題按鈕改為圖示＋主題名稱
//   1.1.2  網頁 1.3.0：主題改為右上角單一圖示按鈕、重新啟動顯示經過秒數、顯示網頁版本；spiffs.bat 可指定分區大小
//   1.1.1  SPIFFS OTA 先檢查映像大小、失敗不自動格式化；修正 AP SSID 的 MAC 後四碼為 0000
//   1.1.0  網頁初始免登入（設定帳密後才需登入）、明亮／黑暗／玻璃三種主題
//   1.0.0  初版：自主平衡、RJ45／WiFi 設定、PID 設定、OTA、帳密設定
// ---------------------------------------------------------------------------
const char FW_VERSION[] = "1.1.4";
const char FW_BUILD[] = __DATE__ " " __TIME__;

void setup() {
  Flight::beginMotors();  // 先讓電調收到最低油門
  Serial.begin(115200);
  Serial.printf("\n[SYS] %s v%s (%s)\n", DEVICE_NAME, FW_VERSION, FW_BUILD);

  Settings::begin();
  if (!SPIFFS.begin(true)) Serial.println("[SYS] SPIFFS 掛載失敗");

  Flight::begin(Settings::flight);
  Net::begin();
  Link::begin();
  Web::begin();
}

void loop() {
  Net::loop();
  Link::loop();
  Web::loop();

  float trimRoll, trimPitch;
  if (Flight::takeCalibration(trimRoll, trimPitch)) {
    Settings::flight.trimRoll = trimRoll;
    Settings::flight.trimPitch = trimPitch;
    Settings::saveFlight();
    Serial.printf("[FC] 水平校正完成 roll=%.2f pitch=%.2f\n", trimRoll, trimPitch);
  }
  delay(1);
}
