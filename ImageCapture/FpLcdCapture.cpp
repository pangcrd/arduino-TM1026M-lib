/*
 * FpLcdCapture.cpp — module chup anh van tay hien thi len LCD.
 *
 * display_conf.h la file cau hinh LCD goc 
 * Anh van tay: grayscale 8-bit, 0 = vanh toi (ridge). Chuyen sang RGB565
 * (LUT 256 phan tu) theo tung hang roi day qua 1 address window.
 *
 */
#include "FpLcdCapture.h"

#include <stdarg.h>
#include <stdio.h>

#include "display_conf.h"

LGFX tft;  // instance duy nhat; display_conf.h khai bao extern

namespace {

constexpr int16_t kScreenW = 320;
constexpr int16_t kScreenH = 240;

constexpr int16_t kHeaderH = 36;
constexpr uint16_t kHeaderBg = 0x014B;  // xanh navy dam (RGB565)

constexpr int16_t kImgSize = 176;
constexpr int16_t kOffsetX = 0;    // + sang phai, - sang trai
constexpr int16_t kOffsetY = 20;  // + xuong duoi, - len tren
constexpr int16_t kImageX = (kScreenW - kImgSize) / 2 + kOffsetX;
constexpr int16_t kImageY = (kScreenH - kImgSize) / 2 + kOffsetY;
constexpr int16_t kFrameX = kImageX - 2;                // vien ngoai 2px
constexpr int16_t kFrameY = kImageY - 2;
constexpr int16_t kFrameSize = kImgSize + 4;            // 180

constexpr int16_t kBandY = 256;
constexpr int16_t kBandH = kScreenH - kBandY;           // 64
constexpr int16_t kLine1Y = kBandY + 12;
constexpr int16_t kLine2Y = kBandY + 36;
constexpr int16_t kLineSingleY = kBandY + (kBandH - 16) / 2;

constexpr int16_t kLineBufMax = 240;     // 1 hang, clamp theo chieu rong man hinh
constexpr uint32_t kErrorShowMs = 1500;

// Buffer anh tinh (khong de tren stack): captureImage() ghi day buffer nay.
uint8_t s_img[176 * 176];

uint16_t s_lut[256];  // gray 8-bit -> RGB565 (chua swap)

// Timer khong chan thay cho delay() sau khi chup loi.
bool s_idlePending = false;
uint32_t s_idleAt = 0;

// Sau begin() hien "DANG KHOI DONG"; lan update() dau tien chuyen sang idle.
bool s_needIdle = false;

// Log serial khong chan: chua mo monitor / buffer day thi bo qua dong log.
void logf_safe(const char *fmt, ...) {
  char b[96];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(b)) n = sizeof(b) - 1;
  if (!Serial) return;
  if (Serial.availableForWrite() < n) return;
  Serial.write((const uint8_t *)b, (size_t)n);
}

void buildLut() {
  for (int g = 0; g < 256; g++) {
    s_lut[g] = (uint16_t)(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
  }
}

// In chu can giua ngang, co nen -> khong de lai vet khi doi noi dung.
void drawCentered(int16_t y, const char *msg, uint16_t fg, uint16_t bg) {
  tft.setTextSize(2);
  tft.setTextColor(fg, bg);
  int16_t x = (kScreenW - tft.textWidth(msg)) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, y);
  tft.print(msg);
}

void drawHeader() {
  tft.fillRect(0, 0, kScreenW, kHeaderH, kHeaderBg);
  drawCentered((kHeaderH - 16) / 2, "CHUP VAN TAY TM1026M", TFT_WHITE, kHeaderBg);
}

void drawFrame(uint16_t color) {
  tft.drawRect(kFrameX, kFrameY, kFrameSize, kFrameSize, color);
  tft.drawRect(kFrameX + 1, kFrameY + 1, kFrameSize - 2, kFrameSize - 2, color);
}

void clearInterior() {
  tft.fillRect(kImageX, kImageY, kImgSize, kImgSize, TFT_BLACK);
}

// Chu giua khung anh (idle / dang chup / loi).
void drawInteriorText(const char *msg, uint16_t color) {
  clearInterior();
  drawCentered(kImageY + (kImgSize - 16) / 2, msg, color, TFT_BLACK);
}

// Vung thong bao: toi da 20 ky tu / dong (size 2 = 12px/ky tu).
void drawBand(const char *l1, const char *l2, uint16_t color) {
  tft.fillRect(0, kBandY, kScreenW, kBandH, TFT_BLACK);
  if (l2 == nullptr) {
    drawCentered(kLineSingleY, l1, color, TFT_BLACK);
  } else {
    drawCentered(kLine1Y, l1, color, TFT_BLACK);
    drawCentered(kLine2Y, l2, color, TFT_BLACK);
  }
}

}  // namespace

FpLcdCapture::FpLcdCapture(TM1026M &fp, uint8_t buttonPin)
    : _fp(fp), _buttonPin(buttonPin) {}

