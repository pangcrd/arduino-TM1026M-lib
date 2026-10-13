/*
 * EnrollMatch — enroll a fingerprint and match it, with explicit UART choice.
 *
 */
#include <TM1026M.h>

#define PIN_FP_POWER  5
#define PIN_FP_WAKE   4

// Pick any free UART pins for your board:
#define FP_RX_PIN     18
#define FP_TX_PIN     17

TM1026M fp(Serial1);  // <-- explicit port; pins set in begin() below

void printHelp() {
  Serial.println("Commands (type '?' any time to see this list):");
  Serial.println("  Go lenh + so tren CUNG 1 dong, cach nhau bang dau cach (vd \"t 1\").");
  Serial.println("  Neu chi go lenh, may se hoi so o dong tiep theo.");
  Serial.println("  e <id> [role] Dang ky van tay cho user <id> (role 1/2/3, mac dinh 1;");
  Serial.println("                     vd \"e 7\", \"e 7 2\" = dang ky voi quyen 2, \"e 0\" = module tu cap ID)");
  Serial.println("           Cham CUNG 1 ngon 3 lan, NHAC HAN ngon ra giua cac lan cham.");
  Serial.println("  m [role] So khop 1:N (\"m\" = so voi tat ca user; \"m 2\" = chi so voi user quyen 2)");
  Serial.println("  t <id>   So khop 1:1 voi 1 user (de kiem tra template co bi hong khong)");
  Serial.println("  s <0-2>  Dat do gat khi so khop (0=de nhat, 2=gat nhat; reboot la ve 2)");
  Serial.println("  c        Dem so user da dang ky");
  Serial.println("  d <id>   Xoa user <id>");
  Serial.println("  D        XOA TAT CA users (can than!)");
  Serial.println("  f        ID trong dau tien con dung duoc");
  Serial.println("  v        Doc chuoi version cua module");
  Serial.println("  l        Liet ke cac user da dang ky");
  Serial.println("  h <0-5>  Muc kiem tra homology khi enroll (0=tat, 5=gat nhat)");
  Serial.println("  ?        Hien bang lenh nay");
}

void doEnroll(long id, long role) {
  if (id < 0 || id > 10000) { Serial.println("id must be 0..10000 (0 = auto)"); return; }
  uint8_t r = (role < 1 || role > 3) ? 1 : (uint8_t)role;
  uint16_t uid = (uint16_t)id;
  Serial.print("Enrolling");
  if (uid == 0) Serial.print(" (auto ID)");
  Serial.print(" with role ");
  Serial.print(r);
  Serial.println(" — press the SAME finger 3 times, lifting between presses.");
  if (fp.enroll(uid, r)) {
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

void doMatch11(long arg) {
  if (arg < 1 || arg > 10000) { Serial.println("usage: t <id>"); return; }
  Serial.println("Press the enrolled finger...");
  Serial.println(fp.match1to1((uint16_t)arg) ? "1:1 MATCH!" : "1:1 no match");
}

void doMatchLevel(long arg) {
  if (arg < 0 || arg > 2) {
    Serial.println("usage: s <0-2>  (0=loosest, 2=strictest; resets to 2 on reboot)");
    return;
  }
  if (!fp.setMatchLevel((uint8_t)arg)) { Serial.println("Set FAILED"); return; }
  Serial.print("Match level now: ");
  Serial.println(fp.getMatchLevel());
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
  Serial.println(TM1026M_LIB_VERSION);  // must show 1.1.10 — if older, update the library!

  fp.setPowerPin(PIN_FP_POWER);
  fp.setWakePin(PIN_FP_WAKE);

  if (!fp.begin(Serial1, FP_RX_PIN, FP_TX_PIN)) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("TM1026M ready. Type '?' for command help.");

  // Match level resets to 2 on every reboot (SFM-V1.7 §3.13). Level 1 is
  // the sweet spot for this sensor: sensitive, yet 10x stricter than the
  // ~1/50k FAR of phone fingerprint sensors.
  if (fp.setMatchLevel(1)) {
    Serial.println("Match level auto-set to 1 (s <0-2> to change).");
  } else {
    Serial.println("Warning: could not set match level!");
  }

  printHelp();
  doVersion();
}

/* Line-based REPL: accumulate a full line, then parse it — no timeouts,
 * so you can take as long as you want between keystrokes. If a command
 * needs a number and you typed it bare, you'll be prompted on the next
 * line (typing a new command letter cancels the prompt). */
static char pendingCmd = 0;  // command waiting for its argument

void runCommand(char cmd, long arg, long arg2, bool hasArg) {
  switch (cmd) {
    case 'e': doEnroll(arg, arg2); break;  // no arg = auto-assign ID
    case 'm': doMatch(arg); break;
    case 't':
      if (!hasArg) { Serial.print("id: "); pendingCmd = 't'; break; }
      doMatch11(arg); break;
    case 's':
      if (!hasArg) { Serial.print("level (0-2): "); pendingCmd = 's'; break; }
      doMatchLevel(arg); break;
    case 'c':
      Serial.print("Enrolled users: ");
      Serial.println(fp.userCount());
      break;
    case 'd':
      if (!hasArg) { Serial.print("id: "); pendingCmd = 'd'; break; }
      doDelete(arg); break;
    case 'D':
      Serial.println(fp.deleteAllUsers() ? "All users deleted" : "Delete failed");
      break;
    case 'f':
      Serial.print("First free ID: ");
      Serial.println(fp.firstFreeId());
      break;
    case 'v': doVersion(); break;
    case 'l': doList(); break;
    case 'h':
      if (!hasArg) { Serial.print("level (0-5): "); pendingCmd = 'h'; break; }
      doHomology(arg); break;
    case '?': printHelp(); break;
    default:
      Serial.print("Unknown command '");
      Serial.print(cmd);
      Serial.println("'. Type '?' for help.");
      break;
  }
}

void handleCommand(char *line) {
  while (*line == ' ' || *line == '\t') line++;  // skip leading blanks
  if (*line == '\0') return;                       // blank line: ignore

  if (pendingCmd) {
    // A fresh command letter cancels the pending prompt.
    if ((*line >= 'a' && *line <= 'z') || (*line >= 'A' && *line <= 'Z') ||
        *line == '?') {
      pendingCmd = 0;  // fall through, parse as a new command
    } else {
      char pc = pendingCmd;
      pendingCmd = 0;
      long arg = 0, arg2 = 0;
      if (sscanf(line, "%ld %ld", &arg, &arg2) < 1) {
        Serial.println("not a number — cancelled.");
        return;
      }
      runCommand(pc, arg, arg2, true);
      return;
    }
  }

  char cmd = *line++;
  long arg = 0, arg2 = 0;
  int n = (cmd == 'e') ? sscanf(line, "%ld %ld", &arg, &arg2)
                       : sscanf(line, "%ld", &arg);
  runCommand(cmd, arg, arg2, n >= 1);
}

void loop() {
  static char line[64];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[len] = '\0';
      len = 0;
      handleCommand(line);
      return;
    }
    if (len < sizeof(line) - 1) line[len++] = c;  // drop overflow chars
  }
}
