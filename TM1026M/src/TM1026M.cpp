/*
 * TM1026M.cpp — Arduino driver for the BIOSEC TM1026M fingerprint module.
 * Implements the SFM-V1.7 protocol (指纹模组通讯协议 SFM-V1.7).
 *
 * Frame format (8 bytes):  F5 TYPE P1 P2 P3 00 CHK F5
 *   CHK = TYPE ^ P1 ^ P2 ^ P3 ^ 0x00   (XOR of bytes 2..6)
 *
 * Wiring (module pin -> MCU):
 *   1 V_TOUCH   -> 3.3V, ALWAYS powered
 *   2 TOUCH_OUT -> GPIO input (HIGH = finger present / acquiring)
 *   3 VCC       -> 3.3V via MOSFET / LDO-EN switch (dedicated LDO recommended)
 *   4 TX        -> MCU RX
 *   5 RX        -> MCU TX
 *   6 GND       -> GND
 */
#include "TM1026M.h"

static const uint32_t TM_BAUD_TABLE[6] =
  { 0, 9600, 19200, 38400, 57600, 115200 };

TM1026M::TM1026M() : _serial(nullptr) {}

TM1026M::TM1026M(HardwareSerial &serial) : _serial(&serial) {}

HardwareSerial &TM1026M::defaultSerial() { return TM1026M_AUTO_SERIAL; }

const char *TM1026M::defaultPortName() { return TM1026M_STR(TM1026M_AUTO_SERIAL); }

void TM1026M::setPowerPin(int8_t pin, bool activeHigh) {
  _powerPin = pin;
  _powerActiveHigh = activeHigh;
}

void TM1026M::setWakePin(int8_t pin) {
  _wakePin = pin;
  if (pin >= 0) pinMode(pin, INPUT);  // TOUCH_OUT is driven by the module
}

/* ---------------- init ---------------- */

bool TM1026M::begin(uint32_t baud) {
  _baud = baud;
  if (!_serial) _serial = &defaultSerial();
  return powerOn();
}

bool TM1026M::begin(HardwareSerial &serial, uint32_t baud) {
  _serial = &serial;
  _baud = baud;
  _rxPin = TM_PIN_AUTO;
  _txPin = TM_PIN_AUTO;
  return powerOn();
}

bool TM1026M::begin(HardwareSerial &serial, int8_t rxPin, int8_t txPin,
                    uint32_t baud) {
  _serial = &serial;
  _baud = baud;
  _rxPin = rxPin;
  _txPin = txPin;
  return powerOn();
}

void TM1026M::beginSerial() {
  int8_t rx = (_rxPin == TM_PIN_AUTO) ? TM1026M_AUTO_RX : _rxPin;
  int8_t tx = (_txPin == TM_PIN_AUTO) ? TM1026M_AUTO_TX : _txPin;
  _rxPin = rx;  // remember the resolved pins so powerOff() can use them
  _txPin = tx;
#if defined(ARDUINO_ARCH_ESP32)
  _serial->begin(_baud, SERIAL_8N1, rx, tx);
#elif defined(ARDUINO_ARCH_RP2040)
  _serial->begin(_baud, SERIAL_8N1, rx, tx);
#elif defined(ARDUINO_ARCH_STM32)
  if (rx >= 0) _serial->setRx(rx);
  if (tx >= 0) _serial->setTx(tx);
  _serial->begin(_baud, SERIAL_8N1);
#else
  (void)rx;
  (void)tx;
  _serial->begin(_baud, SERIAL_8N1);  // AVR, ESP8266, ... : fixed pins
#endif
}

/* ---------------- power flow (datasheet section 6) ---------------- */

bool TM1026M::powerOn(uint32_t handshakeTimeoutMs) {
  if (!_serial) _serial = &defaultSerial();
  if (_powerPin >= 0) {
    pinMode(_powerPin, OUTPUT);
    digitalWrite(_powerPin, _powerActiveHigh ? HIGH : LOW);
    delay(150);  // datasheet: wait for the module to finish its init
  }
  beginSerial();
  return handshake(handshakeTimeoutMs);
}

