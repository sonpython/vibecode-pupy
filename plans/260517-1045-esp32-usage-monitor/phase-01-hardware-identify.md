---
phase: 1
title: "Hardware identify + ESP-IDF toolchain"
status: done
priority: P1
effort: "0.5d"
dependencies: []
---

# Phase 01: Hardware Identify + ESP-IDF Toolchain

## Overview
Pin down the exact xiaozhi-esp32 board variant matching our physical device, capture its display/button GPIO map from upstream board config, and set up an ESP-IDF v5.5 toolchain locally so subsequent firmware phases can build immediately.

## Context Links
- Brainstorm §9 hardware open items: `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md`
- Upstream: github.com/78/xiaozhi-esp32 (path: `main/boards/`)
- Known hardware (USB probe verified): ESP32-S3 N16R8, 16MB flash, 8MB PSRAM, MAC `a0:f2:62:e8:a4:40`, currently running xiaozhi v2.0.8 / ESP-IDF v5.5

## Requirements
### Functional
- Identify exact board name in `xiaozhi-esp32/main/boards/<board>/`
- Document: display controller + resolution + SPI pins, backlight GPIO, power button GPIO, battery voltage ADC pin, charge status pin (if any)
- ESP-IDF v5.5 toolchain operational; clone xiaozhi-esp32; build + flash stock firmware unchanged → device still boots into Xiaozhi UI

### Non-functional
- Toolchain on macOS (M-series); ESP-IDF in `~/esp/esp-idf` (standard layout)
- No destructive changes to factory NVS (preserve `_puid` style keys if any) — backup full flash first

## Architecture
Decision tree for board identification (cheap → expensive):

```
1. Boot device → idf.py monitor → grep serial output for "Board:", "BOARD_TYPE", "init_board"
2. If not printed → flash xiaozhi-esp32 main branch with menuconfig "Board Type" cycling through likely candidates (compile + check display init success)
3. If still unknown → open enclosure, photograph PCB silkscreen near module + display flex, match to board photos in repo's docs/
```

Likely candidates (Xiaozhi alarm-clock form factor + ESP32-S3 N16R8):
- `taiji-pi-s3`, `lichuang-c3`, `xmini-c3-clock`, `bread-compact-wifi-lcd`, `m5stack-core-s3` (unlikely — too large)
- Custom OEM build for JQRNZ/Estella (might not be upstream → fork closest match)

## Related Code Files
- Create: `~/esp/esp-idf/` (ESP-IDF v5.5 install)
- Create: `~/projects/xiaozhi-esp32-fork/` (git clone)
- Create: `plans/260517-1045-esp32-usage-monitor/reports/phase-01-board-identification.md` (findings)

## Implementation Steps

1. **Backup current flash** (safety net for revert):
   ```bash
   esptool --port /dev/cu.usbmodem83101 --baud 921600 read-flash 0x0 0x1000000 /tmp/xiaozhi-factory-backup.bin
   ```

2. **Install ESP-IDF v5.5** via official installer:
   ```bash
   mkdir -p ~/esp && cd ~/esp
   git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf && ./install.sh esp32s3
   ```
   Add `. ~/esp/esp-idf/export.sh` to shell rc.

3. **Clone xiaozhi-esp32 v2.0.8**:
   ```bash
   cd ~/projects && git clone https://github.com/78/xiaozhi-esp32.git xiaozhi-esp32-fork
   cd xiaozhi-esp32-fork && git checkout v2.0.8  # (or matching tag)
   ```

4. **Try serial monitor first** (read board name from boot log):
   ```bash
   idf.py -p /dev/cu.usbmodem83101 monitor
   ```
   Look for: `Board:`, `Initializing display`, `GPIO_BUTTON_*`, `MCP-Xiao`, panel driver lines like `ST7789`, `GC9A01`, etc.

5. **If board name not in log** → list candidates:
   ```bash
   ls main/boards/
   ```
   Cross-reference each `config.json` / `Kconfig.projbuild` against observed physical clues (button count, display shape).

6. **Verify by rebuild**: `idf.py set-target esp32s3; idf.py menuconfig` → select candidate board → `idf.py build flash` → device boots Xiaozhi unchanged.

7. **Document findings** in `reports/phase-01-board-identification.md`:
   - Board name (upstream key)
   - Display: controller, resolution, SPI pins (MOSI, SCLK, CS, DC, RST), backlight GPIO
   - Power button: GPIO #, RTC-capable yes/no, pull direction
   - Battery: ADC channel, voltage divider ratio
   - I2C bus (if any): SDA, SCL, address map (touch? audio codec?)

8. **Quick µA sanity check**: with USB unplugged + USB power meter inline, observe idle current after firmware boots to a static screen. Note baseline.

## Todo List
- [x] Factory flash backup preserved (`~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin`, 16MB)
- [x] ESP-IDF v5.5 installed, `idf.py --version` works
- [x] xiaozhi-esp32 cloned; upstream has no `v2.0.8` tag, so `v2.0.5` was used as the closest 2.0.x baseline
- [x] Board name identified (recorded in report)
- [x] Display + button + battery pins documented
- [x] Stock rebuild flashes successfully + boots
- [ ] Idle current baseline noted

## Success Criteria
- [x] Report file lists exact board name with all GPIO assignments
- [x] `idf.py build` succeeds against the identified board config
- [x] Device still boots stock Xiaozhi after rebuild (confirms toolchain + board choice)

## Risk Assessment
| Risk | Mitigation |
|---|---|
| Board not in upstream repo (custom OEM) | Use closest match as starting point; document deviations; create new `boards/jqrnz-clock/` in our fork during Phase 05 |
| ESP-IDF v5.5 install breaks (cmake/ninja issues) | Use ESP-IDF Docker image as fallback (`espressif/idf:release-v5.5`) |
| Flash backup corrupted / unable to revert | Keep both factory backup and current stock binary (two copies); never overwrite ota_0 + ota_1 simultaneously |
| Native USB-JTAG disabled by current firmware → cannot reflash | Use BOOT button (manual download mode); add to docs |

## Security Considerations
- Factory backup contains NVS with potential WiFi credentials / device-id — store locally only, do not commit
- Do not enable Secure Boot in Phase 1 (one-way operation, blocks all rework)

## Next Steps
→ Phase 02 (Usage-API) can run in parallel; firmware track waits for this phase to finish.
