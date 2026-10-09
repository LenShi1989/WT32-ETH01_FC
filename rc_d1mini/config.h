#pragma once
#include <Arduino.h>

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "本專案需要 Arduino-ESP32 核心 3.x 版"
#endif

// ---------------------------------------------------------------------------
// 裝置
// ---------------------------------------------------------------------------
#define FW_VERSION   "1.0.0"
#define DEVICE_NAME  "RC-Remote"
#define MDNS_HOST    "rc-remote"        // http://rc-remote.local
#define AP_SSID_PREFIX "RC-Remote-"
#define AP_PASSWORD  "12345678"
#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"

// ---------------------------------------------------------------------------
// 腳位（ESP32 D1 mini）
// 搖桿必須接 ADC1（GPIO32~39），WiFi 開啟時 ADC2 無法使用
// ---------------------------------------------------------------------------
#define PIN_THROTTLE 36    // SVP
#define PIN_YAW      39    // SVN
#define PIN_PITCH    34
#define PIN_ROLL     35
#define PIN_ARM      25    // 解鎖撥動開關，接 GND = 解鎖（內部上拉）

#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define OLED_ADDR    0x3C

// 搖桿反向（依實際安裝方向調整）
#define INVERT_THROTTLE false
#define INVERT_ROLL     false
#define INVERT_PITCH    false
#define INVERT_YAW      false

// 油門 ADC 範圍（0~4095），兩端各留一點死區
#define THROTTLE_RAW_MIN 150
#define THROTTLE_RAW_MAX 3950
// 回中搖桿：開機時取中點，±此值內視為 0
#define STICK_DEADBAND   60
#define STICK_RAW_SPAN   1900   // 中點到端點的 ADC 差值

// ---------------------------------------------------------------------------
// 遙控連線
// ---------------------------------------------------------------------------
#define LINK_FC_PORT       4210  // 飛控接收埠（預設值，可在網頁修改）
#define LINK_LOCAL_PORT    4211  // 本機接收遙測
#define LINK_SEND_HZ       50
#define LINK_TIMEOUT_MS    1000  // 超過此時間沒收到遙測 → 顯示未連線
#define LINK_RELEARN_MS    3000  // 自動模式下失聯多久後重新廣播

// ---------------------------------------------------------------------------
// 網路
// ---------------------------------------------------------------------------
#define STA_CONNECT_TIMEOUT_MS 20000
#define AP_LINGER_MS           30000
