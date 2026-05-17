# Brainstorm Report — ESP32 Xiaozhi → Claude/Codex Usage Monitor

**Date:** 2026-05-17 10:45 GMT+7
**Owner:** project owner
**Status:** Design agreed, ready for `/ck:plan`

---

## 1. Problem Statement

Repurpose existing Xiaozhi ESP32 AI alarm-clock board (display + buttons + speaker/mic) into single-purpose desk gadget:
- 1 power button on back → wake from deep sleep
- Display: Claude + Codex usage (5h rolling block + weekly Max-plan limits)
- Style: cute pixel mascot, mood reflects current usage %
- Self-hosted, no cloud dependency on third parties (besides Anthropic/OpenAI themselves)

---

## 2. Reality Check (corrected user assumption)

User initially thought Anthropic/OpenAI expose usage APIs for subscription plans. **They don't:**

| Source | Public usage API for subscription? | Real path |
|---|---|---|
| Claude Code Max | ❌ No | Parse local `~/.claude/projects/*.jsonl` via `ccusage` npm tool |
| Codex CLI | ❌ No | Parse local `~/.codex/sessions/` (no standard tool) |
| Codex Cloud | ❌ No public API | Scrape authenticated dashboard `chatgpt.com/codex/cloud/settings/analytics` via headless browser |
| Anthropic Console API | ✅ Yes but irrelevant | Only tracks API-key spending, not Max plan limits |
| OpenAI Usage API | ✅ Yes but irrelevant | Only API-key billing, not ChatGPT subscription |

**Consequence**: need a *collector* running where the source data lives (laptop for Claude, Proxmox-hosted browser for Codex Cloud). Proxmox = aggregation + serving layer only.

---

## 3. Architecture (agreed)

```
┌─────────────────────────┐         ┌──────────────────────┐         ┌──────────────┐
│  Macbook / Workstation  │  POST   │   Proxmox LXC        │   GET   │  ESP32       │
│  ─────────────────────  │ ──────> │   usage-api          │ <────── │  Xiaozhi     │
│  claude-collector       │ token   │   ──────────────     │ secret  │  ──────────  │
│   • ccusage blocks      │         │   FastAPI /collect   │         │  ESP-IDF +   │
│   • POST every 60s      │         │   FastAPI /status    │         │  LVGL        │
│                         │         │   SQLite snapshot    │         │              │
│  codex-collector        │         │                      │         │  deep sleep  │
│   • Playwright headless │         │  fallback: cached    │         │  wake on btn │
│   • scrape Codex Cloud  │         │  data 5min stale     │         │  GET status  │
│   • POST every 5min     │         │                      │         │  render 4bar │
└─────────────────────────┘         └──────────────────────┘         └──────────────┘
```

**Why this split:**
- Claude collector on laptop because data IS local there
- Codex collector on Proxmox (Playwright in LXC) because session-cookie scraping should run 24/7 independently of laptop
- ESP32 only talks to LAN — no internet egress needed at device
- Single endpoint for ESP32 = simple firmware

---

## 4. Approaches Evaluated

### Approach A: All collectors on laptop, Proxmox is just relay
- **Pros**: Simple, no headless browser on Proxmox
- **Cons**: Laptop sleep = stale data; Codex scrape from laptop = ChatGPT logged-in conflict
- **Verdict**: ❌ Rejected — Codex scraper needs 24/7 isolated session

### Approach B: All collectors on Proxmox (mount laptop's ~/.claude via NFS)
- **Pros**: Single host
- **Cons**: NFS dependency, security headache, breaks when laptop off network
- **Verdict**: ❌ Rejected — fragile

### Approach C: Split collectors (CHOSEN)
- **Pros**: Each collector lives where data lives; clean separation; Proxmox aggregates
- **Cons**: 2 codebases to maintain
- **Verdict**: ✅ Chosen — best fit for the actual data topology

### Approach D: Direct ESP32 → ccusage via SSH
- **Pros**: No middle layer
- **Cons**: SSH from ESP32 = nightmare; laptop sleep = no data; coupling
- **Verdict**: ❌ Rejected

---

## 5. Component Specs

