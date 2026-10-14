/*
 * FpLcdCapture — bam nut de chup anh van tay, hien len LCD ST7789.
 *
 * Luong dung: bam nut GPIO0 (hoac nut BOOT tren board) -> dat ngon tay
 * len cam bien -> anh van tay hien len LCD. Bam nut lai de chup tiep.
 * KHONG so khop — chi de test nhanh cam bien / LCD.
 *
 * Can cai them thu vien LovyanGFX (Arduino IDE: Sketch -> Include
 * Library -> Manage Libraries -> tim "LovyanGFX").

 */
#include <TM1026M.h>

#include "FpLcdCapture.h"

#if !defined(ARDUINO_ARCH_ESP32)
#error "FpLcdCapture can board ESP32 (display_conf.h dung SPI2_HOST cua ESP32 + anh 176x176 can ~31KB RAM)"
#endif

// Chan UART cho module van tay (tranh chan LCD 10/11/12/13/45/46):
#define FP_RX_PIN     18
#define FP_TX_PIN     17

TM1026M fp(Serial1);
FpLcdCapture viewer(fp, /*buttonPin=*/0);  // nut bam o GPIO0

void setup() {
  viewer.begin();

  Serial.begin(115200);
  Serial.print("TM1026M driver v");
  Serial.println(TM1026M_LIB_VERSION);

  if (!fp.begin(Serial1, FP_RX_PIN, FP_TX_PIN)) {
    Serial.println("TM1026M not responding — check wiring and power.");
    while (true) { delay(1000); }
  }
  Serial.println("TM1026M ready. Bam nut GPIO0 (nut BOOT) de chup van tay.");
}

void loop() {
  viewer.update();  // debounce nut -> bam nut la chup + hien anh
}
