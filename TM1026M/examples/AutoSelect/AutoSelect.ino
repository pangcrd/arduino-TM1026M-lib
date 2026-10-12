/*
 * AutoSelect — TM1026M basic usage with per-MCU automatic UART selection.
 *
 * Just declare TM1026M fp; and the library picks the right port for your MCU:
 *   ESP32 (classic) -> Serial2 (RX=16, TX=17)
 *   ESP32-S2/S3/C3/S31 -> Serial1 (board-variant pins)
 *   Mega/STM32/RP2040/... -> Serial1, Uno -> Serial
 *
 * Wiring (module -> MCU):
 *   1 V_TOUCH   -> 3.3V (ALWAYS on)
 *   2 TOUCH_OUT -> PIN_FP_WAKE
 *   3 VCC       -> 3.3V through a MOSFET, gate driven by PIN_FP_POWER
 *   4 TX        -> MCU RX of the chosen UART
 *   5 RX        -> MCU TX of the chosen UART
 *   6 GND       -> GND
 */
#include <TM1026M.h>

#define PIN_FP_POWER  5  // drives the VCC switch (MOSFET gate / LDO EN)
#define PIN_FP_WAKE   4  // TOUCH_OUT from the module (HIGH = finger present)

TM1026M fp;  // <-- auto-select UART for this MCU

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.print("TM1026M driver v");
  Serial.println(TM1026M_LIB_VERSION);
  Serial.print("Using UART port: ");
  Serial.println(TM1026M::defaultPortName());

  fp.setPowerPin(PIN_FP_POWER);  // comment out if VCC is hard-wired
  fp.setWakePin(PIN_FP_WAKE);    // comment out if TOUCH_OUT is not wired

  // Manual override examples (uncomment as needed):
  // TM1026M fp2(Serial1);                 // pick the port, auto pins
  // fp.begin(Serial1, 16, 17);            // pick the port AND pins (ESP32/RP2040)

  if (!fp.begin()) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("TM1026M ready. Touch the sensor to match.");

  int16_t n = fp.userCount();
  Serial.print("Enrolled users: ");
  Serial.println(n);
}

void loop() {
  // Wake on touch: only run the (slow) match when a finger is present.
  if (!fp.fingerTouched()) {
    delay(50);
    return;
  }

  uint16_t id;
  uint8_t perm;
  Serial.println("Matching...");
  if (fp.match1N(id, perm)) {
    Serial.print("Match! user=");
    Serial.print(id);
    Serial.print(" perm=");
    Serial.println(perm);
  } else {
    Serial.print("No match (");
    Serial.print(TM1026M::statusName(fp.lastStatus()));
    Serial.println(")");
  }
  delay(1500);  // debounce: let the finger lift
}
