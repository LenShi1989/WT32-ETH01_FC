// SSD1306 128x64 OLED（OLED 字型只有 ASCII，畫面使用英文）
// 兩個畫面：主畫面（狀態、姿態、搖桿）與連線資訊；由網頁設定固定顯示其一或自動輪播
#include "display.h"
#include "config.h"
#include "settings.h"
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

// 依優先順序回傳最重要的狀態
const char *stateText(const Sticks::Values &s, const Link::Telem &t) {
  if (s.calibrating) return "CALIBRATE";
  if (!t.ok) return "NO LINK";
  if (t.failsafe) return "FAILSAFE";
  if (t.armed) return "ARMED";
  if (!t.imuOk) return "IMU ERR";
  if (t.lockout) return "LOCKED";
  if (s.arm) return "ARM SW!";  // 開關已 ON 但飛控未解鎖：先撥回 OFF、油門拉到底
  return "DISARMED";
}

void printRight(int y, const char *text) {
  oled.setCursor(128 - strlen(text) * 6, y);
  oled.print(text);
}

// ---------------------------------------------------------------------------
// 主畫面：WiFi / 狀態（大字）/ 飛控姿態 / 搖桿
void drawMain(const Sticks::Values &s, const Link::Telem &t) {
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

  // 第 2 行：狀態（大字）
  oled.setTextSize(2);
  oled.setCursor(0, 12);
  oled.print(stateText(s, t));
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
}

// ---------------------------------------------------------------------------
// 連線資訊：飛控 IP / 目標位址 / 模式與訊號 / 封包速率 / 延遲 / 飛控遙測
void drawLink(const Sticks::Values &s, const Link::Telem &t) {
  Link::Stats st = Link::stats();

  // 標題列（反白）
  oled.fillRect(0, 0, 128, 9, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(1, 1);
  oled.print("LINK");
  printRight(1, stateText(s, t));
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 12);
  oled.print("FC ");
  if (st.fcKnown) oled.print(st.fcIp);
  else oled.print("--");

  oled.setCursor(0, 21);
  oled.print("> ");
  oled.print(st.target);
  oled.printf(":%u", st.port);

  oled.setCursor(0, 30);
  oled.print(st.fixed ? "FIXED" : st.broadcast ? "BCAST" : "AUTO");
  oled.setCursor(42, 30);
  if (WiFi.isConnected()) oled.printf("RSSI %ddBm", WiFi.RSSI());
  else oled.print("WiFi down");

  oled.setCursor(0, 39);
  oled.printf("TX %2lu/s  RX %2lu/s", st.txRate, st.rxRate);

  oled.setCursor(0, 48);
  if (t.age == UINT32_MAX) oled.print("Last --");
  else if (t.age < 10000) oled.printf("Last %lums", t.age);
  else oled.printf("Last %lus", t.age / 1000);
  if (t.ok) {
    oled.setCursor(72, 48);
    oled.printf("Yaw%+4d", t.yawRate);
  }

  oled.setCursor(0, 57);
  if (t.ok) {
    oled.printf("R%+5.1f P%+5.1f", t.roll, t.pitch);
    if (t.vbat > 0) oled.printf(" %4.1fV", t.vbat);
  }
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

  const DisplayConfig &cfg = Settings::display;
  bool showLink;
  if (s.calibrating) showLink = false;  // 校正時固定顯示主畫面（有搖桿橫條）
  else if (cfg.mode == DISPLAY_AUTO) showLink = (lastDraw / (cfg.rotateSec * 1000UL)) % 2;
  else showLink = cfg.mode == DISPLAY_LINK;

  oled.clearDisplay();
  oled.setTextSize(1);
  if (showLink) drawLink(s, t);
  else drawMain(s, t);
  oled.display();
}

}  // namespace Display
