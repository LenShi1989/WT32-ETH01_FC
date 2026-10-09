#pragma once
#include <Arduino.h>
#include "settings.h"

namespace Flight {

struct Command {
  uint16_t throttle;          // 0 ~ 1000
  int16_t roll, pitch, yaw;   // -500 ~ 500
  bool armSwitch;
  uint32_t stamp;             // millis() 收到時間，0 = 從未收到
};

enum CalStatus : uint8_t { CAL_IDLE, CAL_RUNNING, CAL_DONE, CAL_FAILED };

struct State {
  bool armed, failsafe, imuOk, linkOk, lockout;
  CalStatus cal;
  float roll, pitch;          // 度
  float rate[3];              // 度/秒：roll / pitch / yaw
  uint16_t motor[4];          // 微秒
  float vbat;                 // 伏特，0 = 未接
  uint32_t loopUs, loopMaxUs;
  const char *disarmReason;
};

void beginMotors();                       // 開機第一步：把電調訊號固定在最低
void begin(const FlightConfig &cfg);      // IMU 初始化、陀螺儀零點、啟動控制迴圈
void setConfig(const FlightConfig &cfg);  // 即時套用 PID
void setCommand(const Command &cmd);
State getState();
bool isArmed();
void setLockout(bool on);                 // OTA / 重新啟動前呼叫：強制上鎖並禁止解鎖
bool startCalibration();                  // 陀螺儀零點 + 水平校正，需上鎖且靜置
bool takeCalibration(float &trimRoll, float &trimPitch);  // 校正完成時回傳新的水平修正量

}