### 5.1 Claude Collector (laptop)
- Lang: Python 3.11+ (single file, ~80 LoC)
- Runtime: launchd (macOS) every 60s
- Cmd: `npx ccusage@latest blocks --json` → parse → POST
- Auth: bearer token in keychain
- Failure mode: log only, no retry storm

### 5.2 Codex Collector (Docker on <docker-host-ip>)
- Lang: Python + `httpx` (~40 LoC, no browser needed)
- **Endpoint discovered**: `GET https://chatgpt.com/backend-api/wham/usage`
- Returns JSON with exact shape we need:
  ```json
  {
    "rate_limit": {
      "primary_window":   {"used_percent": 1, "reset_at": 1779002657, "limit_window_seconds": 18000},
      "secondary_window": {"used_percent": 0, "reset_at": 1779571446, "limit_window_seconds": 604800}
    },
    "additional_rate_limits": [{"limit_name": "GPT-5.3-Codex-Spark", "rate_limit": {...}}],
    "credits": {"balance": "0"}
  }
  ```
- **Field mapping** (clean — no inversion needed, no time parsing):
  - `rate_limit.primary_window.used_percent` → `codex.current_pct`
  - `rate_limit.primary_window.reset_at` (unix ts) → `codex.current_resets_at`
  - `rate_limit.secondary_window.used_percent` → `codex.weekly_pct`
  - `rate_limit.secondary_window.reset_at` → `codex.weekly_resets_at`
- Auth (the only hard part):
  - `Authorization: Bearer <JWT>` — ~10 day TTL (extracted from browser)
  - Cookies: `__Secure-next-auth.session-token`, `cf_clearance`, `_puid`, `__cf_bm`, `__Host-next-auth.csrf-token`
  - Headers mimic browser: `oai-device-id`, `oai-client-version`, `user-agent`, etc.
- **Auth strategy** (chosen for MVP — option A "manual refresh"):
  - Extract cookies + JWT from browser via DevTools (or `browser_cookie3` lib) once
  - Store in Docker secret / env file (not committed)
  - On 401 response → mark status `auth_expired` → webhook alert (Pushover/Discord) → user re-extracts in ~1 min
  - Expected re-extraction cadence: weekly (cf_clearance rotates fastest)
  - **Future Phase B**: auto-refresh via `/api/auth/session` (cookie-based JWT refresh) + headless Playwright fallback for cf_clearance challenge
- Poll cadence: every 2-5min (cheap call, no rate-limit on this endpoint per docs/observation)
- MVP scope: consume `rate_limit.primary_window` + `secondary_window` only. Spark + Credits = Phase 7+
- **Risk** (much lower than scraping):
  - JWT expiry mid-day → handled by webhook + manual refresh
  - OpenAI deprecates `/wham/usage` endpoint → unlikely (UI depends on it), but track via daily smoke test

### 5.3 Usage API (Docker on <docker-host-ip>)
- Lang: FastAPI + SQLite (~150 LoC)
- Endpoints:
  - `POST /collect/{source}` — bearer auth (per-source token), body = snapshot JSON
  - `GET /status` — shared-secret header, returns aggregated JSON for ESP32
- Both collectors POST normalized `used_pct` (already inverted for Codex)
- Schema (all percentages are `used`, not `remaining`):
  ```json
  {
    "ts": "2026-05-17T10:45:00Z",
    "claude": {"current_pct": 50, "current_resets_at": "2026-05-17T12:07:00+07:00", "weekly_pct": 11, "weekly_resets_at": "2026-05-23T18:45:00+07:00", "status": "ok", "stale_sec": 30},
    "codex":  {"current_pct": 1,  "current_resets_at": "2026-05-17T14:24:00+07:00", "weekly_pct": 0,  "weekly_resets_at": "2026-05-21T00:00:00+07:00", "status": "ok", "stale_sec": 120}
  }
  ```
- Reset times stored as absolute ISO → API converts to `resets_in_sec` at response time for ESP32 simplicity
- **Exposure**: Cloudflare Tunnel (`cloudflared`) → `https://usage.<userdomain>.com`
  - Real TLS cert (no self-signed pain on ESP32)
  - ESP32 verifies cert via DigiCert root in ESP-IDF cert bundle
  - Cloudflare Access policy: only allow ESP32 device token + user's session (defense in depth beyond shared secret)
