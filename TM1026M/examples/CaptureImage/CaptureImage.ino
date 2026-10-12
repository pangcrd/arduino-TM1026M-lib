/*
 * CaptureImage — capture the fingerprint bitmap and preview it as ASCII art.
 *
 * Type 'i' in the Serial Monitor, then press a finger on the sensor.
 *
 * Needs ~31 KB RAM for a 176x176 image, so this example is for
 * ESP32 / ESP8266 / RP2040 class boards (NOT Uno/Nano).
 */
#include <TM1026M.h>

#if !defined(ARDUINO_ARCH_ESP32) && !defined(ESP8266) && !defined(ARDUINO_ARCH_RP2040)
#error "CaptureImage needs a MCU with plenty of RAM (ESP32 / ESP8266 / RP2040)"
#endif

#define IMG_MAX_W 176
#define IMG_MAX_H 176

static uint8_t img[IMG_MAX_W * IMG_MAX_H];  // 30976 bytes

TM1026M fp;

// dark -> light
static const char kShade[] = "@%#*+=-:. ";

void printAscii(const uint8_t *buf, uint16_t w, uint16_t h) {
  // Downsample to ~88x44: characters are ~twice as tall as wide.
  const uint8_t bw = 2, bh = 4;
  uint16_t ow = w / bw, oh = h / bh;
  for (uint16_t y = 0; y < oh; y++) {
    for (uint16_t x = 0; x < ow; x++) {
      uint32_t sum = 0;
      for (uint8_t dy = 0; dy < bh; dy++)
        for (uint8_t dx = 0; dx < bw; dx++)
          sum += buf[(uint32_t)(y * bh + dy) * w + (x * bw + dx)];
      uint8_t avg = (uint8_t)(sum / (bw * bh));
      Serial.print(kShade[avg * 9 / 255]);
    }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.print("TM1026M driver v");
  Serial.println(TM1026M_LIB_VERSION);

  if (!fp.begin()) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("Type 'i' + press a finger to capture its image.");
}

void loop() {
  if (!Serial.available()) { delay(20); return; }
  char c = Serial.read();
  while (Serial.available()) Serial.read();
  if (c != 'i' && c != 'I') return;

  Serial.println("Press a finger on the sensor...");
  uint16_t w = 0, h = 0;
  if (fp.captureImage(img, sizeof(img), w, h)) {
    Serial.print("Got image: ");
    Serial.print(w);
    Serial.print("x");
    Serial.println(h);
    printAscii(img, w, h);
  } else {
    Serial.print("Capture FAILED (");
    Serial.print(TM1026M::statusName(fp.lastStatus()));
    Serial.println(") — try pressing firmly, covering the sensor.");
  }
}
