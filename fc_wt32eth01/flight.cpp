// 自主平衡：MPU-6050 互補濾波求姿態 → 角度環(P) → 角速度環(PID) → X 型混控 → 電調
#include "flight.h"
#include "config.h"
#include <Wire.h>
#include <math.h>

namespace {

constexpr float GYRO_LSB = 65.5f;     // ±500 dps
constexpr float ACC_LSB = 4096.0f;    // ±8 g
constexpr float RAD2DEG = 57.2957795f;
constexpr float COMP_ALPHA = 0.99f;   // 互補濾波：陀螺儀權重
constexpr float PID_OUT_LIMIT = 400.0f;
constexpr float MAX_LEVEL_RATE = 300.0f;
constexpr float D_LPF_HZ = 40.0f;
constexpr int CAL_SAMPLES = 500;
constexpr int GYRO_STILL_RANGE = 100; // 校正期間陀螺儀原始值最大擺動（約 1.5 度/秒）
constexpr int IMU_FAIL_LIMIT = 10;

enum Axis { ROLL, PITCH, YAW };

struct PidState {
  float i, prevMeas, d;
};

const uint8_t motorPins[4] = {PIN_M1, PIN_M2, PIN_M3, PIN_M4};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Flight::Command sharedCmd{};
FlightConfig sharedCfg;
bool cfgDirty = false;
Flight::State sharedState{};
bool lockout = false;
bool calRequest = false;
bool calResultReady = false;
float calTrimRoll = 0, calTrimPitch = 0;

float gyroBias[3] = {0, 0, 0};

// ---------------------------------------------------------------------------
// MPU-6050
// ---------------------------------------------------------------------------
bool mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// raw[0..2] = 加速度 XYZ，raw[3] = 溫度，raw[4..6] = 陀螺儀 XYZ
bool mpuRead(int16_t raw[7]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;
  for (int i = 0; i < 7; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }
  return true;
}

bool imuInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(5);
  if (!mpuWrite(0x6B, 0x80)) return false;  // 重置
  delay(100);
  bool ok = mpuWrite(0x6B, 0x01)             // 時脈源：X 軸陀螺儀 PLL
            && mpuWrite(0x19, 0x00)          // 取樣率 1kHz
            && mpuWrite(0x1A, MPU_DLPF_CFG)  // 數位低通
            && mpuWrite(0x1B, 0x08)          // 陀螺儀 ±500 dps
            && mpuWrite(0x1C, 0x10);         // 加速度 ±8 g
  delay(50);
  return ok;
}

// 開機時的陀螺儀零點校正（阻塞式，迴圈任務啟動前呼叫）
bool calibrateGyroBlocking() {
  for (int attempt = 0; attempt < 5; attempt++) {
    long sum[3] = {0, 0, 0};
    int16_t lo[3] = {INT16_MAX, INT16_MAX, INT16_MAX}, hi[3] = {INT16_MIN, INT16_MIN, INT16_MIN};
    int n = 0;
    for (int s = 0; s < CAL_SAMPLES; s++) {
      int16_t raw[7];
      if (mpuRead(raw)) {
        for (int a = 0; a < 3; a++) {
          int16_t v = raw[4 + a];
          sum[a] += v;
          lo[a] = min(lo[a], v);
          hi[a] = max(hi[a], v);
        }
        n++;
      }
      delay(2);
    }
    if (n < CAL_SAMPLES / 2) return false;
    for (int a = 0; a < 3; a++) gyroBias[a] = (float)sum[a] / n;
    bool still = true;
    for (int a = 0; a < 3; a++) still &= (hi[a] - lo[a]) < GYRO_STILL_RANGE;
    if (still) return true;
    Serial.println("[FC] 陀螺儀校正時偵測到晃動，重試…");
  }
  Serial.println("[FC] 警告：陀螺儀校正時持續晃動，零點可能不準");
  return true;
}

// ---------------------------------------------------------------------------
// 電調
// ---------------------------------------------------------------------------
void writeMotor(int i, uint16_t us) {
  uint32_t duty = (uint32_t)((uint64_t)us * (1u << ESC_RES_BITS) * ESC_FREQ_HZ / 1000000u);
  ledcWrite(motorPins[i], duty);
}

void writeAllMotors(uint16_t us) {
  for (int i = 0; i < 4; i++) writeMotor(i, us);
}

// ---------------------------------------------------------------------------
// PID（D 項取測量值微分，避免設定值跳動造成突波）
// ---------------------------------------------------------------------------
void pidReset(PidState &s, float meas) {
  s.i = 0;
  s.prevMeas = meas;
  s.d = 0;
}

