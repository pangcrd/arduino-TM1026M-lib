/*
 * EnrollMatch — enroll a fingerprint and match it, with explicit UART choice.
 *
 * Serial monitor commands (115200):
 *   e <id>   enroll finger as user <id> (e.g. "e 7"; "e 0" = auto-assign ID)
 *   m [role] 1:N match, optional role filter (e.g. "m" or "m 2")
 *   c        show enrolled user count
 *   d <id>   delete user <id>
 *   D        delete ALL users (careful!)
 *   f        first unused user ID
 *   v        module version string
 *   l        list enrolled users
 *
 * This example shows the manual port/pin selection:
 *   fp.begin(Serial1, RX_PIN, TX_PIN);
 * On ESP32-S31 pick GPIOs that are NOT strapping pins (avoid 36/37/60/61).
 */
#include <TM1026M.h>

#define PIN_FP_POWER  5
#define PIN_FP_WAKE   4

// Pick any free UART pins for your board:
#define FP_RX_PIN     16
#define FP_TX_PIN     17

TM1026M fp(Serial1);  // <-- explicit port; pins set in begin() below

void doEnroll(long arg) {
  if (arg < 0 || arg > 10000) { Serial.println("id must be 0..10000 (0 = auto)"); return; }
  uint16_t uid = (uint16_t)arg;
  Serial.print("Enrolling");
  if (uid == 0) Serial.print(" (auto ID)");
  Serial.println(" — press the SAME finger 3 times, lifting between presses.");
  if (fp.enroll(uid, 1)) {
    Serial.print("Enroll OK, user id = ");
    Serial.println(uid);
  } else {
    Serial.print("Enroll FAILED (");
    Serial.print(TM1026M::statusName(fp.lastStatus()));
    Serial.println(")");
  }
}

void doMatch(long arg) {
  Serial.println("Press a finger...");
  uint16_t uid;
  uint8_t perm;
  if (fp.match1N(uid, perm, (uint8_t)arg)) {
    Serial.print("Match! user=");
    Serial.print(uid);
    Serial.print(" perm=");
    Serial.println(perm);
  } else {
    Serial.print("No match (");
    Serial.print(TM1026M::statusName(fp.lastStatus()));
    Serial.println(")");
  }
}

void doDelete(long arg) {
  Serial.println(fp.deleteUser((uint16_t)arg) ? "Deleted" : "Delete failed");
}

void doList() {
  TM1026M_User users[20];
  int16_t n = fp.listUsers(users, 20);
  Serial.print("Users: ");
  Serial.println(n);
  for (int16_t i = 0; i < n; i++) {
    Serial.print("  id=");
    Serial.print(users[i].id);
    Serial.print(" role=");
    Serial.println(users[i].role);
  }
}

void doVersion() {
  char ver[48];
  Serial.println(fp.getVersion(ver, sizeof(ver)) ? ver : "read failed");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.print("TM1026M driver v");
  Serial.println(TM1026M_LIB_VERSION);  // must show 1.1.0 — if 1.0.0, update the library!

  fp.setPowerPin(PIN_FP_POWER);
  fp.setWakePin(PIN_FP_WAKE);

  if (!fp.begin(Serial1, FP_RX_PIN, FP_TX_PIN)) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("TM1026M ready. Commands: e <id> | m [role] | c | d <id> | D | f | v | l");

  doVersion();
}

void loop() {
  if (!Serial.available()) { delay(20); return; }
  char cmd = Serial.read();
  while (Serial.available() && Serial.peek() <= ' ') Serial.read();
  long arg = Serial.parseInt();
  while (Serial.available()) Serial.read();  // eat end of line

  switch (cmd) {
    case 'e': doEnroll(arg); break;
    case 'm': doMatch(arg); break;
    case 'c':
      Serial.print("Enrolled users: ");
      Serial.println(fp.userCount());
      break;
    case 'd': doDelete(arg); break;
    case 'D':
      Serial.println(fp.deleteAllUsers() ? "All users deleted" : "Delete failed");
      break;
    case 'f':
      Serial.print("First free ID: ");
      Serial.println(fp.firstFreeId());
      break;
    case 'v': doVersion(); break;
    case 'l': doList(); break;
    default:
      Serial.println("Unknown command. Use: e <id> | m [role] | c | d <id> | D | f | v | l");
      break;
  }
}
