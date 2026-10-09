#pragma once
#include <Arduino.h>

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "本專案需要 Arduino-ESP32 核心 3.x 版"
#endif

// ---------------------------------------------------------------------------
// 裝置
// ---------------------------------------------------------------------------
#define FW_VERSION   "1.0.0"
#define DEVICE_NAME  "WT32-FC"
#define MDNS_HOST    "wt32-fc"          // http://wt32-fc.local
#define AP_SSID_PREFIX "WT32-FC-"       // 後面接 MAC 末 4 碼
#define AP_PASSWORD  "12345678"         // AP 密碼至少 8 碼
// 登入帳密預設為空：未設定時網頁不需登入，在「使用者設定」頁設定後才啟用

// ---------------------------------------------------------------------------
// 腳位（WT32-ETH01 乙太網路已佔用 GPIO0/16/18/19/21/22/23/25/26/27）
// ---------------------------------------------------------------------------
// MPU-6050（板子預設 I2C 腳位）
#define PIN_I2C_SDA  33
#define PIN_I2C_SCL  32
#define MPU_ADDR     0x68
#define MPU_DLPF_CFG 3                  // 0x1A CONFIG：3 = 約 44Hz 低通

// ESC 訊號，四軸 X 型（俯視，機頭朝前）
//
//      M4(FL,CW)   M2(FR,CCW)     <- 機頭
//              \   /
//               [ ]
//              /   \   .
//      M3(RL,CCW)  M1(RR,CW)
//
// 避開 GPIO12（MTDI 開機 strapping 腳，被拉高會無法開機）
#define PIN_M1  2    // 右後
#define PIN_M2  4    // 右前
#define PIN_M3  14   // 左後
#define PIN_M4  15   // 左前

#define ESC_FREQ_HZ    400              // 一般 PWM 電調；只吃 50Hz 的電調請改 50
#define ESC_RES_BITS   14
#define MOTOR_OFF_US   1000             // 上鎖時輸出
#define MOTOR_IDLE_US  1080             // 解鎖後怠速
#define THROTTLE_MAX_US 1800            // 油門桿滿量程對應值（保留餘量給 PID）
#define MOTOR_MAX_US   2000
static_assert(1000000 / ESC_FREQ_HZ > MOTOR_MAX_US, "ESC PWM 週期必須大於最大脈寬");

// 電池電壓（ADC1 輸入腳，未接請設 -1）。分壓比 = (R1+R2)/R2
#define VBAT_PIN      -1                // 例如 36
#define VBAT_DIVIDER  11.0f             // 100k / 10k

// ---------------------------------------------------------------------------
// 飛行控制
// ---------------------------------------------------------------------------
#define LOOP_HZ              250
#define ARM_THROTTLE_MAX     50          // 油門低於此值才能解鎖（0~1000）
#define THROTTLE_PID_MIN     100         // 油門低於此值不套用 PID、積分歸零
#define MAX_ARM_TILT_DEG     25          // 傾斜超過此角度不允許解鎖
#define CRASH_TILT_DEG       75          // 飛行中傾斜超過此角度…
#define CRASH_TIME_MS        300         // …持續此時間即自動上鎖

// ---------------------------------------------------------------------------
// 遙控連線（UDP，經 RJ45 或 WiFi）
// ---------------------------------------------------------------------------
#define LINK_UDP_PORT        4210
#define LINK_TIMEOUT_MS      500         // 超過此時間沒收到遙控封包 → 失控保護
#define LINK_PEER_HOLD_MS    3000        // 鎖定遙控器 IP，斷線超過此時間才接受新遙控器
#define FAILSAFE_DESCENT_MS  3000        // 失控保護：保持水平並在此時間內把油門降到 0 後上鎖
#define TELEMETRY_PERIOD_MS  50

// ---------------------------------------------------------------------------
// 網路
// ---------------------------------------------------------------------------
#define STA_CONNECT_TIMEOUT_MS 20000     // STA 連不上多久後開啟 AP
#define AP_LINGER_MS           30000     // STA 連上後 AP 再保留多久（讓使用者看到新 IP）