void FpLcdCapture::begin() {
  pinMode(_buttonPin, INPUT_PULLUP);
  _lastReading = digitalRead(_buttonPin);
  _stableState = _lastReading;
  _lastDebounceTime = millis();

  buildLut();

  tft.init();
  tft.setBrightness(0);  // tat den nen cho den khi ve xong -> khong thay rac
  tft.setRotation(0);    // doc 240x320; neu anh bi xoay ngang thi doi sang 1
  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);  // fill toan man hinh DUNG 1 LAN o day

  drawHeader();
  drawFrame(TFT_YELLOW);
  drawInteriorText("KHOI DONG", TFT_YELLOW);
  drawBand("DANG KHOI DONG", "CAM BIEN...", TFT_YELLOW);
  s_needIdle = true;

  tft.setBrightness(255);  // bat den nen sau khi man hinh da san sang
}

void FpLcdCapture::update() {
  if (s_needIdle) {  // setup() da xong -> vao man hinh cho
    s_needIdle = false;
    showIdle();
  }
  // Het thoi gian hien loi -> quay ve idle (khong dung delay).
  if (s_idlePending && (int32_t)(millis() - s_idleAt) >= 0) {
    s_idlePending = false;
    showIdle();
  }
  if (buttonPressed()) {
    captureAndShow();
  }
}

bool FpLcdCapture::captureAndShow() {
  s_idlePending = false;
  showPrompt();

  uint16_t w = 0, h = 0;
  bool ok = _fp.captureImage(s_img, sizeof(s_img), w, h);  // blocking (thu vien cam bien)
  if (ok) {
    logf_safe("Capture: %ux%u\n", (unsigned)w, (unsigned)h);
    showImage(s_img, w, h);
    drawFrame(TFT_GREEN);
    drawBand("DA CHUP XONG", "BAM NUT DE CHUP LAI", TFT_GREEN);
    return true;
  }

  logf_safe("Capture FAILED (%s)\n", TM1026M::statusName(_fp.lastStatus()));
  drawFrame(TFT_RED);
  drawInteriorText("LOI", TFT_RED);
  drawBand("CHUP THAT BAI", "THU LAI", TFT_RED);
  s_idlePending = true;
  s_idleAt = millis() + kErrorShowMs;
  return false;
}

void FpLcdCapture::showIdle() {
  drawFrame(TFT_DARKGREY);
  drawInteriorText("CHUA CO ANH", TFT_DARKGREY);
  drawBand("BAM NUT GPIO0", "DE CHUP VAN TAY", TFT_WHITE);
}

void FpLcdCapture::showStatus(const char *msg) {
  drawBand(msg, nullptr, TFT_WHITE);
}

bool FpLcdCapture::buttonPressed() {
  int reading = digitalRead(_buttonPin);
  if (reading != _lastReading) {
    _lastDebounceTime = millis();  // co thay doi -> cho on dinh lai
  }
  _lastReading = reading;
  if ((millis() - _lastDebounceTime) > kDebounceMs) {
    if (reading != _stableState) {
      _stableState = reading;
      if (_stableState == LOW) return true;  // canh xuong: vua bam nut
    }
  }
  return false;
}

void FpLcdCapture::showPrompt() {
  drawFrame(TFT_YELLOW);
  drawInteriorText("DANG CHUP...", TFT_YELLOW);
  drawBand("DAT NGON TAY LEN", "CAM BIEN", TFT_YELLOW);
}

void FpLcdCapture::showImage(const uint8_t *img, uint16_t w, uint16_t h) {
  if (img == nullptr || w == 0 || h == 0) {
    drawFrame(TFT_RED);
    drawInteriorText("ANH RONG", TFT_RED);
    showStatus("ANH RONG");
    return;
  }

  // Clamp theo khung anh; neu anh nho hon khung thi xoa nen truoc.
  uint16_t dw = (w > (uint16_t)kImgSize) ? (uint16_t)kImgSize : w;
  uint16_t dh = (h > (uint16_t)kImgSize) ? (uint16_t)kImgSize : h;
  if (dw > (uint16_t)kLineBufMax) dw = kLineBufMax;
  if (dw < kImgSize || dh < kImgSize) clearInterior();

  // Can giua trong khung -> khung nam giua man hinh nen anh cung giua man hinh.
  int16_t x0 = kImageX + (kImgSize - (int16_t)dw) / 2;
  int16_t y0 = kImageY + (kImgSize - (int16_t)dh) / 2;

  static uint16_t line[kLineBufMax];
  tft.startWrite();
  tft.setAddrWindow(x0, y0, dw, dh);
  for (uint16_t y = 0; y < dh; y++) {
    const uint8_t *row = img + (size_t)y * w;  // stride theo w goc
    for (uint16_t x = 0; x < dw; x++) {
      line[x] = s_lut[row[x]];  // 0 = vanh toi, 255 = ranh sang
    }
    tft.pushPixels(line, dw, true);  // swap=true: host LE -> panel BE
  }
  tft.endWrite();
}

void FpLcdCapture::printCentered(int16_t y, const char *msg, uint16_t color) {
  drawCentered(y, msg, color, TFT_BLACK);
}