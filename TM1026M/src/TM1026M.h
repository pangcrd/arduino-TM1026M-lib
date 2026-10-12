/*
 * TM1026M.h — Arduino driver for the BIOSEC TM1026M fingerprint module.
 *
 * Implements the SFM-V1.7 protocol (指纹模组通讯协议 SFM-V1.7):
 *   8-byte frames:  F5 TYPE P1 P2 P3 00 CHK F5,  CHK = XOR of bytes 2..6
 *   multi-packet:   8-byte header (with Len) + packet F5 <data> CHK F5
 * UART: 115200 bps, 8N1, 3.3V TTL.
 *
 * The driver auto-selects a UART port (and safe default pins where known)
 * based on the target MCU. You can always override the port/pins manually.
 */
#ifndef TM1026M_H
#define TM1026M_H

#include <Arduino.h>

/* ---------------- protocol constants ---------------- */
#define TM1026M_BAUD          115200UL
#define TM1026M_LIB_VERSION   "1.1.2"

#define TM1026M_FRAME_HEAD    0xF5
#define TM1026M_FRAME_TAIL    0xF5

/* 0x23 capture: Real Data = 3 info bytes + 8192 feature bytes (SFM-V1.7 §3.10) */
#define TM1026M_FEATURE_LEN       8192
#define TM1026M_FEATURE_RAWDATA   8195

/* ACK status codes (Q3 of the 8-byte answer frame) */
#define TM_ACK_SUCCESS        0x00
#define TM_ACK_FAIL           0x01
#define TM_ACK_FULL           0x04
#define TM_ACK_NOUSER         0x05
#define TM_ACK_USER_EXIST     0x07
#define TM_ACK_TIMEOUT        0x08
#define TM_ACK_NO_FINGER      0x09  // not in SFM-V1.7; tolerated (older firmwares)
#define TM_ACK_HARDWAREERROR  0x0A
#define TM_ACK_IMAGEERROR     0x10
#define TM_ACK_ALGORITHMFAIL  0x11  // film/spoof-attack detected
#define TM_ACK_HOMOLOGYFAIL   0x12  // homology check failed
#define TM_ACK_BREAK          0x18  // command was broken off

/* command codes (SFM-V1.7 §3) */
#define TM_CMD_ADD_STEP1      0x01  // enroll step 1 (3C3R) / each press (NCNR)
#define TM_CMD_ADD_STEP2      0x02  // enroll step 2 (3C3R)
#define TM_CMD_ADD_FINAL      0x03  // enroll final / "registration finished" marker
#define TM_CMD_DEL_USER       0x04  // P1/P2 = user id, P3 = role (0 = no role filter)
#define TM_CMD_DEL_ALL        0x05
#define TM_CMD_GET_COUNT      0x09
#define TM_CMD_GET_PERM       0x0A
#define TM_CMD_MATCH_1TO1     0x0B
#define TM_CMD_MATCH_1TON     0x0C  // P3 = role filter (0 = all roles)
#define TM_CMD_GET_FREE_ID    0x0D
#define TM_CMD_SET_BAUD       0x21  // P2 = baud id 1..5, P3 = 0 temp / 1 permanent
#define TM_CMD_CAP_FEATURE    0x23  // capture + upload feature (>8 bytes answer)
#define TM_CMD_CAP_IMAGE      0x24  // capture + upload image (>8 bytes answer)
#define TM_CMD_GET_VERSION    0x26  // (>8 bytes answer)
#define TM_CMD_SET_LEVEL      0x28  // match level 0..2 (SFM-V1.7!)
#define TM_CMD_LIST_USERS     0x2B  // (>8 bytes answer)
#define TM_CMD_DUP_MODE       0x2D  // allow/deny duplicate enroll
#define TM_CMD_FINGER_DETECT  0x30  // immediate finger-press check
#define TM_CMD_CONFIG_ENROLL  0x3F  // P1/P2 = PID, P3 = value
#define TM_CMD_GET_UID        0x60  // module unique ID (>8 bytes answer)
#define TM_CMD_SLEEP          0x2C
#define TM_CMD_LED            0xC3  // P1 = start color, P2 = end color, P3 = period
/* handshake codes (TM1026M datasheet appendix 1; 0xFE is also the break cmd §3.20) */
#define TM_CMD_PING           0xFE
#define TM_CMD_SYNC           0xFD

/* 0x3F enroll-config parameter IDs (SFM-V1.7 §3.18) */
#define TM_PID_ENROLL_MODE    0x0000  // value: 5 = 1CNR, 6 = NCNR
#define TM_PID_HOMOLOGY       0x0001  // value: 0..5 (homology check strictness)
#define TM_PID_LIFT_DETECT    0x0002  // value: 1 = enable lift detection
#define TM_PID_CAPTURE_COUNT  0x0003  // value: N captures (NCNR)
#define TM_PID_FILM_DETECT    0x0004  // value: 1 = enable film-attack detection
#define TM_PID_SELF_LEARN     0x0040  // send before match to enable self-learning