float pidStep(const PidGains &g, PidState &s, float setpoint, float meas, float dt, float iLimit) {
  float err = setpoint - meas;
  s.i = constrain(s.i + g.ki * err * dt, -iLimit, iLimit);
  float dRaw = -(meas - s.prevMeas) / dt;
  s.prevMeas = meas;
  float rc = 1.0f / (2.0f * PI * D_LPF_HZ);
  s.d += (dt / (dt + rc)) * (dRaw - s.d);
  return constrain(g.kp * err + s.i + g.kd * s.d, -PID_OUT_LIMIT, PID_OUT_LIMIT);
}

// ---------------------------------------------------------------------------
// 控制迴圈任務（核心 1，高優先權）
// ---------------------------------------------------------------------------
void flightTask(void *) {
  FlightConfig cfg;
  portENTER_CRITICAL(&mux);
  cfg = sharedCfg;
  portEXIT_CRITICAL(&mux);

  float roll = 0, pitch = 0;
  float rate[3] = {0, 0, 0};
  bool attitudeInit = false;
  PidState pid[3] = {};
  bool armed = false, failsafe = false, imuOk = true, prevArmSw = false;
  const char *disarmReason = "開機";
  int imuFails = 0;
  uint32_t failsafeStart = 0, crashSince = 0;
  float failsafeThrottle = 0;
  uint16_t motor[4] = {MOTOR_OFF_US, MOTOR_OFF_US, MOTOR_OFF_US, MOTOR_OFF_US};
  uint32_t loopMaxUs = 0, prevUs = micros();
  float vbat = 0;
  int vbatDiv = 0;

  // 校正累加
  Flight::CalStatus cal = Flight::CAL_IDLE;
  int calN = 0;
  long calGyro[3];
  int16_t calLo[3], calHi[3];
  double calAcc[3];

  const TickType_t period = pdMS_TO_TICKS(1000 / LOOP_HZ);
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, period);
    uint32_t t0 = micros();
    float dt = constrain((t0 - prevUs) * 1e-6f, 0.001f, 0.02f);
    prevUs = t0;

    Flight::Command cmd;
    bool lock, wantCal;
    portENTER_CRITICAL(&mux);
    cmd = sharedCmd;
    if (cfgDirty) {
      cfg = sharedCfg;
      cfgDirty = false;
    }
    lock = lockout;
    wantCal = calRequest;
    calRequest = false;
    portEXIT_CRITICAL(&mux);
    uint32_t now = millis();

    // ---- IMU ----
    int16_t raw[7];
    if (mpuRead(raw)) {
      imuFails = 0;
      imuOk = true;

      float ax = raw[0] / ACC_LSB, ay = raw[1] / ACC_LSB, az = raw[2] / ACC_LSB;
      // 機體座標：X 前、Y 左、Z 上（MPU-6050 晶片箭頭朝機頭、元件面朝上）
      // 轉成：roll 正 = 右側向下、pitch 正 = 抬頭、yaw 正 = 機頭向右
      rate[ROLL] = (raw[4] - gyroBias[0]) / GYRO_LSB;
      rate[PITCH] = -(raw[5] - gyroBias[1]) / GYRO_LSB;
      rate[YAW] = -(raw[6] - gyroBias[2]) / GYRO_LSB;

      float accRoll = atan2f(ay, az) * RAD2DEG - cfg.trimRoll;
      float accPitch = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD2DEG - cfg.trimPitch;

      if (!attitudeInit) {
        roll = accRoll;
        pitch = accPitch;
        attitudeInit = true;
      }
      roll += rate[ROLL] * dt;
      pitch += rate[PITCH] * dt;
      // 偏航時把傾角在 roll / pitch 之間轉移
      float s = sinf(rate[YAW] * dt / RAD2DEG);
      float r0 = roll;
      roll += pitch * s;
      pitch -= r0 * s;

      float amag = sqrtf(ax * ax + ay * ay + az * az);
      if (amag > 0.85f && amag < 1.15f) {  // 有額外加速度時不信任加速度計
        roll = COMP_ALPHA * roll + (1 - COMP_ALPHA) * accRoll;
        pitch = COMP_ALPHA * pitch + (1 - COMP_ALPHA) * accPitch;
      }

      // ---- 校正（陀螺儀零點 + 水平）----
      if (wantCal && !armed && cal != Flight::CAL_RUNNING) {
        cal = Flight::CAL_RUNNING;
        calN = 0;
        for (int a = 0; a < 3; a++) {
          calGyro[a] = 0;
          calAcc[a] = 0;
          calLo[a] = INT16_MAX;
          calHi[a] = INT16_MIN;
        }
      }
      if (cal == Flight::CAL_RUNNING) {
        for (int a = 0; a < 3; a++) {
          calGyro[a] += raw[4 + a];
          calLo[a] = min(calLo[a], raw[4 + a]);
          calHi[a] = max(calHi[a], raw[4 + a]);
          calAcc[a] += raw[a];
        }
        if (++calN >= CAL_SAMPLES) {
          bool still = true;
          for (int a = 0; a < 3; a++) still &= (calHi[a] - calLo[a]) < GYRO_STILL_RANGE;
          if (still) {
            for (int a = 0; a < 3; a++) gyroBias[a] = (float)calGyro[a] / calN;
            float mx = calAcc[0] / calN, my = calAcc[1] / calN, mz = calAcc[2] / calN;
            cfg.trimRoll = atan2f(my, mz) * RAD2DEG;
            cfg.trimPitch = atan2f(mx, sqrtf(my * my + mz * mz)) * RAD2DEG;
            roll = pitch = 0;
            portENTER_CRITICAL(&mux);
            sharedCfg.trimRoll = calTrimRoll = cfg.trimRoll;
            sharedCfg.trimPitch = calTrimPitch = cfg.trimPitch;
            calResultReady = true;
            portEXIT_CRITICAL(&mux);
            cal = Flight::CAL_DONE;
          } else {
            cal = Flight::CAL_FAILED;
          }
        }
      }
    } else if (++imuFails >= IMU_FAIL_LIMIT) {
      imuOk = false;
    }

    // ---- 連線 / 解鎖 / 失控保護 ----
    bool linkOk = cmd.stamp != 0 && (now - cmd.stamp) < LINK_TIMEOUT_MS;
    bool armSw = linkOk && cmd.armSwitch;
    bool calibrating = cal == Flight::CAL_RUNNING;

    auto disarm = [&](const char *why) {
      armed = false;
      failsafe = false;
      disarmReason = why;
    };

    if (!armed) {
      if (armSw && !prevArmSw && cmd.throttle < ARM_THROTTLE_MAX && imuOk && !lock && !calibrating &&
          fabsf(roll) < MAX_ARM_TILT_DEG && fabsf(pitch) < MAX_ARM_TILT_DEG) {
        armed = true;
        failsafe = false;
        crashSince = 0;
        disarmReason = "";
        for (int a = 0; a < 3; a++) pidReset(pid[a], rate[a]);
      }
    } else if (lock) {
      disarm("系統鎖定 (OTA/重啟)");
    } else if (!imuOk) {
      disarm("IMU 讀取失敗");
    } else if (linkOk) {
      if (!cmd.armSwitch) disarm("遙控器上鎖");
      else failsafe = false;
    } else if (!failsafe) {
      failsafe = true;
      failsafeStart = now;
      failsafeThrottle = cmd.throttle;
    } else if (now - failsafeStart >= FAILSAFE_DESCENT_MS) {
      disarm("失控保護");
    }
    prevArmSw = armSw;

    if (armed && (fabsf(roll) > CRASH_TILT_DEG || fabsf(pitch) > CRASH_TILT_DEG)) {
      if (!crashSince) crashSince = now ? now : 1;
      else if (now - crashSince > CRASH_TIME_MS) disarm("傾斜過大 (翻機保護)");
    } else {
      crashSince = 0;
    }

    // ---- 控制 ----
    if (armed) {
      float thr, tRoll, tPitch, tYawRate;
      if (failsafe) {
        float k = 1.0f - (float)(now - failsafeStart) / FAILSAFE_DESCENT_MS;
        thr = failsafeThrottle * max(k, 0.0f);
        tRoll = tPitch = tYawRate = 0;
      } else {
        thr = cmd.throttle;
        tRoll = cmd.roll / 500.0f * cfg.maxAngle;
        tPitch = -cmd.pitch / 500.0f * cfg.maxAngle;  // 桿往前推 = 低頭
        tYawRate = cmd.yaw / 500.0f * cfg.maxYawRate;
      }

      float base = MOTOR_IDLE_US + thr / 1000.0f * (THROTTLE_MAX_US - MOTOR_IDLE_US);
      float out[3] = {0, 0, 0};

      if (thr >= THROTTLE_PID_MIN) {
        float spRoll = constrain(cfg.levelKp * (tRoll - roll), -MAX_LEVEL_RATE, MAX_LEVEL_RATE);
        float spPitch = constrain(cfg.levelKp * (tPitch - pitch), -MAX_LEVEL_RATE, MAX_LEVEL_RATE);
        out[ROLL] = pidStep(cfg.roll, pid[ROLL], spRoll, rate[ROLL], dt, cfg.iLimit);
        out[PITCH] = pidStep(cfg.pitch, pid[PITCH], spPitch, rate[PITCH], dt, cfg.iLimit);
        out[YAW] = pidStep(cfg.yaw, pid[YAW], tYawRate, rate[YAW], dt, cfg.iLimit);
      } else {
        for (int a = 0; a < 3; a++) pidReset(pid[a], rate[a]);
      }

      // X 型混控（M1 右後 CW、M2 右前 CCW、M3 左後 CCW、M4 左前 CW）
      float m[4] = {
        base - out[ROLL] - out[PITCH] - out[YAW],
        base - out[ROLL] + out[PITCH] + out[YAW],
        base + out[ROLL] - out[PITCH] + out[YAW],
        base + out[ROLL] + out[PITCH] - out[YAW],
      };
      // 超出上限時整體下移，保留姿態修正量
      float hi = max(max(m[0], m[1]), max(m[2], m[3]));
      float shift = hi > MOTOR_MAX_US ? hi - MOTOR_MAX_US : 0;
      for (int i = 0; i < 4; i++) motor[i] = (uint16_t)constrain(m[i] - shift, (float)MOTOR_IDLE_US, (float)MOTOR_MAX_US);
    } else {
      for (int i = 0; i < 4; i++) motor[i] = MOTOR_OFF_US;
    }
    for (int i = 0; i < 4; i++) writeMotor(i, motor[i]);

