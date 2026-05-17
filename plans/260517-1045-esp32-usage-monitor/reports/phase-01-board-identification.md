# Phase 01 Board Identification Report

Date: 2026-05-17
Actor: codex-cli

## Summary

The physical Xiaozhi device is confirmed as an ESP32-S3 N16R8-class board using the upstream `xiaozhi-esp32` board profile:

- Upstream board key: `sp-esp32-s3-1.54-muma`
- Boot-log SKU: `sp-esp32-s3-1.54-muma`
- Target: `esp32s3`
- Flash: 16MB
- PSRAM: 8MB
- MAC: `a0:f2:62:e8:a4:40`
- USB port: `/dev/cu.usbmodem83101`

`xiaozhi-esp32` tag `v2.0.8` does not exist upstream. The nearest 2.0.x tag found is `v2.0.5`, which built successfully with ESP-IDF v5.5.

## Toolchain

ESP-IDF v5.5 is installed at:

```bash
~/esp/esp-idf-v5.5
```

Because the local Python environment auto-selected Python 3.14 while ESP-IDF installed an IDF Python 3.13 environment, the reliable shell bootstrap is:

```bash
export IDF_PATH=$HOME/esp/esp-idf-v5.5
export IDF_TOOLS_PATH=$HOME/.espressif
. $HOME/.espressif/python_env/idf5.5_py3.13_env/bin/activate
eval "$($IDF_PATH/tools/idf_tools.py export --format shell)"
```

Verified:

- `idf.py --version` -> `ESP-IDF v5.5`
- `python scripts/release.py sp-esp32-s3-1.54-muma` completed
- App binary size: `0x2a3e00`; smallest app partition: `0x3f0000`; 33% free
- Release zip generated: `~/projects/xiaozhi-esp32-fork/releases/v2.0.5_sp-esp32-s3-1.54-muma.zip`

## Flash Safety

Existing factory backup preserved:

```text
~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin
SHA256 3a6a8a1f8a3a46be3993cd46ca3d0371c4003b953257995be0b2de789f6b583f
```

Fresh backup attempts on 2026-05-17 failed at both 921600 and 460800 baud with serial stream corruption. No new backup file was produced. The preserved factory backup remains the rollback source.

## Stock Flash Verification

Flashed stock `v2.0.5` build for `sp-esp32-s3-1.54-muma`:

```bash
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem83101 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x20000 build/xiaozhi.bin \
  0x800000 build/generated_assets.bin
```

All written ranges verified hash successfully.

Boot log confirms:

```text
Project name: xiaozhi
App version: 2.0.5
ESP-IDF: v5.5
Board: UUID=e1ce5437-8cfa-4421-8e89-d01eea51c2dc SKU=sp-esp32-s3-1.54-muma
LcdDisplay: Turning display on
LcdDisplay: Adding LCD display
Backlight: Set brightness to 75
WifiConfigurationAp: Access Point started with SSID Xiaozhi-A441
WifiConfigurationAp: Web server started
```

The device reaches WiFi provisioning mode at AP `Xiaozhi-A441`, IP `192.168.4.1`.

## Pin Map From Upstream Board Profile

Source files:

- `~/projects/xiaozhi-esp32-fork/main/boards/sp-esp32-s3-1.54-muma/config.json`
- `~/projects/xiaozhi-esp32-fork/main/boards/sp-esp32-s3-1.54-muma/sp-esp32-s3-1.54-muma.cc`

Display and UI-relevant pins:

| Function | Value |
|---|---|
| Resolution | 240 x 240 |
| SPI SCLK | GPIO4 |
| SPI MOSI | GPIO2 |
| LCD CS | GPIO5 |
| LCD DC | GPIO47 |
| LCD reset | GPIO38 |
| Backlight | GPIO42 |
| Boot/power button | GPIO0 |
| Touch I2C SDA | GPIO11 |
| Touch I2C SCL | GPIO7 |
| Touch reset | GPIO6 |
| Touch interrupt | GPIO12 |
| Charge detect | GPIO41 |
| Battery ADC | ADC1 channel 0 |
| Power charge LED | GPIO3 |

The boot button GPIO0 is RTC-capable on ESP32-S3, so it remains a candidate wake source for Phase 07. Confirm final wake behavior in firmware after audio/cloud stripping.

## Observed Issues

Stock firmware logs repeated ES8311 I2C NACK/open failures:

```text
ES8311: Open fail
Es8311AudioCodec: Failed to create Es8311AudioCodec
```

This does not block the usage-monitor target because Phase 05 should remove audio/codec initialization entirely. Display, backlight, LVGL, PSRAM, WiFi, and provisioning all initialized.

## Next Firmware Steps

- Fork a local usage-monitor firmware from `sp-esp32-s3-1.54-muma`.
- Strip audio, wake-word, Xiaozhi cloud, and codec initialization first.
- Keep WiFi, display/LVGL, backlight, button GPIO0, ADC battery, and sleep plumbing.
- Rebuild and flash after each deletion slice to avoid losing the known-good baseline.