/* 0xC3 LED colors: low 3 bits = RGB, active low (SFM-V1.7 §3.19) */
#define TM_LED_RED    0x03
#define TM_LED_GREEN  0x05
#define TM_LED_BLUE   0x06
#define TM_LED_OFF    0x07
#define TM_LED_RG     0x01  // red+green mix
#define TM_LED_RB     0x02  // red+blue mix
#define TM_LED_GB     0x04  // green+blue mix

/* baud ids for 0x21 (SFM-V1.7 §3.9) */
#define TM_BAUD_9600    1
#define TM_BAUD_19200   2
#define TM_BAUD_38400   3
#define TM_BAUD_57600   4
#define TM_BAUD_115200  5

/* pin sentinels */
#define TM_PIN_AUTO           (-2)  // resolve from the per-MCU default below
#define TM_PIN_CORE_DEFAULT   (-1)  // leave to the Arduino core / board variant

/* ---------------- per-MCU default UART ----------------
 * Override any time:
 *   TM1026M fp(Serial1);            // pick the port
 *   fp.begin(Serial1, rxPin, txPin); // pick the port + pins (on remappable MCUs)
 */
#if defined(ARDUINO_ARCH_ESP32)
  #if defined(CONFIG_IDF_TARGET_ESP32)
    // Classic ESP32: Serial2 defaults to GPIO16/17 on most devkits — safe choice.
    #define TM1026M_AUTO_SERIAL Serial2
    #define TM1026M_AUTO_RX     16
    #define TM1026M_AUTO_TX     17
  #else
    // ESP32-S2/S3/C3/C6/H2/S31: Serial1, pins from the board variant.
    // Pass explicit pins to begin() if the variant default does not suit you
    // (on S31 avoid strapping pins 36/37/60/61).
    #define TM1026M_AUTO_SERIAL Serial1
    #define TM1026M_AUTO_RX     TM_PIN_CORE_DEFAULT
    #define TM1026M_AUTO_TX     TM_PIN_CORE_DEFAULT
  #endif
#elif defined(ESP8266)
  // ESP8266: only UART0 is full-duplex; pins are fixed.
  #define TM1026M_AUTO_SERIAL Serial
  #define TM1026M_AUTO_RX     TM_PIN_CORE_DEFAULT
  #define TM1026M_AUTO_TX     TM_PIN_CORE_DEFAULT
#elif defined(ARDUINO_ARCH_RP2040)
  #define TM1026M_AUTO_SERIAL Serial1
  #define TM1026M_AUTO_RX     TM_PIN_CORE_DEFAULT
  #define TM1026M_AUTO_TX     TM_PIN_CORE_DEFAULT
#elif defined(HAVE_HWSERIAL1)
  // AVR Mega/Leonardo, STM32, SAMD, nRF52, ... : Serial1 exists.
  #define TM1026M_AUTO_SERIAL Serial1
  #define TM1026M_AUTO_RX     TM_PIN_CORE_DEFAULT
  #define TM1026M_AUTO_TX     TM_PIN_CORE_DEFAULT
#else
  // Fallback (e.g. ATmega328P/Arduino Uno): the only hardware UART.
  // NOTE: Uno has no spare UART — use it only if USB debug is not needed.
  #define TM1026M_AUTO_SERIAL Serial
  #define TM1026M_AUTO_RX     TM_PIN_CORE_DEFAULT
  #define TM1026M_AUTO_TX     TM_PIN_CORE_DEFAULT
#endif

#define TM1026M_STR_(x) #x
#define TM1026M_STR(x)  TM1026M_STR_(x)

/* 8-byte answer frame payload */
struct TM1026M_Ack {
  uint8_t cmd = 0;
  uint8_t q1  = 0;
  uint8_t q2  = 0;
  uint8_t q3  = 0;   // status code, see TM_ACK_*
};

/* one enrolled user (0x2B list) */
struct TM1026M_User {
  uint16_t id   = 0;
  uint8_t  role = 0;
};

class TM1026M {
public:
  TM1026M();                              // auto port + auto pins for this MCU
  explicit TM1026M(HardwareSerial &serial);// fixed port, auto pins

  static HardwareSerial &defaultSerial(); // the per-MCU default port
  static const char *defaultPortName();   // e.g. "Serial1", for debug prints

  /* Optional hardware control (recommended by the datasheet):
   *  - power pin drives the module VCC switch (MOSFET / LDO EN).
   *    V_TOUCH (pin 1) must stay powered at 3.3V at all times.
   *  - wake pin reads TOUCH_OUT (pin 2): HIGH = finger present. */
  void setPowerPin(int8_t pin, bool activeHigh = true);
  void setWakePin(int8_t pin);

