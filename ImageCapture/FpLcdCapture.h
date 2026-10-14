#pragma once
/*
 * FpLcdCapture.h — chup anh van tay TM1026M va hien thi len LCD ST7789.
 */
#include <Arduino.h>

#include "TM1026M.h"

class FpLcdCapture {
 public:
  FpLcdCapture(TM1026M &fp, uint8_t buttonPin);

  void begin();   // goi TRUOC fp.begin() de LCD len ngay
  void update();  // goi trong loop()

  bool captureAndShow();  // chup + hien thi; true neu thanh cong

  // De .ino hien thong bao (vd: cam bien khoi dong that bai).
  void showIdle();
  void showStatus(const char *msg);

 private:
  bool buttonPressed();  // debounce, true khi vua bam xuong
  void showPrompt();
  void showImage(const uint8_t *img, uint16_t w, uint16_t h);
  void printCentered(int16_t y, const char *msg, uint16_t color);

  static constexpr uint32_t kDebounceMs = 30;

  TM1026M &_fp;
  uint8_t _buttonPin;
  int _lastReading = HIGH;
  int _stableState = HIGH;
  uint32_t _lastDebounceTime = 0;
};