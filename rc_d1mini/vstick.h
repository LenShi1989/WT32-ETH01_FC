#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// 網頁虛擬搖桿：瀏覽器經 WebSocket（埠 VSTICK_WS_PORT）送搖桿值，取代實體搖桿
//
// 狀態：
//   IDLE    使用實體搖桿
//   ACTIVE  網頁接管，且持續收到搖桿值
//   LOST    網頁接管中但收不到搖桿值 → 遙控器停送控制封包，讓飛控進入失控保護
//           LOST 超過 VSTICK_RELEASE_MS 自動交回實體搖桿
namespace VStick {

enum Mode : uint8_t { IDLE, ACTIVE, LOST };

struct Input {
  uint16_t thr;               // 0 ~ 1000
  int16_t roll, pitch, yaw;   // -500 ~ 500
  bool arm;
};

void begin();
Mode get(Input &in);          // 由 Link 任務每次送封包前呼叫
const char *token();          // WebSocket 連線用的存取權杖（每次開機產生）
bool clientConnected();
void fillStatus(JsonObject out);

}