#if VBAT_PIN >= 0
    if (++vbatDiv >= LOOP_HZ / 10) {
      vbatDiv = 0;
      float v = analogReadMilliVolts(VBAT_PIN) * VBAT_DIVIDER / 1000.0f;
      vbat = vbat == 0 ? v : vbat * 0.8f + v * 0.2f;
    }
#else
    (void)vbatDiv;
#endif

    uint32_t loopUs = micros() - t0;
    loopMaxUs = max(loopMaxUs, loopUs);

    portENTER_CRITICAL(&mux);
    Flight::State &st = sharedState;
    st.armed = armed;
    st.failsafe = failsafe;
    st.imuOk = imuOk;
    st.linkOk = linkOk;
    st.lockout = lock;
    st.cal = cal;
    st.roll = roll;
    st.pitch = pitch;
    for (int a = 0; a < 3; a++) st.rate[a] = rate[a];
    for (int i = 0; i < 4; i++) st.motor[i] = motor[i];
    st.vbat = vbat;
    st.loopUs = loopUs;
    st.loopMaxUs = loopMaxUs;
    st.disarmReason = disarmReason;
    portEXIT_CRITICAL(&mux);
  }
}

}  // namespace

namespace Flight {

void beginMotors() {
  for (int i = 0; i < 4; i++) ledcAttach(motorPins[i], ESC_FREQ_HZ, ESC_RES_BITS);
  writeAllMotors(MOTOR_OFF_US);
}

void begin(const FlightConfig &cfg) {
  sharedCfg = cfg;
  sharedState.imuOk = imuInit();
  if (!sharedState.imuOk) {
    Serial.println("[FC] 找不到 MPU-6050，請檢查 I2C 接線");
  } else {
    Serial.println("[FC] 陀螺儀校正中，請保持靜止…");
    sharedState.imuOk = calibrateGyroBlocking();
  }
  xTaskCreatePinnedToCore(flightTask, "flight", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
}

void setConfig(const FlightConfig &cfg) {
  portENTER_CRITICAL(&mux);
  sharedCfg = cfg;
  cfgDirty = true;
  portEXIT_CRITICAL(&mux);
}

void setCommand(const Command &cmd) {
  portENTER_CRITICAL(&mux);
  sharedCmd = cmd;
  portEXIT_CRITICAL(&mux);
}

State getState() {
  portENTER_CRITICAL(&mux);
  State s = sharedState;
  portEXIT_CRITICAL(&mux);
  return s;
}

bool isArmed() {
  return getState().armed;
}

void setLockout(bool on) {
  portENTER_CRITICAL(&mux);
  lockout = on;
  portEXIT_CRITICAL(&mux);
  if (on) {
    // 等控制迴圈完成上鎖並輸出最低油門
    for (int i = 0; i < 20 && isArmed(); i++) delay(5);
    writeAllMotors(MOTOR_OFF_US);
  }
}

bool startCalibration() {
  portENTER_CRITICAL(&mux);
  bool ok = !sharedState.armed && sharedState.imuOk;
  if (ok) calRequest = true;
  portEXIT_CRITICAL(&mux);
  return ok;
}

bool takeCalibration(float &trimRoll, float &trimPitch) {
  portENTER_CRITICAL(&mux);
  bool ready = calResultReady;
  calResultReady = false;
  trimRoll = calTrimRoll;
  trimPitch = calTrimPitch;
  portEXIT_CRITICAL(&mux);
  return ready;
}

}  // namespace Flight