void TM1026M::powerOff() {
  if (_powerPin < 0 || !_serial) return;
  _serial->end();  // release the UART first ...
  // ... then park the pins as high-Z inputs so the MCU cannot back-feed
  // the unpowered module (datasheet appendix 2a).
  if (_rxPin >= 0) pinMode(_rxPin, INPUT);
  if (_txPin >= 0) pinMode(_txPin, INPUT);
  digitalWrite(_powerPin, _powerActiveHigh ? LOW : HIGH);
}

/* SFM-V1.7 §3.20: 0xFE terminates acquisition immediately.
 * Idle module answers FAIL; a capturing module answers with ACK_BREAK. */
void TM1026M::abortCapture() {
  if (!_serial) return;
  flushRx();
  sendFrame(TM_CMD_PING, 0, 0, 0);
  TM1026M_Ack ack;
  recvFrame(500, ack, 0);
  flushRx();
}

/* ---------------- handshake (datasheet appendix 1) ----------------
 * The module answers unimplemented commands with a TA_FAIL frame, so:
 *  1. spam 0xFE every 10 ms until ANY valid frame comes back (= module alive)
 *  2. flush the line, send a single 0xFD
 *  3. the 0xFD answer proves the module is fully in command mode.        */
bool TM1026M::handshake(uint32_t timeoutMs) {
  if (!_serial) return false;
  TM1026M_Ack ack;
  uint32_t start = millis();

  bool alive = false;
  while ((uint32_t)(millis() - start) < timeoutMs) {
    sendFrame(TM_CMD_PING, 0, 0, 0);
    if (recvFrame(10, ack, 0)) { alive = true; break; }
  }
  if (!alive) return false;

  quietDrain(20);                    // drop the queued 0xFE answers
  sendFrame(TM_CMD_SYNC, 0, 0, 0);    // one single 0xFD

  start = millis();
  while ((uint32_t)(millis() - start) < 1000) {
    if (recvFrame(50, ack, 0)) {
      if (ack.cmd == TM_CMD_SYNC) return true;  // 0xFD answered -> ready
      // stale 0xFE answers are ignored
    }
  }
  return false;
}

bool TM1026M::fingerTouched() {
  return _wakePin >= 0 && digitalRead(_wakePin) == HIGH;
}

/* ---------------- transport ---------------- */

void TM1026M::flushRx() {
  if (!_serial) return;
  while (_serial->available()) _serial->read();
}

void TM1026M::quietDrain(uint32_t quietMs) {
  if (!_serial) return;
  uint32_t last = millis();
  while ((uint32_t)(millis() - last) < quietMs) {
    if (_serial->available()) {
      _serial->read();
      last = millis();
    } else {
      yield();
    }
  }
}

void TM1026M::sendFrame(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3) {
  uint8_t f[8];
  f[0] = TM1026M_FRAME_HEAD;
  f[1] = cmd;
  f[2] = p1;
  f[3] = p2;
  f[4] = p3;
  f[5] = 0x00;
  f[6] = (uint8_t)(cmd ^ p1 ^ p2 ^ p3 ^ 0x00);
  f[7] = TM1026M_FRAME_TAIL;
  _serial->write(f, 8);
  _serial->flush();
}

bool TM1026M::recvFrame(uint32_t timeoutMs, TM1026M_Ack &ack, uint8_t expectCmd) {
  if (!_serial) return false;
  uint32_t start = millis();
  uint8_t buf[7];
  uint8_t idx = 0;
  bool sync = false;
  while ((uint32_t)(millis() - start) < timeoutMs) {
    if (_serial->available()) {
      uint8_t b = (uint8_t)_serial->read();
      if (!sync) {
        if (b == TM1026M_FRAME_HEAD) { sync = true; idx = 0; }
        continue;
      }
      buf[idx++] = b;
      if (idx == 7) {
        // buf = TYPE Q1 Q2 Q3 00 CHK TAIL
        sync = false;
        idx = 0;
        bool okTail = (buf[6] == TM1026M_FRAME_TAIL);
        bool okChk = ((uint8_t)(buf[0] ^ buf[1] ^ buf[2] ^ buf[3] ^ buf[4]) == buf[5]);
        if (okTail && okChk && (expectCmd == 0 || buf[0] == expectCmd)) {
          ack.cmd = buf[0];
          ack.q1  = buf[1];
          ack.q2  = buf[2];
          ack.q3  = buf[3];
          _lastAck = ack;
          return true;
        }
        // wrong type or bad frame: keep scanning for the next 0xF5
      }
    } else {
      yield();
    }
  }
  return false;
}

