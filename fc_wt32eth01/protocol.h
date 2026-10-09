#pragma once
#include <stdint.h>
#include <stddef.h>

// 遙控器 <-> 飛控 UDP 封包格式。
// fc_wt32eth01/protocol.h 與 rc_d1mini/protocol.h 必須保持一致。

#define LINK_MAGIC_CTRL  0x4346   // 遙控器 -> 飛控
#define LINK_MAGIC_TELEM 0x4C54   // 飛控 -> 遙控器
#define LINK_VERSION     1

enum : uint8_t {
  CTRL_FLAG_ARM = 0x01,           // 解鎖開關 ON
};

enum : uint8_t {
  TELEM_FLAG_ARMED    = 0x01,
  TELEM_FLAG_FAILSAFE = 0x02,
  TELEM_FLAG_IMU_OK   = 0x04,
  TELEM_FLAG_LOCKOUT  = 0x08,     // OTA / 校正中，不允許解鎖
};

struct __attribute__((packed)) CtrlPacket {
  uint16_t magic;
  uint8_t  version;
  uint8_t  flags;
  uint16_t seq;
  uint16_t throttle;              // 0 ~ 1000
  int16_t  roll;                  // -500 ~ 500，正 = 向右傾
  int16_t  pitch;                 // -500 ~ 500，正 = 桿往前推（低頭前進）
  int16_t  yaw;                   // -500 ~ 500，正 = 機頭向右轉
  uint16_t crc;
};

struct __attribute__((packed)) TelemPacket {
  uint16_t magic;
  uint8_t  version;
  uint8_t  flags;
  uint16_t ackSeq;                // 最後收到的 CtrlPacket.seq
  int16_t  roll;                  // 0.1 度，正 = 右側向下
  int16_t  pitch;                 // 0.1 度，正 = 抬頭
  int16_t  yawRate;               // 度/秒，正 = 機頭向右
  uint16_t vbat;                  // mV，0 = 未接
  uint16_t crc;
};

inline uint16_t linkCrc16(const uint8_t *d, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(*d++) << 8;
    for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

template <typename T> inline void linkSeal(T &p) {
  p.crc = linkCrc16((const uint8_t *)&p, sizeof(T) - sizeof(p.crc));
}

template <typename T> inline bool linkCheck(const T &p, uint16_t magic) {
  return p.magic == magic && p.version == LINK_VERSION &&
         p.crc == linkCrc16((const uint8_t *)&p, sizeof(T) - sizeof(p.crc));
}
