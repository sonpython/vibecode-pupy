---
phase: 6
title: "ESP32 usage UI (LVGL 4-bar + mascots)"
status: pending
priority: P1
effort: "1.5d"
dependencies: [2, 5]
---

# Phase 06: ESP32 Usage UI

## Overview
Replace stub `application.cc` with the real usage display: HTTPS GET to `/status`, parse JSON, render 4 progress bars (Claude 5h + week, Codex 5h + week), draw mood mascot + 2 character sprites, handle stale/error states gracefully. End-state: pressing button cycles through wake → fetch → display, with full visual polish.

## Context Links
- Phase 02 API contract (`GET /status` response shape)
- Phase 05 firmware skeleton
- Brainstorm §5.5 UI layout sketch

## Requirements
### Functional
- HTTPS GET to `${USAGE_API_URL}/status` with `X-Device-Secret` header
- JSON parse via `cJSON` (already in ESP-IDF)
- Render layout split top half (Claude) / bottom half (Codex), each with 2 progress bars + reset time text
- Mood mascot icon at bottom: 😊 / 😐 / 😰 / 💀 driven by `max(current_pct, weekly_pct)`
- Stale indicator overlay if `stale_sec > 600`
- "WiFi disconnected" / "API unreachable" error screens (don't crash)
- "Refreshed Xs ago" footer text

### Non-functional
- Cold render under 200ms from JSON in-hand to pixels-on-screen
- Sprites + fonts fit in `assets` partition (existing 8MB plenty)
- No dynamic allocation in render path (re-use LVGL objects)

## Architecture

### Layout (assuming 240×280 typical, adjust to actual)
```
y=0…20    : "[Claude sprite] Claude" header  + battery icon  + WiFi icon
y=20…40   : "▓▓▓▓▓░░░░░ 50% Current  1h22m"
y=40…60   : "▓▓░░░░░░░░ 11% Weekly  6d 8h"
y=60…80   : separator line
y=80…100  : "[Codex sprite] Codex" header
y=100…120 : "▓▓░░░░░░░░ 1% Current  3h09m"
y=120…140 : "░░░░░░░░░░ 0% Weekly  6d 17h"
y=140…end : mood mascot + footer "refreshed 12s ago"
```

### State machine (per wake cycle)
```
SPLASH (200ms) → WIFI_CONNECTING → HTTP_FETCH → PARSE → RENDER → IDLE_LOOP (30s) → SLEEP
                       │                  │
                       └─error─► ERROR_SCREEN ──┘
```

## Related Code Files
- Modify: `firmware/main/application.cc` (full state machine)
- Create: `firmware/main/ui/usage_screen.cc` + `usage_screen.h` (LVGL widget assembly)
- Create: `firmware/main/ui/sprites.h` (raw LVGL image arrays for Claude/Codex/mood)
- Create: `firmware/main/net/status_client.cc` + `.h` (HTTPS GET + cJSON parse)
- Create: `firmware/main/util/format_duration.cc` (seconds → "1h 22m" / "6d 8h")
- Modify: `firmware/main/CMakeLists.txt` (add new sources)
- Modify: `firmware/main/Kconfig.projbuild` (add USAGE_API_URL + DEVICE_SECRET)

### Sprites
- Source: hand-pixel via Aseprite or pixilart.com
- 24×24 px sprites: `claude_mascot.png`, `codex_mascot.png`
- 32×32 px mood: `mood_chill.png`, `mood_cruising.png`, `mood_careful.png`, `mood_cooked.png`
- Convert via LVGL image converter (`lv_img_conv`) to C arrays, RGB565 + indexed8

## Implementation Steps

1. **`status_client.cc`**: wrap `esp_http_client` with bundled DigiCert root; GET to `https://usage.<domain>/status` with `X-Device-Secret` header; deserialize via cJSON into `struct UsageSnapshot`.

2. **`format_duration.cc`**: pure function `int sec → string`. < 60s → "<1m". <1h → "Xm". <24h → "Xh Ym". >=24h → "Xd Yh". Unit test in component test (`idf.py build && pytest pytest_unit/`).

3. **`sprites.h`**: include generated arrays from `lv_img_conv`. Keep file <50KB total → fits in regular flash, no asset partition needed.

4. **`usage_screen.cc`** — single function `void render(const UsageSnapshot& s)` builds tree:
   ```cpp
   lv_obj_t* root = lv_scr_act();
   // Claude block
   lv_obj_t* claude_bar1 = lv_bar_create(root);
   lv_bar_set_value(claude_bar1, s.claude.current_pct, LV_ANIM_OFF);
   // ... etc.
   ```
   Use static LVGL objects so subsequent renders just update values (no alloc/free).

5. **Mood logic**:
   ```cpp
   int worst = std::max({s.claude.current_pct, s.claude.weekly_pct,
                         s.codex.current_pct,  s.codex.weekly_pct});
   const lv_img_dsc_t* mood =
       worst < 50  ? &mood_chill    :
       worst < 75  ? &mood_cruising :
       worst < 90  ? &mood_careful  : &mood_cooked;
   lv_img_set_src(mood_widget, mood);
   ```

6. **`application.cc`** state machine — `enum State { SPLASH, WIFI, FETCH, RENDER, IDLE }`, simple switch in main task with `vTaskDelay` between transitions.

7. **Error UI**: when fetch fails → render screen with WiFi icon + last-known status text + "tap to retry". Tap = button press.

8. **Idle timeout**: 30s after render → trigger deep sleep (real sleep in Phase 07; for now, just blank display).

## Todo List
- [ ] status_client with HTTPS + cJSON
- [ ] format_duration with unit tests
- [ ] Sprites generated and committed
- [ ] usage_screen renders all 4 bars correctly
- [ ] Mood mascot switches across thresholds
- [ ] Stale + error screens both render
- [ ] End-to-end: button press → real data on screen within 3s

## Success Criteria
- [ ] Visual match to brainstorm §5.5 sketch
- [ ] Cold render ≤200ms from JSON parsed
- [ ] All 4 corner cases (stale / API down / WiFi down / fresh OK) render without crash
- [ ] No frame tearing (LVGL double-buffer enabled, 8MB PSRAM supports it)

## Risk Assessment
| Risk | Mitigation |
|---|---|
| LVGL DPI mismatch on actual display | Phase 01 captures real resolution; usage_screen uses constants from Kconfig |
| cJSON memory pressure on big payloads | Payload is <500 bytes; cJSON in static buffer pool |
| HTTPS cert bundle size | DigiCert-only bundle ~5KB; full bundle ~250KB. Use selective bundle config. |
| Sprites look bad at small DPI | Mock up in figma at actual res first; tweak sprites accordingly |

## Security Considerations
- `DEVICE_SECRET` hard-baked into firmware via Kconfig — accept for personal device; not for distribution
- HTTPS via bundled root CA — pin Cloudflare's intermediate too if paranoid (future)
- No log of secrets (esp_log_level ERROR for net component)

## Next Steps
→ Phase 07 turns the IDLE timeout into actual deep sleep + adds button wake.