bool TM1026M::readBytes(uint8_t *dst, size_t n, uint32_t timeoutMs) {
  if (!_serial) return false;
  uint32_t start = millis();
  size_t got = 0;
  while (got < n && (uint32_t)(millis() - start) < timeoutMs) {
    if (_serial->available()) {
      dst[got++] = (uint8_t)_serial->read();
    } else {
      yield();
    }
  }
  return got == n;
}

bool TM1026M::command(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                      TM1026M_Ack &ack, uint32_t timeoutMs) {
  if (!_serial) return false;
  flushRx();
  sendFrame(cmd, p1, p2, p3);
  return recvFrame(timeoutMs, ack, cmd);
}

/* ---------------- enroll (SFM-V1.7 §3.1, 3C3R) ----------------
 * Step 1 carries ID+role; steps 2-3 carry zeros. Any ACK whose TYPE is
 * 0x03 means registration has finished — Q3==0 → success, Q1/Q2 = user ID. */
bool TM1026M::enrollSend(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3,
                         TM1026M_Ack &ack, uint32_t timeoutMs) {
  if (!_serial) return false;
  _lastAck.q3 = 0xFF;  // mark "no response yet" so lastStatus() isn't stale
  flushRx();
  sendFrame(cmd, p1, p2, p3);
  if (!recvFrame(timeoutMs, ack, 0)) return false;  // timeout / bad checksum
  return ack.cmd == cmd || ack.cmd == TM_CMD_ADD_FINAL;
}

/* Wait for the finger to lift between enroll presses. TOUCH_OUT is
 * active-HIGH (HIGH = finger present, per BIOSEC-TM1026M datasheet). */
void TM1026M::waitLift(uint32_t timeoutMs) {
  if (_wakePin < 0) {
    delay(1200);  // no wake pin: fixed wait so the user can lift in time
    return;
  }
  uint32_t t = millis();
  while (digitalRead(_wakePin) == HIGH && (uint32_t)(millis() - t) < timeoutMs)
    delay(10);
  delay(300);  // extra settle time after the lift is detected
}

bool TM1026M::enroll(uint16_t &userId, uint8_t permission,
                     uint32_t stepTimeoutMs, uint8_t maxRetries) {
  const uint8_t steps[3] = { TM_CMD_ADD_STEP1, TM_CMD_ADD_STEP2, TM_CMD_ADD_FINAL };
  uint8_t p1 = (uint8_t)((userId >> 8) & 0xFF);
  uint8_t p2 = (uint8_t)(userId & 0xFF);

  for (uint8_t s = 0; s < 3; s++) {
    uint8_t tries = 0, badTries = 0;
    for (;;) {
      TM1026M_Ack ack;
      bool first = (s == 0);
      bool ok = enrollSend(steps[s], first ? p1 : 0, first ? p2 : 0,
                           first ? permission : 0, ack, stepTimeoutMs);
      if (!ok) return false;
#ifdef TM_DEBUG
      Serial.printf("[enroll] step %u ok=%d cmd=%02X q3=%02X (%s)\n",
                    s, ok, ack.cmd, ack.q3, statusName(ack.q3));
#endif
      if (ack.cmd == TM_CMD_ADD_FINAL) {  // registration finished
        if (ack.q3 == TM_ACK_SUCCESS) {
          userId = ((uint16_t)ack.q1 << 8) | ack.q2;  // auto-assigned ID, if any
          return true;
        }
        return false;  // e.g. USER_EXIST when duplicates are denied
      }
      if (ack.q3 == TM_ACK_SUCCESS) { waitLift(); break; }  // next press
      // 0x08 = 8 s without a finger press: ask the user to press again.
      if ((ack.q3 == TM_ACK_TIMEOUT || ack.q3 == TM_ACK_NO_FINGER) &&
          ++tries <= maxRetries)
        continue;
      // Bad capture on this step: let the user press again for the same step.
      if ((ack.q3 == TM_ACK_HOMOLOGYFAIL || ack.q3 == TM_ACK_IMAGEERROR) &&
          ++badTries <= 5) {
        waitLift();
        continue;
      }
      return false;  // FULL / FAIL / ...
    }
  }
  return false;  // unreachable: 0x03 always terminates the flow
}

