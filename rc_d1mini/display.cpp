// SSD1306 128x64 OLED（OLED 字型只有 ASCII，畫面使用英文）
#include "display.h"
#include "config.h"
#include "link.h"
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool present = false;
uint32_t lastDraw = 0;

// 置中橫條：-500~500
void drawBar(int x, int y, int w, int v) {
  oled.drawRect(x, y, w, 5, SSD1306_WHITE);
  int mid = x + w / 2;
  int len = (long)v * (w / 2 - 1) / 500;
  if (len >= 0) oled.fillRect(mid, y + 1, len, 3, SSD1306_WHITE);
  else oled.fillRect(mid + len, y + 1, -len, 3, SSD1306_WHITE);
}
}  // namespace

namespace Display {

void begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, 400000);
  present = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!present) {
    Serial.println("[RC] 找不到 SSD1306 OLED");
    return;
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextWrap(false);
}

void showMessage(const char *line1, const char *line2) {
  if (!present) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 20);
  oled.print(line1);
  if (line2) {
    oled.setCursor(0, 34);
    oled.print(line2);
  }
  oled.display();
}

void loop() {
  if (!present || millis() - lastDraw < 100) return;
  lastDraw = millis();

  Sticks::Values s = Link::sticks();
  Link::Telem t = Link::telem();
  oled.clearDisplay();
  oled.setTextSize(1);

  // 第 1 行：WiFi 狀態與 IP
  oled.setCursor(0, 0);
  if (WiFi.isConnected()) {
    oled.print(WiFi.localIP());
    oled.setCursor(98, 0);
    oled.print(WiFi.RSSI());
  } else if (WiFi.getMode() & WIFI_AP) {
    oled.print("AP ");
    oled.print(WiFi.softAPIP());
  } else {
    oled.print("WiFi connecting");
  }

  // 第 2 行：飛控狀態（大字）
  oled.setTextSize(2);
  oled.setCursor(0, 12);
  if (s.calibrating) oled.print("CALIBRATE");
  else if (!t.ok) oled.print("NO LINK");
  else if (t.failsafe) oled.print("FAILSAFE");
  else if (t.armed) oled.print("ARMED");
  else if (!t.imuOk) oled.print("IMU ERR");
  else if (t.lockout) oled.print("LOCKED");
  else if (s.arm) oled.print("ARM SW!");   // 開關已 ON 但飛控未解鎖：先撥回 OFF、油門拉到底
  else oled.print("DISARMED");
  oled.setTextSize(1);

  // 第 3 行：飛控姿態與電池
  oled.setCursor(0, 30);
  if (s.calibrating) {
    oled.print("Move sticks to ends");
  } else if (t.ok) {
    oled.printf("R%+5.1f P%+5.1f", t.roll, t.pitch);
    if (t.vbat > 0) oled.printf(" %4.1fV", t.vbat);
  } else {
    oled.print("searching FC...");
  }

  // 第 4~5 行：搖桿
  oled.setCursor(0, 42);
  oled.printf("T%4d", s.thr);
  oled.drawRect(32, 43, 30, 5, SSD1306_WHITE);
  oled.fillRect(33, 44, (long)s.thr * 28 / 1000, 3, SSD1306_WHITE);
  oled.setCursor(66, 42);
  oled.print("Y");
  drawBar(74, 43, 54, s.yaw);
  oled.setCursor(0, 54);
  oled.print("R");
  drawBar(8, 55, 54, s.roll);
  oled.setCursor(66, 54);
  oled.print("P");
  drawBar(74, 55, 54, s.pitch);

  oled.display();
}

}  // namespace Display