- Deploy: docker-compose with `usage-api` + `codex-collector` + `cloudflared` containers

### 5.4 ESP32 Firmware (Path A — Fork xiaozhi-esp32, chosen)
- Base: fork `github.com/78/xiaozhi-esp32` v2.0.8 (ESP-IDF v5.5, C++)
- **Reuse from upstream**:
  - `main/boards/<our-variant>/` — display driver, GPIO map, button, audio (we drop audio)
  - WiFi provisioning component (captive portal)
  - OTA infrastructure (keep but unused for MVP)
  - Deep sleep + LP-core button wake (if board supports)
  - LVGL integration + base widgets
- **Replace**:
  - `main/application.cc` → our usage-display state machine
  - `main/protocols/*` (cloud comms) → simple HTTPS client to our `/status` endpoint
  - Audio codec init → stubbed out (save flash + boot time)
- **Boot path**: deep sleep → button GPIO wake → restore RTC state → WiFi fast-connect (BSSID + channel cached in RTC mem) → HTTPS GET → render → 30s timer → deep sleep
- Cred storage: NVS encrypted partition
- **Power target**: deep-sleep <100 µA (LP core handles button → ULP wake source, main core full deep sleep)
- Display: assumed 240×280 or 240×320 IPS via SPI ST7789 (Phase 0 confirms by matching board config in xiaozhi-esp32 repo)
- Fallback Path B (from scratch ESP-IDF + LVGL) reserved if upstream codebase is unworkable

### 5.5 UI Layout (per user spec)
```
┌─────────────────────────────────┐
│ [🐰 Claude]            [📡][🔋] │  ← top half header
│ ▓▓▓▓▓░░░░░ 50%  Current  1h22m │
│ ▓▓░░░░░░░░ 11%  Weekly  6d 8h  │
├─────────────────────────────────┤
│ [🤖 Codex]                      │  ← bottom half header
│ ▓▓▓▓▓▓▓░░░ 70%  Current  2h10m │
│ ▓▓▓░░░░░░░ 30%  Weekly  4d 2h  │
└─────────────────────────────────┘
   mood: 😊  ·  refreshed 12s ago
```

**Mood states** (driven by max of current/weekly %):
- 0–50% → 😊 chill
- 50–75% → 😐 cruising
- 75–90% → 😰 careful
- 90–100% → 💀 cooked
- `stale_sec > 600` → ⚠️ offline indicator overlays

**Mascot icons**: 2 pixel-art sprites (16×16 or 24×24), Claude = orange creature (per user image), Codex = green/black robot. Stored as raw arrays in firmware.

---

## 6. Implementation Phases (proposed)

| Phase | Scope | Effort |
|---|---|---|
| **0. Hardware identify** | Match board to xiaozhi-esp32 `main/boards/*` (open device, read silkscreen, OR pinout probe). Set up ESP-IDF v5.5 toolchain. Build & flash stock xiaozhi to confirm pipeline works | 0.5d |
| **1. Usage-API skeleton** | FastAPI Docker on <docker-host-ip>, SQLite, `/collect/{source}` + `/status`, dummy data | 0.5d |
| **2. Claude collector** | Python + launchd on Mac, ccusage parsing, push to API | 0.5d |
| **3. Codex collector** | Python httpx → `/wham/usage`, cookie/JWT extract + storage + 401 webhook | 0.5-1d |
| **4. Firmware fork + strip** | Fork xiaozhi-esp32, identify minimum board+component subset, strip audio/AI/cloud, verify still boots | 1d |
| **5. ESP32 usage UI** | Replace `application.cc`: HTTPS GET, LVGL 4-bar layout, mood mascot, Claude+Codex sprites | 1.5d |
| **6. Deep sleep + button wake** | GPIO/LP-core wake source, 30s sleep timer, WiFi fast-reconnect, measure µA | 1d |
| **7. Hardening** | Cloudflare Tunnel exposure, per-source auth tokens, daily smoke test, README | 0.5d |
| **Total MVP (Claude only)** | Phase 0-2 + 4-7 (skip Codex) | ~5d |
| **Total full** | All phases | ~6-6.5d |