/* NCNR: configure with configEnroll(TM_PID_ENROLL_MODE, 6) and
 * configEnroll(TM_PID_CAPTURE_COUNT, N) first. */
bool TM1026M::enrollNCNR(uint16_t &userId, uint8_t permission, uint8_t presses,
                         uint32_t stepTimeoutMs, uint8_t maxRetries) {
  if (presses == 0) presses = 1;
  uint8_t p1 = (uint8_t)((userId >> 8) & 0xFF);
  uint8_t p2 = (uint8_t)(userId & 0xFF);

  for (uint8_t i = 0; i < presses; i++) {
    uint8_t tries = 0;
    for (;;) {
      TM1026M_Ack ack;
      bool first = (i == 0);
      if (!enrollSend(TM_CMD_ADD_STEP1, first ? p1 : 0, first ? p2 : 0,
                      first ? permission : 0, ack, stepTimeoutMs))
        return false;
      if (ack.cmd == TM_CMD_ADD_FINAL) {
        if (ack.q3 == TM_ACK_SUCCESS) {
          userId = ((uint16_t)ack.q1 << 8) | ack.q2;
          return true;
        }
        return false;
      }
      if (ack.q3 == TM_ACK_SUCCESS) {
        if (i + 1 < presses) waitLift();  // let the user lift before next press
        break;
      }
      if ((ack.q3 == TM_ACK_TIMEOUT || ack.q3 == TM_ACK_NO_FINGER) &&
          ++tries <= maxRetries)
        continue;
      return false;
    }
  }
  return false;
}

/* ---------------- match ---------------- */

/* SFM-V1.7 §3.7: success ⟺ returned ID != 0. On failure the module returns
 * ID = 0 (Q3 = 0x00) or the timeout frame (Q3 = 0x08). */
bool TM1026M::match1N(uint16_t &userId, uint8_t &permission, uint8_t role,
                      uint32_t timeoutMs) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_MATCH_1TON, 0, 0, role, ack, timeoutMs)) return false;
  uint16_t id = ((uint16_t)ack.q1 << 8) | ack.q2;
  if (id == 0) return false;
  userId = id;
  permission = ack.q3;  // role 1/2/3 on success
  return true;
}

bool TM1026M::match1to1(uint16_t userId, uint32_t timeoutMs) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_MATCH_1TO1, (uint8_t)((userId >> 8) & 0xFF),
               (uint8_t)(userId & 0xFF), 0, ack, timeoutMs)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

bool TM1026M::detectFinger(uint32_t timeoutMs) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_FINGER_DETECT, 0, 0, 0, ack, timeoutMs)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

/* ---------------- user management ---------------- */

bool TM1026M::deleteUser(uint16_t userId, uint8_t role) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_DEL_USER, (uint8_t)((userId >> 8) & 0xFF),
               (uint8_t)(userId & 0xFF), role, ack)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

bool TM1026M::deleteAllUsers() {
  TM1026M_Ack ack;
  if (!command(TM_CMD_DEL_ALL, 0, 0, 0, ack)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

int16_t TM1026M::userCount() {
  TM1026M_Ack ack;
  if (!command(TM_CMD_GET_COUNT, 0, 0, 0, ack)) return -1;
  if (ack.q3 != TM_ACK_SUCCESS) return -1;
  return (int16_t)(((uint16_t)ack.q1 << 8) | ack.q2);
}

int16_t TM1026M::userPermission(uint16_t userId) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_GET_PERM, (uint8_t)((userId >> 8) & 0xFF),
               (uint8_t)(userId & 0xFF), 0, ack)) return -1;
  if (ack.q3 == TM_ACK_NOUSER) return -1;
  return (int16_t)ack.q3;
}

