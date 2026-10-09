// 網頁虛擬搖桿：精簡的 WebSocket 伺服器（單一連線），獨立任務執行
//
// 瀏覽器 → 遙控器（binary，10 bytes，little-endian）
//   [0] 版本 1  [1] flags（bit0 解鎖、bit1 要求交回實體搖桿）
//   [2..3] 油門 0~1000  [4..5] roll  [6..7] pitch  [8..9] yaw（-500~500）
// 遙控器 → 瀏覽器（text JSON）
//   {"t":"hello",...} 連線時：目前狀態與最後的油門／解鎖，讓重新整理的頁面能接續
//   {"t":"st",...}    每 100ms：接管狀態與飛控遙測
//   {"t":"err","msg"} 拒絕操作的原因
#include "vstick.h"
#include "config.h"
#include "link.h"
#include "sticks.h"
#include <WiFi.h>
#include <base64.h>
#include <mbedtls/sha1.h>

namespace {
constexpr uint8_t FLAG_ARM = 0x01;
constexpr uint8_t FLAG_RELEASE = 0x02;
constexpr size_t FRAME_LEN = 10;

NetworkServer server(VSTICK_WS_PORT);
NetworkClient client;
char tok[17];

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
// 以下由 mux 保護
bool taken = false;            // 網頁接管中
VStick::Input last{};
uint32_t lastFrameAt = 0;
bool wsConnected = false;

const char *modeName(VStick::Mode m) {
  return m == VStick::ACTIVE ? "active" : m == VStick::LOST ? "lost" : "idle";
}

// ---------------------------------------------------------------------------
// HTTP 升級握手
bool readLine(NetworkClient &c, String &line, uint32_t deadline) {
  line = "";
  while ((int32_t)(millis() - deadline) < 0) {
    while (c.available()) {
      char ch = c.read();
      if (ch == '\n') return true;
      if (ch != '\r' && line.length() < 512) line += ch;
    }
    if (!c.connected()) return false;
    delay(2);
  }
  return false;
}

String queryParam(const String &requestLine, const char *name) {
  int q = requestLine.indexOf('?');
  int end = requestLine.indexOf(' ', q > 0 ? q : 0);
  if (q < 0 || end < 0) return "";
  String query = requestLine.substring(q + 1, end);
  String key = String(name) + "=";
  for (int start = 0; start < (int)query.length();) {
    int amp = query.indexOf('&', start);
    if (amp < 0) amp = query.length();
    String part = query.substring(start, amp);
    if (part.startsWith(key)) return part.substring(key.length());
    start = amp + 1;
  }
  return "";
}

bool handshake(NetworkClient &c) {
  uint32_t deadline = millis() + 1500;
  String line;
  if (!readLine(c, line, deadline) || !line.startsWith("GET ")) return false;
  bool tokenOk = queryParam(line, "t") == tok;

  String key;
  bool upgrade = false;
  while (readLine(c, line, deadline)) {
    if (line.isEmpty()) break;
    int colon = line.indexOf(':');
    if (colon < 0) continue;
    String name = line.substring(0, colon);
    String value = line.substring(colon + 1);
    name.toLowerCase();
    value.trim();
    if (name == "sec-websocket-key") key = value;
    else if (name == "upgrade") upgrade = value.equalsIgnoreCase("websocket");
  }
  if (!tokenOk || !upgrade || key.isEmpty()) {
    c.print("HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    return false;
  }
  key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t sha[20];
  mbedtls_sha1((const unsigned char *)key.c_str(), key.length(), sha);
  c.printf("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Accept: %s\r\n\r\n",
           base64::encode(sha, sizeof(sha)).c_str());
  return true;
}

// ---------------------------------------------------------------------------
// 傳送（伺服器端不加遮罩）
void sendFrame(uint8_t opcode, const uint8_t *data, size_t len) {
  if (!client || !client.connected()) return;
  uint8_t hdr[4];
  size_t h = 0;
  hdr[h++] = 0x80 | opcode;
  if (len < 126) {
    hdr[h++] = len;
  } else {
    hdr[h++] = 126;
    hdr[h++] = len >> 8;
    hdr[h++] = len & 0xFF;
  }
  client.write(hdr, h);
  if (len) client.write(data, len);
}

void sendText(const String &s) {
  sendFrame(0x1, (const uint8_t *)s.c_str(), s.length());
}

void sendError(const char *msg) {
  JsonDocument doc;
  doc["t"] = "err";
  doc["msg"] = msg;
  String s;
  serializeJson(doc, s);
  sendText(s);
}

void sendState(const char *type) {
  VStick::Input in;
  VStick::Mode m = VStick::get(in);
  Link::Telem t = Link::telem();
  JsonDocument doc;
  doc["t"] = type;
  doc["mode"] = modeName(m);
  doc["thr"] = in.thr;
  doc["arm"] = in.arm;
  doc["ok"] = t.ok;
  doc["armed"] = t.armed;
  doc["fs"] = t.failsafe;
  doc["imu"] = t.imuOk;
  doc["lock"] = t.lockout;
  doc["r"] = t.roll;
  doc["p"] = t.pitch;
  doc["y"] = t.yawRate;
  doc["v"] = t.vbat;
  String s;
  serializeJson(doc, s);
  sendText(s);
}

void closeClient() {
  if (client) {
    const uint8_t code[2] = {0x03, 0xE8};  // 1000 正常關閉
    sendFrame(0x8, code, sizeof(code));
    client.stop();
  }
  portENTER_CRITICAL(&mux);
  wsConnected = false;
  portEXIT_CRITICAL(&mux);
}

// ---------------------------------------------------------------------------
// 收到搖桿值
void handleInput(const uint8_t *p, size_t len) {
  if (len != FRAME_LEN || p[0] != 1) return;
  uint8_t flags = p[1];
  VStick::Input in;
  in.thr = min<uint16_t>(p[2] | p[3] << 8, 1000);
  in.roll = constrain((int16_t)(p[4] | p[5] << 8), -500, 500);
  in.pitch = constrain((int16_t)(p[6] | p[7] << 8), -500, 500);
  in.yaw = constrain((int16_t)(p[8] | p[9] << 8), -500, 500);
  in.arm = flags & FLAG_ARM;

  Link::Telem t = Link::telem();
  bool fcArmed = t.ok && t.armed;
  portENTER_CRITICAL(&mux);
  bool wasTaken = taken;
  portEXIT_CRITICAL(&mux);

  if (flags & FLAG_RELEASE) {
    if (wasTaken && fcArmed) return sendError("飛控解鎖中，無法停止虛擬遙控，請先降落並上鎖");
    portENTER_CRITICAL(&mux);
    taken = false;
    portEXIT_CRITICAL(&mux);
    sendState("st");
    return;
  }
  if (!wasTaken) {  // 開始接管：只能在飛控上鎖時開始
    if (fcArmed) return sendError("飛控解鎖中，無法開始虛擬遙控");
    if (Sticks::isCalibrating()) return sendError("搖桿校正中，無法開始虛擬遙控");
    Serial.println("[RC] 網頁虛擬搖桿接管");
  }
  portENTER_CRITICAL(&mux);
  taken = true;
  last = in;
  lastFrameAt = millis();
  portEXIT_CRITICAL(&mux);
}

// 解析已收到的資料（瀏覽器送來的 frame 一定有遮罩）；回傳 false 表示要關閉連線
uint8_t rx[160];
size_t rxLen = 0;
uint32_t lastActivity = 0;     // 本次連線最後收到 frame 的時間（只在 wsTask 使用）

bool parseFrames() {
  while (client.available() && rxLen < sizeof(rx)) rx[rxLen++] = client.read();
  for (;;) {
    if (rxLen < 2) return true;
    uint8_t opcode = rx[0] & 0x0F;
    bool masked = rx[1] & 0x80;
    size_t len = rx[1] & 0x7F;
    if (!masked || len >= 126) return false;  // 只接受小封包
    size_t total = 2 + 4 + len;
    if (rxLen < total) return true;
    lastActivity = millis();
    uint8_t *mask = rx + 2, *payload = rx + 6;
    for (size_t i = 0; i < len; i++) payload[i] ^= mask[i & 3];

    switch (opcode) {
      case 0x2: handleInput(payload, len); break;          // binary：搖桿值
      case 0x8: return false;                               // close
      case 0x9: sendFrame(0xA, payload, len); break;        // ping → pong
      default: break;                                       // text / pong：忽略
    }
    memmove(rx, rx + total, rxLen - total);
    rxLen -= total;
  }
}

void wsTask(void *) {
  server.begin();
  server.setNoDelay(true);
  uint32_t lastState = 0;

  for (;;) {
    NetworkClient incoming = server.accept();
    if (incoming) {
      if (handshake(incoming)) {
        if (client) closeClient();  // 新連線取代舊連線
        client = incoming;
        client.setNoDelay(true);
        rxLen = 0;
        lastActivity = millis();
        portENTER_CRITICAL(&mux);
        wsConnected = true;
        portEXIT_CRITICAL(&mux);
        sendState("hello");
      } else {
        incoming.stop();
      }
    }

    if (client) {
      if (!client.connected() || !parseFrames()) {
        closeClient();
      } else {
        uint32_t now = millis();
        // 連上後一直沒送資料（或停止送）就關閉，避免佔用
        if (now - lastActivity > VSTICK_IDLE_CLOSE_MS) closeClient();
        else if (now - lastState >= 100) {
          lastState = now;
          sendState("st");
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}  // namespace

namespace VStick {

void begin() {
  snprintf(tok, sizeof(tok), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  xTaskCreatePinnedToCore(wsTask, "vstick", 6144, nullptr, 2, nullptr, 1);
}

Mode get(Input &in) {
  uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  in = last;
  Mode m = IDLE;
  if (taken) {
    uint32_t age = now - lastFrameAt;
    if (age < VSTICK_STALE_MS) m = ACTIVE;
    else if (age < VSTICK_RELEASE_MS) m = LOST;
    else taken = false;  // 網頁中斷太久，交回實體搖桿
  }
  portEXIT_CRITICAL(&mux);
  return m;
}

const char *token() {
  return tok;
}

bool clientConnected() {
  portENTER_CRITICAL(&mux);
  bool c = wsConnected;
  portEXIT_CRITICAL(&mux);
  return c;
}

void fillStatus(JsonObject out) {
  Input in;
  Mode m = get(in);
  out["mode"] = modeName(m);
  out["ws"] = clientConnected();
}

}  // namespace VStick
