/*
 * EnrollMatch — enroll a fingerprint and match it, with explicit UART choice.
 *
 * Serial monitor commands (115200). Type '?' to list them at any time:
 *   e <id>   enroll finger as user <id> (e.g. "e 7"; "e 0" = auto-assign ID)
 *   m [role] 1:N match, optional role filter (e.g. "m" or "m 2")
 *   c        show enrolled user count
 *   d <id>   delete user <id>
 *   D        delete ALL users (careful!)
 *   f        first unused user ID
 *   v        module version string
 *   l        list enrolled users
 *   h <0-5>  homology check level (0=off, 5=strictest; default varies)
 *   ?        show this help
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

void printHelp() {
  Serial.println("Commands (type '?' any time to see this list):");
  Serial.println("  e <id>   Dang ky van tay cho user <id> (vd \"e 7\"; \"e 0\" = module tu cap ID)");
  Serial.println("           Cham CUNG 1 ngon 3 lan, NHAC HAN ngon ra giua cac lan cham.");
  Serial.println("  m [role] So khop 1:N (vd \"m\" hoac \"m 2\" de loc theo role)");
  Serial.println("  c        Dem so user da dang ky");
  Serial.println("  d <id>   Xoa user <id>");
  Serial.println("  D        XOA TAT CA users (can than!)");
  Serial.println("  f        ID trong dau tien con dung duoc");
  Serial.println("  v        Doc chuoi version cua module");
  Serial.println("  l        Liet ke cac user da dang ky");
  Serial.println("  h <0-5>  Muc kiem tra homology khi enroll (0=tat, 5=gat nhat)");
  Serial.println("  ?        Hien bang lenh nay");
}

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

void doHomology(long arg) {
  if (arg < 0 || arg > 5) {
    Serial.println("usage: h <0-5>  (0=no check, 5=require identical)");
    return;
  }
  Serial.println(fp.configEnroll(TM_PID_HOMOLOGY, (uint8_t)arg)
                 ? "Homology level set" : "Set FAILED");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.print("TM1026M driver v");
  Serial.println(TM1026M_LIB_VERSION);  // must show 1.1.5 — if older, update the library!

  fp.setPowerPin(PIN_FP_POWER);
  fp.setWakePin(PIN_FP_WAKE);

  if (!fp.begin(Serial1, FP_RX_PIN, FP_TX_PIN)) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("TM1026M ready. Type '?' for command help.");

  printHelp();
  doVersion();
}

void loop() {
  // Skip blank lines so a stray Enter doesn't print "Unknown command".
  int c;
  do {
    if (!Serial.available()) { delay(20); return; }
    c = Serial.read();
  } while (c == '\r' || c == '\n' || c == ' ' || c == '\t');
  char cmd = (char)c;

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
    case 'h': doHomology(arg); break;
    case '?': printHelp(); break;
    default:
      Serial.print("Unknown command '");
      Serial.print(cmd);
      Serial.println("'. Type '?' for help.");
      break;
  }
}