int32_t TM1026M::firstFreeId() {
  TM1026M_Ack ack;
  if (!command(TM_CMD_GET_FREE_ID, 0, 0, 0, ack)) return -1;
  if (ack.q3 != TM_ACK_SUCCESS) return -1;
  return (int32_t)(((uint16_t)ack.q1 << 8) | ack.q2);
}

/* SFM-V1.7 §3.14: streams the user list (no big buffer needed).
 * Empty DB → module answers FAIL with no packet → we return 0. */
int16_t TM1026M::listUsers(TM1026M_User *users, uint16_t maxUsers) {
  if (!users || maxUsers == 0 || !_serial) return -1;
  flushRx();
  sendFrame(TM_CMD_LIST_USERS, 0, 0, 0);
  TM1026M_Ack h;
  if (!recvFrame(5000, h, TM_CMD_LIST_USERS)) return -1;
  if (h.q3 == TM_ACK_FAIL) return 0;  // empty database
  if (h.q3 != TM_ACK_SUCCESS) return -1;

  /* SFM-V1.7 §3.14: data package bytes 2-3 are the payload size = 3 * userCount
   * (per the doc example: 1 user -> 0x0003), NOT the user count. */
  uint8_t head;
  if (!readBytes(&head, 1, 2000) || head != TM1026M_FRAME_HEAD) return -1;
  uint8_t cnt[2];
  if (!readBytes(cnt, 2, 3000)) return -1;
  uint16_t total = ((uint16_t)cnt[0] << 8) | cnt[1];
  uint16_t userCount = total / 3;
  uint8_t chk = (uint8_t)(cnt[0] ^ cnt[1]);

  uint16_t stored = 0;
  uint8_t u[3];
  for (uint16_t i = 0; i < userCount; i++) {
    if (!readBytes(u, 3, 3000)) return -1;
    chk ^= (uint8_t)(u[0] ^ u[1] ^ u[2]);
    if (stored < maxUsers) {
      users[stored].id   = ((uint16_t)u[0] << 8) | u[1];
      users[stored].role = u[2];
      stored++;
    }
  }
  uint8_t tailChk, tail;
  if (!readBytes(&tailChk, 1, 2000)) return -1;
  if (!readBytes(&tail, 1, 2000) || tail != TM1026M_FRAME_TAIL) return -1;
  if (chk != tailChk) return -1;
  return (int16_t)stored;
}

/* ---------------- module setup ---------------- */

bool TM1026M::sleep() {
  TM1026M_Ack ack;
  // The sleep answer carries no status; any valid frame means it worked.
  return command(TM_CMD_SLEEP, 0, 0, 0, ack);
}

/* SFM-V1.7 §3.13: level 0..2 (default 2). Higher = stricter.
 * FAR: 0 → 1/100k, 1 → 1/500k, 2 → 1/1M. Resets to 2 on reboot. */