---

## 7. Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| ~~OpenAI changes Codex page~~ → API removed | LOW ⬇️ | Use stable JSON endpoint `/wham/usage` (UI itself depends on it); daily smoke test fires webhook if 404 |
| ChatGPT JWT/cookies expire | MED | Webhook on 401, user re-extracts in <1min; Phase B = auto-refresh via `/api/auth/session` |
| `cf_clearance` cookie rotates faster than expected | MED | Schedule webhook alert; if it becomes too frequent, escalate to Playwright keep-alive |
| `ccusage` format changes | MED | Pin version; schema validator in collector |
| Xiaozhi board parasitic always-on rail → bad battery | MED | Measure µA in Phase 0; may need hardware mod (cut trace) |
| WiFi reconnect slow → button → "loading" UX | LOW | Skeleton UI immediately, populate on fetch; ESP32 fast-connect (store BSSID/channel in RTC mem) |
| Display controller assumption wrong | LOW | Phase 0 confirms before writing UI code |

---

## 8. Success Criteria

- [ ] Press button → screen shows accurate Claude % within 3s
- [ ] Same for Codex (Phase 6+)
- [ ] Mood icon reflects state correctly across 4 ranges
- [ ] Battery lasts ≥30 days with 50 wakes/day
- [ ] WiFi loss → graceful "offline" UI, no crash
- [ ] Codex scraper auth expiry → user gets webhook alert
- [ ] No secrets in firmware binary (NVS encrypted)

---

## 9. Open Questions — Status

1. 🟡 **Xiaozhi hardware** — partially resolved via live USB probe:
   - ✅ Chip: ESP32-S3 N16R8 (16MB flash, 8MB PSRAM, native USB-JTAG, dual core + LP core, 240MHz)
   - ✅ MAC: <device-mac>, Boya 16MB flash
   - ✅ Currently running: official `xiaozhi-esp32` v2.0.8 (github.com/78/xiaozhi-esp32, ESP-IDF v5.5)
   - ✅ Partition layout: standard Xiaozhi (dual 4MB OTA + 8MB assets)
   - ⏳ Still TBD: exact board variant name (match against xiaozhi-esp32 `main/boards/*`) — needs PCB silkscreen check or pinout probe in Phase 0
   - ⏳ Still TBD: actual display resolution + controller (NOT 1024×768 per Alibaba spec — marketing fiction)
   - ⏳ Still TBD: deep-sleep current measurement (need µA meter or USB power meter)
2. ✅ **Codex Cloud selectors** — DOM scrape path verified via user-supplied DevTools screenshots. See §5.2 for selectors. Tailwind class names may shift; pin known-good and add fallback by label text
3. ✅ **Network** — HTTPS via Cloudflare Tunnel (`cloudflared`) → `https://usage.<userdomain>.com`. Docker host `<docker-host-ip>`
4. ✅ **OTA** — Not needed, USB-C re-flash acceptable for personal device. Saves ~1d work + binary size

## 9.1 New things learned mid-brainstorm

- **Codex Cloud has a clean JSON API**: `GET /backend-api/wham/usage` returns `used_percent` + `reset_at` (unix ts) directly. Kills the entire Playwright DOM-scrape approach (saved ~2-3 days)
- Codex Cloud actually has 5 metrics (overall 5h, overall week, Spark 5h, Spark week, Credits). MVP only consumes overall 5h + weekly. Spark + Credits = future scope
- API uses 3-layer auth: Bearer JWT (10d) + session cookies (longer) + `cf_clearance` (hours-days). `cf_clearance` rotation is the main operational pain → MVP accepts manual weekly refresh, with webhook alerts
- ChatGPT plan type leaks via JWT (`chatgpt_plan_type` claim) — useful for future feature (different limits for Pro/Plus/Prolite)

---

## 10. Next Step

→ Run `/ck:plan` to produce phased implementation plan with phase-XX-*.md files using this report as the source of truth.
