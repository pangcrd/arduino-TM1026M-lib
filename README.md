# TM1026M — Arduino driver cho module vân tay BIOSEC TM1026M

Điểm chính: **tự chọn UART theo MCU** — khai báo `TM1026M fp;` rồi `fp.begin();`
là chạy, không cần nhớ Serial nào cho từng board. Muốn tự chọn thì vẫn được.

## Đấu nối/ Wiring

| Chân module | Nối tới | Ghi chú |
|---|---|---|
| 1 `V_TOUCH` | 3.3V | **Luôn cấp nguồn**, không được cắt |
| 2 `TOUCH_OUT` | GPIO input (VD: D4) | HIGH = có ngón tay (dùng `fingerTouched()` / ngắt wake-up) |
| 3 `VCC` | 3.3V qua MOSFET | Điều khiển bằng `setPowerPin()`; nên dùng **LDO riêng**, không chung với MCU |
| 4 `TX` | **RX** của UART đã chọn | Module → MCU |
| 5 `RX` | **TX** của UART đã chọn | MCU → Module |
| 6 `GND` | GND | Chung mass |

- UART: **115200 bps, 8N1, 3.3V TTL**. MCU 5V (Uno/Nano) thì TX của MCU phải qua
  chia áp / level-shifter trước khi vào chân RX của module.
- Trước khi cắt VCC phải đưa chân UART của MCU về **input high-Z**
  (driver tự làm trong `powerOff()`), và **không cắt nguồn khi module đang chụp** —
  gọi `abortCapture()` trước.
- Nếu bật self-learning (`configEnroll(TM_PID_SELF_LEARN, ...)`), sau khi so sánh
  phải **đợi 300ms mới được cắt nguồn**, không sẽ hỏng dữ liệu tự học.

## Cài đặt / Setting

Copy thư mục `TM1026M/` vào `Arduino/libraries/`

## Chọn UART theo MCU

| MCU | Cổng mặc định | Pins mặc định |
|---|---|---|
| ESP32 (classic) | `Serial2` | RX=16, TX=17 |
| ESP32-S2/S3/C3/C6/H2/**S31** | `Serial1` | theo board variant (nên truyền pins rõ ràng) |
| ESP8266 | `Serial` | cố định theo chip |
| Mega / Leonardo / STM32 / RP2040 / SAMD / nRF52 | `Serial1` (nếu có) | theo variant |
| Uno / Nano (ATmega328P) | `Serial` | — (không còn UART thừa cho debug USB) |

```cpp
#include <TM1026M.h>

TM1026M fp;                       // tự chọn cổng theo MCU

void setup() {
  Serial.begin(115200);
  Serial.println(TM1026M::defaultPortName());  // in ra cổng đang dùng, VD "Serial1"

  fp.setPowerPin(5);              // chân điều khiển VCC (bỏ qua nếu VCC đấu cứng)
  fp.setWakePin(4);               // chân TOUCH_OUT (không bắt buộc)

  if (!fp.begin()) { /* module không trả lời: kiểm tra nguồn + dây */ }
}

// --- Ghi đè thủ công khi cần: ---
// TM1026M fp(Serial1);            // chọn cổng, pins tự động
// fp.begin(Serial1, 16, 17);      // chọn cổng + pins (ESP32 / RP2040 remap được)
```

## API chính

```cpp
// Nguồn & handshake
fp.powerOn() / fp.powerOff()      // đúng flow datasheet (handshake / high-Z)
fp.handshake()                    // fast-init phụ lục 1 (thay cho delay 150ms)
fp.abortCapture()                 // §3.20: thoát trạng thái chụp ngay lập tức
fp.fingerTouched()                // đọc TOUCH_OUT
fp.detectFinger()                 // §3.16: hỏi module "có ngón tay không?"

// Đăng ký (SFM-V1.7 §3.1) — 3C3R mặc định, 3 lần nhấn, NHẤC TAY giữa các lần
uint16_t uid = 0;                 // id 1..10000; id = 0 → module tự cấp, trả về qua uid
fp.enroll(uid, /*perm=*/1);       // true = xong, uid = id đã đăng ký
fp.enrollNCNR(uid, 1, presses);   // cần configEnroll(TM_PID_ENROLL_MODE, 6) trước

//比对
uint16_t id; uint8_t perm;
fp.match1N(id, perm);             // 1:N; thành công ⟺ id != 0
fp.match1N(id, perm, /*role=*/2); // chỉ比对 user có quyền = 2
fp.match1to1(userId);             // 1:1

// Quản lý user
fp.deleteUser(id);                // xóa theo id
fp.deleteUser(0, /*role=*/2);     // xóa TẤT CẢ user quyền 2
fp.deleteAllUsers();
fp.userCount();
fp.userPermission(id);
fp.firstFreeId();                 // id trống đầu tiên (§3.8)
fp.listUsers(users, maxUsers);    // liệt kê id + quyền (§3.14)

// Cấu hình module
fp.setMatchLevel(0..2);           // §3.13: 0..2 (mặc định 2, càng cao càng chặt)
fp.getMatchLevel();
fp.setDuplicateMode(true/false);  // mặc định sau power-up: cho trùng
fp.setBaudRate(TM_BAUD_9600);     // §3.9: đổi baud, driver tự mở lại UART đúng baud
fp.setLed(TM_LED_RED, TM_LED_BLUE, 96);  // §3.19: LED heart beat(period 30..200)
fp.configEnroll(TM_PID_CAPTURE_COUNT, 5); // §3.18: PID cấu hình enroll
fp.getVersion(buf, len);          // chuỗi version
fp.getUniqueId(uid8);             // 8 byte ID duy nhất của module
fp.sleep();

// Capture (cần RAM! feature 8KB, ảnh 176×176 ≈ 30KB — chỉ nên dùng trên ESP32/...)
uint8_t feat[TM1026M_FEATURE_LEN]; // 8192 byte
fp.captureFeature(feat);
uint8_t img[176*176]; uint16_t w, h;
fp.captureImage(img, sizeof(img), w, h);

// Thô
fp.command(cmd, p1, p2, p3, ack); // gửi frame 8-byte, tự check checksum
TM1026M::statusName(fp.lastStatus())
```

Frame protocol: `F5 TYPE P1 P2 P3 00 CHK F5`, `CHK = XOR các byte 2..6`.
Mã ACK mở rộng: `0x0A` lỗi phần cứng, `0x10` lỗi ảnh, `0x11` phát hiện贴膜攻击,
`0x12` lỗi同源性, `0x18` lệnh bị打断.

## Ví dụ

- `examples/AutoSelect` — chọn cổng tự động + match 1:N khi chạm.
- `examples/EnrollMatch` — menu qua Serial Monitor: `e <id>` đăng ký
  (`e 0` = tự cấp id), `m [role]`比对, `c` đếm, `d <id>` xóa,
  `f` id trống, `v` version, `l` liệt kê users.

## Xử lý sự cố

- `begin()` trả về false → kiểm tra: V_TOUCH có 3.3V chưa, VCC đã bật chưa,
  TX/RX có đấu chéo chưa, baud 115200.
- Enroll fail với `USER_EXIST` → đang ở chế độ cấm trùng, hoặc ngón tay đã đăng ký.
- Enroll fail với `SPOOF_DETECTED` → module phát hiện  (dán film).
- Match 1:N treo lâu → module mặc định đợi ngón tay (timeout 8s); gửi bất kỳ
  lệnh nào để ngắt (VD `fp.abortCapture()`).
- `captureFeature` cần **8KB RAM trống** — không dùng trên Uno/Nano.