bool TM1026M::setMatchLevel(uint8_t level) {
  if (level > 2) level = 2;
  TM1026M_Ack ack;
  if (!command(TM_CMD_SET_LEVEL, 0, level, 0, ack)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

int16_t TM1026M::getMatchLevel() {
  TM1026M_Ack ack;
  if (!command(TM_CMD_SET_LEVEL, 0, 0, 1, ack)) return -1;
  if (ack.q3 != TM_ACK_SUCCESS) return -1;
  return (int16_t)ack.q2;
}

/* SFM-V1.7 §3.15: module powers up in "allow duplicates" mode. */
bool TM1026M::setDuplicateMode(bool allow) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_DUP_MODE, 0, allow ? 0 : 1, 0, ack)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

int16_t TM1026M::getDuplicateMode() {
  TM1026M_Ack ack;
  if (!command(TM_CMD_DUP_MODE, 0, 0, 1, ack)) return -1;
  if (ack.q3 != TM_ACK_SUCCESS) return -1;
  return (int16_t)ack.q2;  // 0 = allow duplicates, 1 = deny
}

/* SFM-V1.7 §3.9: baud id 1..5 → 9600..115200. Re-opens our UART to match.
 * Temporary (default) resets to 115200 on module reboot; permanent is stored. */
bool TM1026M::setBaudRate(uint8_t baudId, bool permanent) {
  if (baudId < 1 || baudId > 5 || !_serial) return false;
  TM1026M_Ack ack;
  if (!command(TM_CMD_SET_BAUD, 0, baudId, permanent ? 1 : 0, ack)) return false;
  if (ack.q3 != TM_ACK_SUCCESS || ack.q2 != baudId) return false;
  _baud = TM_BAUD_TABLE[baudId];
  beginSerial();
  delay(50);
  return true;
}

/* SFM-V1.7 §3.19: breathing/fading LED. period 30..200 (×10 ms). */
bool TM1026M::setLed(uint8_t startColor, uint8_t endColor, uint8_t period) {
  TM1026M_Ack ack;
  // The LED answer carries no status; any valid frame means it was accepted.
  return command(TM_CMD_LED, startColor, endColor, period, ack);
}

/* SFM-V1.7 §3.18: PID/parameter configuration for the enroll flow. */
bool TM1026M::configEnroll(uint16_t pid, uint8_t value) {
  TM1026M_Ack ack;
  if (!command(TM_CMD_CONFIG_ENROLL, (uint8_t)((pid >> 8) & 0xFF),
               (uint8_t)(pid & 0xFF), value, ack)) return false;
  return ack.q3 == TM_ACK_SUCCESS;
}

/* SFM-V1.7 §3.12: version string, streamed (no big buffer needed). */
bool TM1026M::getVersion(char *buf, size_t bufLen) {
  if (!buf || bufLen == 0 || !_serial) return false;
  flushRx();
  sendFrame(TM_CMD_GET_VERSION, 0, 0, 0);
  TM1026M_Ack h;
  if (!recvFrame(5000, h, TM_CMD_GET_VERSION)) return false;
  if (h.q3 != TM_ACK_SUCCESS) return false;
  uint16_t len = ((uint16_t)h.q1 << 8) | h.q2;

  uint8_t head;
  if (!readBytes(&head, 1, 2000) || head != TM1026M_FRAME_HEAD) return false;
  size_t copy = (len < bufLen - 1) ? len : bufLen - 1;
  uint8_t chk = 0, b;
  for (uint16_t i = 0; i < len; i++) {
    if (!readBytes(&b, 1, 3000)) return false;
    chk ^= b;
    if (i < copy) buf[i] = (char)b;
  }
  buf[copy] = '\0';
  uint8_t tailChk, tail;
  if (!readBytes(&tailChk, 1, 2000)) return false;
  if (!readBytes(&tail, 1, 2000) || tail != TM1026M_FRAME_TAIL) return false;
  return chk == tailChk;
}

/* SFM-V1.7 §3.17: header carries no Len; packet is F5 + 8 ID bytes + CHK + F5. */
bool TM1026M::getUniqueId(uint8_t uid[8]) {
  if (!uid || !_serial) return false;
  flushRx();
  sendFrame(TM_CMD_GET_UID, 0, 0, 0);
  TM1026M_Ack h;
  if (!recvFrame(3000, h, TM_CMD_GET_UID)) return false;
  uint8_t head;
  if (!readBytes(&head, 1, 2000) || head != TM1026M_FRAME_HEAD) return false;
  if (!readBytes(uid, 8, 2000)) return false;
  uint8_t chk, tail;
  if (!readBytes(&chk, 1, 2000)) return false;
  if (!readBytes(&tail, 1, 2000) || tail != TM1026M_FRAME_TAIL) return false;
  uint8_t c = 0;
  for (uint8_t i = 0; i < 8; i++) c ^= uid[i];
  return c == chk;
}

/* ---------------- capture ---------------- */

/* SFM-V1.7 §3.10: header Len = 8195 (3 info bytes + 8192 feature bytes).
 * Also supports break: re-send 0x23 while waiting → module answers BREAK. */
bool TM1026M::captureFeature(uint8_t *featureOut, uint32_t timeoutMs) {
  if (!featureOut || !_serial) return false;
  flushRx();
  sendFrame(TM_CMD_CAP_FEATURE, 0, 0, 0);
  TM1026M_Ack h;
  if (!recvFrame(timeoutMs, h, TM_CMD_CAP_FEATURE)) return false;
  if (h.q3 != TM_ACK_SUCCESS) return false;
  uint16_t len = ((uint16_t)h.q1 << 8) | h.q2;
  if (len != TM1026M_FEATURE_RAWDATA) return false;

  uint8_t head, info[3];
  if (!readBytes(&head, 1, 2000) || head != TM1026M_FRAME_HEAD) return false;
  if (!readBytes(info, 3, 2000)) return false;
  if (!readBytes(featureOut, TM1026M_FEATURE_LEN, 8000)) return false;
  uint8_t chk, tail;
  if (!readBytes(&chk, 1, 2000)) return false;
  if (!readBytes(&tail, 1, 2000) || tail != TM1026M_FRAME_TAIL) return false;

  uint8_t c = (uint8_t)(info[0] ^ info[1] ^ info[2]);
  for (uint32_t i = 0; i < TM1026M_FEATURE_LEN; i++) c ^= featureOut[i];
  return c == chk;
}

/* SFM-V1.7 §3.11: header Q1/Q2 = width>>2 / height>>2 (e.g. 176×176 sensor
 * answers 0x2C/0x2C). Streams the bitmap straight into the caller's buffer. */
bool TM1026M::captureImage(uint8_t *buf, size_t bufCap, uint16_t &width,
                           uint16_t &height, uint32_t timeoutMs) {
  if (!buf || !_serial) return false;
  flushRx();
  sendFrame(TM_CMD_CAP_IMAGE, 0, 0, 0);
  TM1026M_Ack h;
  if (!recvFrame(timeoutMs, h, TM_CMD_CAP_IMAGE)) return false;
  if (h.q3 != TM_ACK_SUCCESS) return false;
  width  = (uint16_t)h.q1 << 2;
  height = (uint16_t)h.q2 << 2;
  uint32_t imgLen = (uint32_t)width * height;
  if (imgLen == 0 || imgLen > bufCap) return false;

  uint8_t head;
  if (!readBytes(&head, 1, 2000) || head != TM1026M_FRAME_HEAD) return false;
  uint8_t chk = 0, b;
  for (uint32_t i = 0; i < imgLen; i++) {
    if (!readBytes(&b, 1, 5000)) return false;
    chk ^= b;
    buf[i] = b;
  }
  uint8_t tailChk, tail;
  if (!readBytes(&tailChk, 1, 2000)) return false;
  if (!readBytes(&tail, 1, 2000) || tail != TM1026M_FRAME_TAIL) return false;
  return chk == tailChk;
}

const char *TM1026M::statusName(uint8_t s) {
  switch (s) {
    case TM_ACK_SUCCESS:       return "SUCCESS";
    case TM_ACK_FAIL:          return "FAIL";
    case TM_ACK_FULL:          return "DB_FULL";
    case TM_ACK_NOUSER:        return "NO_USER";
    case TM_ACK_USER_EXIST:    return "USER_EXIST";
    case TM_ACK_TIMEOUT:       return "TIMEOUT";
    case TM_ACK_HARDWAREERROR: return "HW_ERROR";
    case TM_ACK_IMAGEERROR:    return "IMAGE_ERROR";
    case TM_ACK_ALGORITHMFAIL: return "SPOOF_DETECTED";
    case TM_ACK_HOMOLOGYFAIL:  return "HOMOLOGY_FAIL";
    case TM_ACK_BREAK:         return "BREAK";
    default:                   return "UNKNOWN";
  }
}
