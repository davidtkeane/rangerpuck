# ESP32-C6-LCD-1.47 — confirmed pinout

**Confirmed 2026-09-12.** Do not edit these from memory. If something does not work,
re-check against the sources below rather than guessing a different pin.

## Display — ST7789, 172 × 320, SPI

| Signal | GPIO | Notes |
|--------|------|-------|
| SCL (SPI clock) | **7** | |
| SDA (MOSI) | **6** | no MISO — display is write-only |
| RST (reset) | **21** | |
| DC (data/command) | **15** | |
| CS (chip select) | **14** | |
| BLK (backlight) | **22** | ⚠️ **check this first if the screen is black** |
| VDD | 3.3 V | |
| GND | GND | |

## RGB LED

| Signal | GPIO | Notes |
|--------|------|-------|
| WS2812 data | **8** | single addressable RGB LED, driven over SPI |

## Board facts

- MCU: **ESP32-C6**, RISC-V, 160 MHz HP core + 20 MHz LP core
- Flash 4 MB · 512 KB HP SRAM · 320 KB ROM
- **Native USB** (full-speed) — appears as a serial device without a USB-UART bridge
- microSD slot on board
- Wi-Fi 6 (2.4 GHz only), BLE 5, IEEE 802.15.4

## Toolchain notes

- Needs **ESP32 Arduino core 3.x**. Core 2.x does **not** support the C6 at all.
- Board name in arduino-cli: `esp32:esp32:esp32c6`
- Serial monitor: **115200**
- Display library: any ST7789 driver (TFT_eSPI, Adafruit_ST7789, LovyanGFX). TFT_eSPI
  needs a `User_Setup.h` written for these exact pins — that file is where most
  "black screen" problems actually live.

## Sources

- [Waveshare wiki — ESP32-C6-LCD-1.47](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
- [Waveshare docs](https://docs.waveshare.com/ESP32-C6-LCD-1.47)
- [AndroidCrypto — getting-started write-up](https://medium.com/@androidcrypto/getting-started-with-an-esp32-c6-waveshare-lcd-device-with-1-47-inch-st7789-tft-display-07804fdc589a)
- [AndroidCrypto — starter repo with working User_Setup.h](https://github.com/AndroidCrypto/ESP32_C6_Waveshare_ST7789_Starter)
- [Zephyr board docs](https://docs.zephyrproject.org/latest/boards/waveshare/esp32c6_lcd_1_47/doc/index.html)

⚠️ **Careful:** there is also an **ESP32-C6-Touch-LCD-1.47**, a different board with a
touch layer. Its pinout is not guaranteed to match. Ours is the non-touch version.
