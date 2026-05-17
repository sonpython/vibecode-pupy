---
phase: 5
title: "Firmware fork and strip (xiaozhi-esp32 base)"
status: pending
priority: P1
effort: "1d"
dependencies: [1]
---

# Phase 05: Firmware Fork & Strip

## Overview
Fork xiaozhi-esp32 v2.0.8 into our own working tree, remove audio/AI/cloud subsystems we won't use, keep WiFi-provisioning + display + button + LVGL, verify it still builds and boots to a "hello" screen. Sets a clean foundation for Phase 06's UI work.

## Context Links
- Phase 01 board identification (must be done first)
- Upstream: github.com/78/xiaozhi-esp32

## Requirements
### Functional
- Fork compiles on identified board
- Boot reaches main loop with display init, button input handler, WiFi stack — no audio, no AI cloud connect
- Stub `application.cc` shows a "ready" splash so we know we have control

### Non-functional
- Final binary fits in single OTA slot (~3.5MB) with room for assets growth
- No removed-but-still-linked code (kill at CMake level, not `#if 0`)
- Keep upstream's directory layout (easier upstream rebases later)

## Architecture

### Components to KEEP
- `components/wifi_provisioning_manager/` (captive portal)
- `components/lvgl/` + LVGL init
- `components/board/` (display + button drivers for chosen board)
- ESP-IDF native: nvs_flash, esp_event, esp_netif, esp_wifi, esp_https_ota (unused but cheap)
- `main/protocols/http_protocol.*` (we'll repurpose as outbound HTTPS client)

### Components to REMOVE / NO_LINK
- `components/audio_codec/` (entire I2S audio path)
- `components/wakenet/` / `components/multinet/` (Espressif Skainet wake-word/ASR)
- `main/audio_processor.*`, `main/wake_word_detect.*`
- `main/mcp_server.*` (Xiaozhi's local MCP server)
- `main/protocols/mqtt_protocol.*`, `websocket_protocol.*` (cloud comm)
- `components/iot/` (Xiaozhi IoT thing-binding)

## Related Code Files
- Create: `firmware/` (top-level dir; fork checked in as a subtree or submodule)
- Modify: `firmware/main/CMakeLists.txt` (remove SRCS for audio/MCP/iot files)
- Modify: `firmware/main/Kconfig.projbuild` (hide audio + wakeword config groups)
- Modify: `firmware/main/application.cc` (gut to splash + idle loop)
- Modify: `firmware/main/main.cc` (remove audio init calls)
- Create: `firmware/CHANGES.md` (track our deviations from upstream for future merges)

## Implementation Steps

1. **Fork strategy** (pick one):
   - Option A (recommended): subtree at `firmware/` — easier to inspect with `git log`, but harder to pull upstream
   - Option B: git submodule pointing to our GitHub fork — cleaner upstream merge, ergonomically heavier
   - Decision: subtree for MVP. Migrate to submodule if upstream churn becomes painful.

2. **Initial import**:
   ```bash
   cd <project root>
   git subtree add --prefix=firmware https://github.com/78/xiaozhi-esp32.git v2.0.8 --squash
   ```

3. **Confirm baseline build**:
   ```bash
   cd firmware
   idf.py set-target esp32s3
   idf.py menuconfig   # select board from Phase 01
   idf.py build
   idf.py -p <esp32-serial-port> flash monitor
   ```
   Expect: boots into full Xiaozhi UI.

4. **Strip audio** (largest reduction):
   - Comment out `add_audio_codec()` style calls in `main/main.cc`
   - In `main/CMakeLists.txt`: remove `audio_processor.cc`, `wake_word_detect.cc` from `idf_component_register(SRCS …)`
   - In `idf_component.yml`: remove dependencies on `esp_audio_codec`, `esp-sr`, `esp-skainet`
   - Rebuild → expect smaller binary, no boot crash

5. **Strip cloud/MCP**:
   - Remove from CMake: `mcp_server.cc`, `protocols/mqtt_protocol.cc`, `protocols/websocket_protocol.cc`
   - Keep `protocols/http_protocol.cc` (we'll use as base for our HTTPS GET in Phase 06)
   - Remove `components/iot/`

6. **Gut application.cc**:
   ```cpp
   void Application::Start() {
       wifi_provisioning_init();   // captive portal if no creds
       display_->ShowSplash("Usage Monitor — ready");
       while (true) {
           vTaskDelay(pdMS_TO_TICKS(1000));
       }
   }
   ```

7. **Build size sanity check**: `idf.py size-components` — expect drop ~1-1.5MB after stripping audio.

8. **Flash and verify**:
   - Boot to "Usage Monitor — ready" splash
   - WiFi captive portal opens on first boot
   - After connect, no audio/AI activity in logs

9. **Document deviations** in `firmware/CHANGES.md`:
   - List every file removed + reason
   - List every CMake/Kconfig diff
   - This is the rebase aid for upstream syncs

## Todo List
- [ ] Subtree imported
- [ ] Baseline upstream build succeeds
- [ ] Audio stack removed at CMake level
- [ ] Cloud/MCP/IoT stripped
- [ ] application.cc gutted to splash
- [ ] Boots cleanly with WiFi + splash
- [ ] CHANGES.md captures every deviation

## Success Criteria
- [ ] Build succeeds, binary <3MB
- [ ] Boots to splash within 5s of power
- [ ] WiFi captive portal works on fresh NVS
- [ ] No errors in `idf.py monitor` related to removed components

## Risk Assessment
| Risk | Mitigation |
|---|---|
| Stripping audio breaks unrelated init (shared bus init) | Re-introduce file-by-file using bisect-style toggle in CMake |
| Display driver coupled to audio (rare but possible) | Identify coupling in Phase 01; if exists, leave audio codec dummy-init'd |
| Upstream subtree merge conflicts later | CHANGES.md + small diff surface mitigates; full re-fork is also acceptable for MVP |

## Security Considerations
- Wi-Fi creds stored in NVS — leave NVS encryption disabled for MVP (enabling is one-way; revisit in production hardening)
- HTTPS client (next phase) will use ESP-IDF cert bundle (DigiCert root for Cloudflare)

## Next Steps
→ Phase 06 (UI) builds on this skeleton.
