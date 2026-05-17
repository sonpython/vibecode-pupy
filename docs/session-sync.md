# Session Sync (Claude side)

**Latest export**: 2026-05-17 11:40 SGT | **From**: claude-code-opus-4.7 | **Branch**: (no git yet)
**Format**: Latest entry on TOP. Append new sessions via prepend.

---

## SESSION 2026-05-17 10:45-11:40 SGT — brainstorm + plan ESP32 Xiaozhi usage monitor

### 🎯 Trigger
User wanted to repurpose existing Xiaozhi ESP32-S3 robot (alarm-clock form factor, display + mic + speaker, runs xiaozhi-esp32 v2.0.8 firmware) into single-purpose device that shows Claude Code + Codex Cloud usage on button press. Reference image like ccusage-style 5h+weekly bars.

### ✅ Done
1. `/brainstorm` session: corrected user misconception (no public usage API for Claude/Codex subscriptions); proposed 3-layer architecture (collectors → API → ESP32)
2. Live USB hardware probe via esptool: confirmed ESP32-S3 N16R8, 16MB flash + 8MB PSRAM, native USB-JTAG, MAC `a0:f2:62:e8:a4:40`, currently running xiaozhi-esp32 v2.0.8 (ESP-IDF v5.5)
3. User found Codex Cloud JSON API endpoint `GET /backend-api/wham/usage` — killed need for Playwright scraping (-2d effort)
4. Wrote brainstorm report `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md`
5. `/ck:plan` workflow: created plan dir `plans/260517-1045-esp32-usage-monitor/` with `plan.md` + 8 phase files (Phase 01-08, ~6.5d total)
6. Full 16MB flash backup at `~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin` (SHA256 3a6a8a1f8a3a46be3993cd46ca3d0371c4003b953257995be0b2de789f6b583f)
7. `esptool erase-flash` to silence device (was speaking Chinese loudly)
8. `memory init` — created docs/ from skill templates

### 📁 Files changed
- `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md` — NEW (15KB, full brainstorm summary)
- `plans/260517-1045-esp32-usage-monitor/plan.md` — NEW (overview)
- `plans/260517-1045-esp32-usage-monitor/phase-{01..08}-*.md` — NEW (8 phase files, ~5-7KB each)
- `docs/{session-sync,codex-memory,app-journey-story,codex-memory-protocol,codex-starter-prompt}.md` — NEW (memory-bridge init)
- `~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin` — NEW (16MiB flash dump)

### 🔑 Key decisions
- Firmware approach: **fork xiaozhi-esp32 v2.0.8** (strip audio/AI/cloud, keep WiFi+display+button), NOT from-scratch ESP-IDF. ~70% code reuse.
- Codex data source: **`/backend-api/wham/usage` JSON API** (Bearer JWT + cookies), NOT Playwright. Manual weekly auth refresh + webhook on 401.
- Claude data source: `ccusage` npm tool parsing local `~/.claude/projects/*.jsonl` (no public API exists).
- Architecture split: Claude collector on Mac (launchd), Codex collector on Docker @ 192.168.1.120, Usage-API on same Docker, exposed via Cloudflare Tunnel HTTPS.
- ESP32: ESP-IDF v5.5 + LVGL, deep sleep + GPIO wake, no OTA (USB-C re-flash acceptable for personal device).
- UI: 4 progress bars (Claude 5h+week, Codex 5h+week) + mood mascot + 2 character sprites.

### 📊 State changes
- esp32-usage-monitor: nonexistent → planned (8 phases, ready for Phase 01 implementation)
- Xiaozhi device: factory firmware running → flash erased (silent), factory backup preserved

### 🚨 Follow-ups (cho session sau)
- [ ] Phase 01: identify exact xiaozhi-esp32 board variant (match against `main/boards/*`). Try `idf.py monitor` first; if board name not in serial boot log → open PCB and read silkscreen
- [ ] Phase 01: install ESP-IDF v5.5 (`~/esp/esp-idf`)
- [ ] Phase 01: measure deep-sleep current (need USB power meter, target <100µA)
- [ ] Whenever ready: re-flash backup OR build stripped xiaozhi-esp32 fork to bring device back to life
- [ ] Consider `git init` on project (current: not a git repo; memory-bridge skipped git ops)
- [ ] Phase 04: document Codex auth refresh procedure (cf_clearance rotates fastest)

### 💡 Lessons learned
- Always probe hardware live via USB before trusting Alibaba spec sheets — JQRNZ listed "1024×768 AMOLED" but real is ESP32-S3 with ~240×280 IPS LCD (marketing fiction)
- Even when a vendor dashboard looks like it requires DOM scraping, check Network tab — backend JSON APIs are usually available with browser cookies + bearer
- `ck plan create` does NOT exist in ck CLI v3.35.0 despite the ck-plan skill template referencing it — fall back to manual file creation when CLI subcommand missing

---

<!-- Prepend new sessions ABOVE this line -->

## SESSION YYYY-MM-DD <slot> — <slug-mô-tả>

### 🎯 Trigger
<User request hoặc context>

### ✅ Done
1. <action + outcome>
2. <action + outcome>

### 📁 Files changed
- `path` — <note>

### 🔑 Key decisions
- <decision + rationale>

### 📊 State changes
- <Project X: before → after>

### 🚨 Follow-ups (cho session sau)
- [ ] <task, owner, deadline>

### 💡 Lessons learned
- <note>

---

<!-- Prepend new sessions ABOVE this line -->