  /* Init: attach UART, power the module (if a power pin is set),
   * run the fast handshake (datasheet appendix 1). */
  bool begin(uint32_t baud = TM1026M_BAUD);
  bool begin(HardwareSerial &serial, uint32_t baud = TM1026M_BAUD);
  bool begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin,
             uint32_t baud = TM1026M_BAUD);

  /* Power flow (datasheet section 6):
   *   powerOn() -> commands... -> [abortCapture() if acquiring] -> powerOff()
   * powerOff() puts the MCU UART pins in high-Z input BEFORE cutting VCC
   * so the MCU never back-feeds the unpowered module (appendix 2a). */
  bool powerOn(uint32_t handshakeTimeoutMs = 3000);
  void powerOff();
  void abortCapture();   // §3.20 break: leaves acquisition state immediately

  bool handshake(uint32_t timeoutMs = 3000);  // appendix-1 fast init
  bool fingerTouched();                       // needs setWakePin()

  /* Raw 8-byte command. Returns false on timeout / checksum error. */
  bool command(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
               TM1026M_Ack &ack, uint32_t timeoutMs = 3000);

  /* ---- enroll (SFM-V1.7 §3.1) ----
   * 3C3R (default mode): 3 presses. userId 1..10000; userId = 0 lets the
   * module auto-assign an ID (returned through the reference).
   * Lift the finger between presses (film-attack detection may be on). */
  bool enroll(uint16_t &userId, uint8_t permission = 1,
              uint32_t stepTimeoutMs = 20000, uint8_t maxRetries = 40);
  /* NCNR: configure via configEnroll(TM_PID_ENROLL_MODE, 6) +
   * configEnroll(TM_PID_CAPTURE_COUNT, N) first, then call this. */
  bool enrollNCNR(uint16_t &userId, uint8_t permission, uint8_t presses,
                  uint32_t stepTimeoutMs = 20000, uint8_t maxRetries = 40);

  /* ---- match ---- */
  // 1:N (§3.7). role = 0 → all roles. Success ⟺ returned id != 0.
  // NOTE: with self-learning enabled, wait 300 ms after a match before powerOff!
  bool match1N(uint16_t &userId, uint8_t &permission, uint8_t role = 0,
               uint32_t timeoutMs = 30000);
  bool match1to1(uint16_t userId, uint32_t timeoutMs = 30000);
  bool detectFinger(uint32_t timeoutMs = 1500);  // §3.16 immediate check

  /* ---- user management ---- */
  // role = 0 → delete by id only; userId = 0 + role != 0 → delete all of that role.
  bool deleteUser(uint16_t userId, uint8_t role = 0);
  bool deleteAllUsers();
  int16_t userCount();                        // <0 on communication error
  int16_t userPermission(uint16_t userId);     // <0 on error / unknown user
  int32_t firstFreeId();                      // §3.8, <0 on error
  // Returns number of users stored (capped at maxUsers); 0 = empty DB; <0 on error.
  int16_t listUsers(TM1026M_User *users, uint16_t maxUsers);

  /* ---- module setup ---- */
  bool sleep();
  bool setMatchLevel(uint8_t level);          // SFM-V1.7: 0..2, default 2
  int16_t getMatchLevel();                    // <0 on error
  bool setDuplicateMode(bool allow);          // default after power-up: allow
  int16_t getDuplicateMode();                 // 0 = allow, 1 = deny, <0 on error
  bool setBaudRate(uint8_t baudId, bool permanent = false);  // §3.9, re-opens UART
  bool setLed(uint8_t startColor, uint8_t endColor, uint8_t period);
  // period: 30..200 (×10 ms). TM_LED_RED/GREEN/BLUE/OFF/...; 0x07,0x07,96 = off.
  bool configEnroll(uint16_t pid, uint8_t value);  // §3.18, see TM_PID_*
  bool getVersion(char *buf, size_t bufLen);       // NUL-terminated string
  bool getUniqueId(uint8_t uid[8]);                 // §3.17, 8 raw bytes

  /* ---- capture (multi-packet) ----
   * featureOut must hold TM1026M_FEATURE_LEN (8192) bytes. Needs ~8 KB RAM!
   * Image: buf must hold width*height bytes (e.g. 176×176 = 30976). */
  bool captureFeature(uint8_t *featureOut, uint32_t timeoutMs = 20000);
  bool captureImage(uint8_t *buf, size_t bufCap, uint16_t &width,
                    uint16_t &height, uint32_t timeoutMs = 20000);

  uint8_t lastStatus() const { return _lastAck.q3; }
  static const char *statusName(uint8_t s);

private:
  HardwareSerial *_serial = nullptr;
  int8_t  _rxPin = TM_PIN_AUTO;
  int8_t  _txPin = TM_PIN_AUTO;
  int8_t  _powerPin = -1;
  bool    _powerActiveHigh = true;
  int8_t  _wakePin = -1;
  uint32_t _baud = TM1026M_BAUD;
  TM1026M_Ack _lastAck;

  void beginSerial();
  void flushRx();
  void quietDrain(uint32_t quietMs);
  void sendFrame(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3);
  bool recvFrame(uint32_t timeoutMs, TM1026M_Ack &ack, uint8_t expectCmd);
  bool readBytes(uint8_t *dst, size_t n, uint32_t timeoutMs);
  // Enroll step: like command(), but also accepts the 0x03 "finished" ACK.
  bool enrollSend(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                  TM1026M_Ack &ack, uint32_t timeoutMs);
};

#endif // TM1026M_H
