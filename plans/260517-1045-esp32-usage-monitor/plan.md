---
title: "ESP32 Xiaozhi → Claude/Codex usage monitor"
slug: esp32-usage-monitor
status: in_progress
priority: P2
effort: "6.5d"
created: 2026-05-17
source: plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md
---

# ESP32 Xiaozhi → Claude/Codex Usage Monitor

Convert existing Xiaozhi alarm-clock (ESP32-S3 N16R8, 16MB flash + 8MB PSRAM, native USB-JTAG, currently running official xiaozhi-esp32 v2.0.8) into single-purpose desk gadget. Press power button → wake from deep sleep → fetch Claude + Codex usage → render 4 progress bars + mood mascot → sleep.

## Architecture (3 layers)

```
[Mac]                  [Docker @ 192.168.1.120]              [ESP32-S3 Xiaozhi]
claude-collector       usage-api (FastAPI+SQLite)    HTTPS    forked xiaozhi-esp32
codex-collector  ──>   cloudflared tunnel            <──      LVGL UI + deep sleep
                       /collect/{src}  /status
```

## Phases

| # | Phase | Effort | Status | Blocker |
|---|---|---|---|---|
| 01 | [Hardware identify + ESP-IDF toolchain](./phase-01-hardware-identify.md) | 0.5d | done | — |
| 02 | [Usage-API skeleton](./phase-02-usage-api-skeleton.md) | 0.5d | done | — |
| 03 | [Claude collector (ccusage)](./phase-03-claude-collector.md) | 0.5d | local done | 02 |
| 04 | [Codex collector (wham/usage API)](./phase-04-codex-collector.md) | 0.5-1d | local done; auth needed | 02 |
| 05 | [Firmware fork + strip](./phase-05-firmware-fork-and-strip.md) | 1d | pending | 01 |
| 06 | [ESP32 usage UI (LVGL)](./phase-06-esp32-usage-ui.md) | 1.5d | pending | 02, 05 |
| 07 | [Deep sleep + button wake](./phase-07-deep-sleep-button-wake.md) | 1d | pending | 06 |
| 08 | [Hardening + Cloudflare Tunnel](./phase-08-hardening-cf-tunnel.md) | 0.5d | scaffolded | 02, 03, 04 |

Critical path: 01 → 05 → 06 → 07 (firmware track, ~4d). API track (02 → 03/04 → 08) runs parallel.

## Key Decisions (locked, do NOT re-debate)

- **Firmware base**: Fork github.com/78/xiaozhi-esp32 v2.0.8, strip audio/AI/cloud, ESP-IDF v5.5
- **Claude data source**: `ccusage` npm tool parsing `~/.claude/projects/*.jsonl` (no public API exists)
- **Codex data source**: `GET https://chatgpt.com/backend-api/wham/usage` (JSON API, NOT DOM scrape). Auth via JWT Bearer + session cookies, manual weekly refresh, webhook on 401
- **Exposure**: HTTPS only via Cloudflare Tunnel (`cloudflared`), real TLS cert
- **No OTA**: USB-C re-flash acceptable for personal device
- **UI**: 4 progress bars (Claude 5h, Claude week, Codex 5h, Codex week) + mood mascot + 2 character sprites

## Open Items (resolved in Phase 01)

- Exact xiaozhi-esp32 board variant name: `sp-esp32-s3-1.54-muma`, confirmed by boot log.
- Display resolution + GPIO map: recorded in `reports/phase-01-board-identification.md`.
- Button power GPIO: GPIO0, RTC-capable candidate.
- Deep-sleep current measurement vs. <100µA target: still pending hardware meter validation.

## Source Documents

- Brainstorm: `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md`
- Upstream firmware: github.com/78/xiaozhi-esp32

## Success Criteria (whole project)

- [ ] Press button → screen shows accurate Claude % within 3s
- [ ] Same for Codex
- [ ] Mood mascot reflects state across 4 ranges (chill/cruising/careful/cooked)
- [ ] Battery ≥30 days with 50 wakes/day
- [ ] WiFi loss / API down → graceful offline UI, no crash
- [ ] Codex auth expiry → user gets webhook alert
- [ ] No secrets in firmware binary (NVS encrypted)
