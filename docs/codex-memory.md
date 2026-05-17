# Codex Memory Log

> **Purpose**: Append-only timeline of work done by any AI agent. When user returns to a different agent, that agent reads this file to catchup.
> **Format**: Latest entry on TOP. Each entry = 1 session/task. Atomic, self-contained.
> **Rule**: KHÔNG sửa entry cũ. Chỉ prepend entry mới ở phần "Entries" bên dưới.
> **Git**: file này committed + pushed. Sync giữa các môi trường (local / remote / cloud agents).

---

## Actor namespace (SOT)

Field `Actor` PHẢI chọn 1 trong:

| Actor tag | Use case |
|---|---|
| `claude-code-{model}` | Claude Code (vd `claude-code-opus-4.7`) |
| `codex-cli` | Codex CLI |
| `codex-web` | ChatGPT codex.openai.com |
| `opencode` | OpenCode CLI |
| `chatgpt-web` | ChatGPT thường |
| `routine-claude` | claude.ai routines |
| `routine-codex` | Codex routine |
| `cron-{tool}` | OS cron |
| `agent-{name}` | Subagent spawn |
| `webhook-{source}` | External trigger |

---

## Entry format

### Full template (session work với decision/state change)

```markdown
## YYYY-MM-DD HH:mm SGT — <slug-mô-tả-ngắn>

**Actor**: <từ namespace trên>
**Branch**: <git-branch>
**Trigger**: <user request hoặc context>

### ✅ Done
- <action 1 + outcome>

### 📁 Files changed
- `path/to/file1.ext` — <NEW | UPDATED | DELETED — note>

### 🔑 Key decisions
- <decision + rationale, hoặc xoá nếu không có>

### 📊 State changes (active projects)
- <Project X: before → after, hoặc xoá nếu không có>

### 🚨 Follow-ups (cho session sau)
- [ ] <task, owner, deadline>

### ⚠ Blockers / open questions
- <issue, hoặc xoá nếu không có>

### 💡 Lessons learned
- <note, hoặc xoá nếu không có>
```

### Brief template (recurring jobs / routine / cron)

```markdown
## YYYY-MM-DD HH:mm SGT — <slug> [routine]
**Actor**: routine-claude | **Trigger**: <cron ID>
**Outcome**: ✓/⚠/✗ <1-line result>
**Files**: <changed files hoặc "none">
**Next**: <next run date hoặc "—">
```

---

## Anti-patterns

- ❌ Edit entry cũ — chỉ prepend mới
- ❌ Log secrets/credentials/PII
- ❌ Quên `git push`
- ❌ Tạo memory atomic files trong `docs/` (Claude-private go in `~/.claude/projects/*/memory/`)

---

## Entries

> **NEWEST ON TOP**. Prepend, never append at bottom.

<!-- ENTRY MARKER — agents prepend here -->

## 2026-05-17 15:54 SGT — firmware-brand-icon-color-fix

**Actor**: codex-cli
**Branch**: main
**Trigger**: User reported the brand logos/colors were wrong: Claude Code should be orange crab-style, Codex should be black/white OpenAI-style.

**What changed**
- Replaced the Claude icon bitmap with a compact 32x32 orange crab-style mark for the tiny LVGL screen.
- Recolored the Codex/OpenAI bitmap to white while preserving its alpha mask, so it reads as black/white on the dark interface instead of cyan.

**Validation**
- Rebuilt firmware with ESP-IDF v5.5 successfully.
- Flashed firmware to `/dev/cu.usbmodem83101`; esptool verified hashes and hard reset completed.
- Serial monitor confirmed new app booted with compile time `May 17 2026 15:49:04`, connected to Wi-Fi at `192.168.1.35`, fetched status with HTTP 200, and continued reporting power state.

**Files changed**
- `firmware/usage-monitor/main/brand_icons.c` — UPDATED, Claude and Codex/OpenAI icon pixel maps.

**Next**
- Visually confirm on the physical LCD that the icon silhouettes match expectations; firmware and flash validation are complete.

## 2026-05-17 15:47 SGT — firmware-battery-percent-display-fix

**Actor**: codex-cli
**Branch**: main
**Trigger**: User reported the remaining issue: battery percent shows `--`/unknown instead of a percent.

**What changed**
- Fixed Spotpear MUMA power pin mapping in usage firmware:
  - Battery ADC now uses factory board channel `ADC_CHANNEL_0`.
  - Charge detect now uses factory board pin `GPIO41`.
- Removed the incorrect `USB_ADC_CHANNEL` read that was actually competing with the board's battery ADC channel.
- Battery UI now includes `%` for real battery readings.
- When no valid battery ADC is present but USB/charge is detected, top-right power indicator shows charge icon with `100%` instead of `--`.

**Validation**
- Serial monitor before fix showed `battery_raw=362 battery_pct=-1 usb_raw=148 charge_gpio=0 charging=1`, so UI rendered charge unknown.
- Rebuilt and flashed firmware. Esptool wrote app image to 100%; reset step reported a transient serial reconfigure error, but subsequent monitor confirmed new firmware booted.
- Monitor after fix showed app compile time `May 17 2026 15:44:24`, HTTP 200, and `power battery_raw=27 battery_pct=-1 charge_gpio=0 charging=1`; UI fallback now renders charge `100%`.

**Files changed**
- `firmware/usage-monitor/main/main.c` — UPDATED, power pin mapping and battery display formatting.

**Next**
- If a LiPo battery is actually connected and still reads invalid raw ADC, inspect the JST battery connection or board revision; firmware now matches the upstream Spotpear MUMA mapping.

## 2026-05-17 15:39 SGT — claude-web-usage-api-collector

**Actor**: codex-cli
**Branch**: main
**Trigger**: User provided claude.ai `/api/organizations/.../usage` cURL and clarified Claude should mirror Codex: direct web API + Playwright session keeper.

**What changed**
- Replaced Claude `ccusage` collector with an authenticated Claude web usage API collector.
- Added `collectors/claude/parse_claude_curl.py` to convert browser cURL from `https://claude.ai/settings/usage` into ignored `secrets/claude_auth.json`.
- Added `claude-session-keeper` Playwright service to keep claude.ai session warm and smoke-test the usage endpoint.
- Updated Docker Compose: `claude-collector` reads `/secrets/claude_auth.json`; no longer mounts `/root/.claude`.
- Deployed to Docker host `192.168.1.120`.

**Validation**
- `pytest -q collectors/claude collectors/codex usage-api/tests` -> `10 passed`
- Local Claude web usage fetch -> HTTP 200, current/weekly percentages parsed.
- Host `claude-session-keeper` log -> `page_status=200 usage_status=200`
- `https://vibecode.sonpython.com/public/status` -> Claude `ok`, current `3`, weekly `5` at deploy time.

**Files changed**
- `collectors/claude/claude_collector.py` — UPDATED, Claude web usage API collector.
- `collectors/claude/parse_claude_curl.py` — NEW, auth parser for claude.ai usage cURL.
- `collectors/claude/session_keeper.py` — NEW, Playwright session warmer.
- `collectors/claude/Dockerfile` — UPDATED, pure Python image.
- `collectors/claude/Dockerfile.playwright` — NEW, Playwright image.
- `collectors/claude/test_claude_collector.py` — UPDATED, tests for web usage API payload + parser.
- `usage-api/docker-compose.yml` — UPDATED, adds `claude-session-keeper` and secret-mounted Claude collector.
- `README.md` — UPDATED, Claude auth refresh docs.

**Decisions**
- Use Claude's web app usage endpoint because it directly returns `five_hour.utilization` and `seven_day.utilization`.
- Treat this as an internal web endpoint like Codex `/wham/usage`; if auth expires, collector posts `auth_expired`.
- Keep `secrets/claude_auth.json` ignored and never commit claude.ai cookies/session keys.

**Next**
- If Claude shows `auth_expired`, refresh from browser: copy usage endpoint cURL and run `pbpaste | python3 collectors/claude/parse_claude_curl.py > secrets/claude_auth.json`, then redeploy/copy secret to host.
- Consider adding a shared auth-refresh README for both Codex and Claude.

## 2026-05-17 14:51 SGT — docker-host-deploy-and-codex-playwright-keeper

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked whether deployment was actually on Docker host `192.168.1.120`, then reminded to add Playwright to keep the Codex session alive.

### ✅ Done
- Confirmed previous live stack was still local Docker, not the `192.168.1.120` host.
- Connected to `root@192.168.1.120`, copied the project runtime into `/opt/vibecode-pupy`, and deployed the usage stack there.
- Started host services: `usage-api`, `codex-collector`, `claude-collector`, `cloudflared`, and new `codex-session-keeper`.
- Copied Claude usage project data to `/root/.claude/projects` on the host so host `claude-collector` can post fresh snapshots.
- Stopped the local Docker Compose stack so `vibecode.sonpython.com` is served from the Docker host tunnel.
- Added a Playwright-based Codex session keeper that imports cookies/headers from `secrets/codex_auth.json`, opens Codex analytics, and calls `wham/usage` every 15 minutes.
- Verified on host: Playwright keepalive logs `page_status=200 usage_status=200`; public API reports Claude/Codex `ok`.
- Tweaked firmware UI: current usage percent now sits on the same row as the model name, aligned near the end of the current usage bar; current bar height increased from 10px to 14px and firmware was flashed.

### 📁 Files changed
- `collectors/codex/Dockerfile.playwright` — NEW, Playwright session keeper image.
- `collectors/codex/session_keeper.py` — NEW, Codex browser/API keepalive loop.
- `usage-api/docker-compose.yml` — UPDATED, added `codex-session-keeper` service.
- `firmware/usage-monitor/main/main.c` — UPDATED, current percent positioning and thicker current usage bar.
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry.

### 🔑 Key decisions
- Keep direct `codex-collector` as the data source and add Playwright as a session warmer instead of replacing the collector with browser scraping.
- Use the existing Cloudflare tunnel token on the Docker host and stop local tunnel connectors to avoid split serving.

## 2026-05-17 14:42 SGT — pupy-screen-redesign

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked to redesign the robot screen, reduce weekly usage to a small circular chart, move battery to top-right icon form, show reset remaining time, show live/fetch status at bottom, show next-fetch countdown at bottom-right, and rename the title to `VIBECODE PUPY`.

### ✅ Done
- Redesigned ESP32 LVGL screen layout:
  - Top-left title changed to `VIBECODE PUPY`.
  - Top-right battery now uses LVGL battery/charge symbols with compact percentage/unknown state.
  - Each source row shows icon, current percent, current usage bar, and reset remaining text like `RESET 4h 12m  19:00`.
  - Weekly usage is now a small circular LVGL arc with percent label.
  - Bottom-left shows `LIVE` / `FETCH` / `ERROR` plus status symbol.
  - Bottom-right shows `NEXT mm:ss` countdown to the next automatic fetch.
- Disabled the temporary Cloudflare/ttyd remote-control sessions and removed local runtime credential/log files after user clarified they wanted OpenAI native remote control instead.
- Rebuilt and flashed firmware; monitor verified boot, display init, Wi-Fi connect, HTTP 200 fetch, and power status log.

### 📁 Files changed
- `firmware/usage-monitor/main/main.c` — UPDATED, full screen layout redesign and next-fetch countdown.
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry.

### 🔑 Key decisions
- Used only LVGL fonts already enabled in the firmware (`12/14/18/20`) to avoid increasing binary/config churn.
- Used text `NEXT mm:ss` for the lower-right countdown because the bundled LVGL symbol set does not include an hourglass glyph.

## 2026-05-17 14:31 SGT — reset-time-and-manual-refresh-firmware

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked to fix unplugged battery display, replace the loading icon, detect the top button for manual fetch, and add GMT+7 reset time for the 4-5h quota window.

### ✅ Done
- Added `current_resets_at_gmt7` to `usage-api` status output and verified public API returns local reset times such as Claude `19:00` and Codex `19:24`.
- Updated web UI metadata to show only the 4-5h/current window reset time in GMT+7.
- Updated ESP32 firmware to parse and display current reset time as `RHH:MM` per source row.
- Fixed invalid battery ADC readings so disconnected/invalid battery no longer renders as `BAT 0%`; it now renders `BAT --` or `CHG --`.
- Replaced the persistent LVGL spinner with a fetch status label using LVGL symbols: refresh while fetching, check on success, close on error.
- Added manual-refresh edge detection across GPIO5, GPIO0, GPIO47, and GPIO48 to discover the real top button without driving unknown pins.
- Rebuilt and flashed firmware; monitor verified button-watch logs, Wi-Fi connect, HTTP 200, expanded JSON payload, and `battery_pct=-1` for invalid raw battery input.

### 📁 Files changed
- `.gitignore` — UPDATED, ignore local `.tmp/` remote-control runtime files.
- `usage-api/app.py` — UPDATED, compute GMT+7 current reset display string.
- `usage-api/schemas.py` — UPDATED, expose `current_resets_at_gmt7`.
- `usage-api/static/app.js` — UPDATED, show 4-5h reset time in the web dashboard.
- `usage-api/tests/test_app.py` — UPDATED, assert reset display field.
- `firmware/usage-monitor/main/main.c` — UPDATED, reset-time UI, battery invalid handling, fetch icon state, and multi-GPIO manual refresh detection.
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry.

### 🔑 Key decisions
- Let the API compute GMT+7 reset time because the ESP32 firmware does not maintain reliable wall-clock time.
- Monitor multiple likely button GPIOs for a stable edge first; serial logs identify the real button before narrowing the firmware to one pin later.

### 🚨 Follow-ups
- [ ] Ask the user to press the physical top button while monitor is open in a later session, then keep only the GPIO that logs `manual refresh button=...`.

## 2026-05-17 14:16 SGT — battery-charge-and-claude-collector

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked to show battery percent/charging status on the robot screen and fix missing Claude Code usage.

### ✅ Done
- Added a Dockerized `claude-collector` service that runs `ccusage` continuously with Node 22 and posts snapshots to `usage-api`.
- Rebuilt and restarted the live compose stack; verified `claude.status = ok`, `codex.status = ok`, and both collectors post to `/collect/*`.
- Added ESP32 power footer showing `BAT xx%` or `CHG xx%`.
- Added battery ADC reading from `ADC_CHANNEL_6` / GPIO7, using the same raw threshold family found in the factory firmware lineage.
- Added charging detection fallbacks from USB ADC, charge GPIO, and USB-Serial/JTAG host connection.
- Rebuilt and flashed firmware; verified serial boot, Wi-Fi, HTTPS status fetch, and power log with `charging=1` while connected over USB.

### 📁 Files changed
- `collectors/claude/Dockerfile` — NEW, Node 22 + Python runtime for ccusage collector.
- `collectors/claude/claude_collector.py` — UPDATED, run continuously instead of one-shot.
- `usage-api/docker-compose.yml` — UPDATED, added `claude-collector` service.
- `firmware/usage-monitor/main/main.c` — UPDATED, battery/charging state readout and LVGL footer.
- `firmware/usage-monitor/main/CMakeLists.txt` — UPDATED, ADC and USB-Serial/JTAG component deps.
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry.

### 🔑 Key decisions
- Run Claude usage collection in Docker rather than launchd so the live stack owns both Codex and Claude collectors.
- Use USB-Serial/JTAG connection as a pragmatic charging indicator for the current cabled device because the tested board pins did not expose USB-in during monitor tests.

### ⚠ Blockers / open questions
- Battery ADC currently logs very low raw values around 370-400, so display shows 0%. This likely means the LiPo cell is not connected or the board revision gates battery voltage differently.

## 2026-05-17 13:32 SGT — wonderboy-display-gpio-map-from-factory

**Actor**: codex-cli
**Branch**: main
**Trigger**: User reported the robot screen was still black after earlier firmware flashes; factory firmware was confirmed to display correctly.

### ✅ Done
- Restored the saved factory image temporarily and monitored boot logs; factory app identified as `xiaozhi` 2.0.8 / `WonderBoy-AI-Buddy`.
- Used USB-JTAG/OpenOCD while factory firmware was running to read ESP32-S3 GPIO matrix registers and derive the real display pin map.
- Updated usage-monitor firmware to use factory-derived pins:
  - ST7789 SPI3 SCLK `GPIO9`, MOSI `GPIO10`, CS `GPIO14`
  - LCD DC `GPIO8`, reset `GPIO18`
  - Backlight `GPIO13`
- Rebuilt and flashed the usage monitor firmware back onto the device.
- Verified serial boot: display init completes, WiFi connects to LAN, and usage API returns HTTP 200.
- Re-read GPIO registers under the monitor firmware to confirm display pins are configured on the expected GPIOs.

### 📁 Files changed
- `firmware/usage-monitor/main/main.c` — UPDATED, replaced guessed display/backlight pins with factory-derived WonderBoy map.
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry.

### 🔑 Key decisions
- Trust factory runtime GPIO matrix over upstream board guesses. Earlier candidates (`GPIO0/1/2/46`, `GPIO16`, `GPIO3`) allowed firmware/network to run but did not match the actual screen wiring.
- Keep digital backlight enable on `GPIO13` for now. Factory uses LEDC PWM on `GPIO13`; a static high level is enough for full-bright monitor mode.

### 📊 State changes (active projects)
- ESP32 monitor firmware: black screen with guessed pins → flashed with factory-derived WonderBoy display map.
- Factory backup: still preserved at `~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin`.

### 🚨 Follow-ups
- [ ] Visual confirmation still needs the human looking at the physical screen. If it is lit but wrong/blank, first swap LCD `DC`/`RST` between `GPIO8` and `GPIO18`; all other display pins are now evidence-based.

### ⚠ Blockers / open questions
- Codex cannot directly see the physical LCD, so visual success must be confirmed by user.

## 2026-05-17 13:03 SGT — usage-api-web-live-deploy

**Actor**: codex-cli
**Branch**: main
**Trigger**: Continue after root monitor UI patch; user wanted `https://vibecode.sonpython.com/` to show a robot-like screen.

### ✅ Done
- Rebuilt and restarted the live `usage-api` Docker service behind the existing Cloudflare tunnel.
- Verified `https://vibecode.sonpython.com/` serves the monitor HTML and `https://vibecode.sonpython.com/public/status` returns live Claude/Codex JSON.
- Fixed mobile horizontal overflow found via headless Chrome screenshots by tightening hero typography and stacking source badges on mobile.
- Added cache-busting query versions to static asset links so Cloudflare/browser caches pick up the updated CSS/JS.
- Re-tested API with `pytest -q usage-api/tests` after deploy.

### 📁 Files changed
- `usage-api/static/index.html` — UPDATED, versioned CSS/JS asset URLs
- `usage-api/static/styles.css` — UPDATED, mobile overflow fix
- `docs/codex-memory.md` — UPDATED, prepended this live deploy note

### 🔑 Key decisions
- Leave authenticated `/status` unchanged for ESP32 clients; expose browser data through `/public/status` only.

### 📊 State changes (active projects)
- Public domain root: API-only/blank -> live robot-style usage dashboard.

## 2026-05-17 12:57 SGT — usage-api-root-monitor-ui

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked for a robot usage monitor web screen at `https://vibecode.sonpython.com/`

### ✅ Done
- Added a root web UI for `usage-api` that resembles the ESP32 usage monitor: robot face, Claude/Codex current + weekly usage bars, source status badges, stale age, and live/error state.
- Added `/public/status` as a read-only UI status endpoint so browser code can poll aggregate usage data without exposing `DEVICE_SECRET`.
- Kept existing authenticated `/status` endpoint intact for ESP32/device clients.
- Updated Dockerfile to include static assets in the runtime image.
- Added tests for root UI serving and public status access.
- Verified with `pytest -q usage-api/tests`, `python3 -m py_compile usage-api/app.py usage-api/schemas.py`, Docker image build, and local Docker HTTP checks on `127.0.0.1:18080`.

### 📁 Files changed
- `usage-api/app.py` — UPDATED, static mount, root route, shared status builder, public UI status endpoint
- `usage-api/Dockerfile` — UPDATED, copy static assets
- `usage-api/tests/test_app.py` — UPDATED, root/public status tests
- `usage-api/static/index.html` — NEW, monitor UI markup
- `usage-api/static/styles.css` — NEW, responsive robot/dashboard styling
- `usage-api/static/app.js` — NEW, polling/rendering logic
- `docs/codex-memory.md` — UPDATED, prepended this entry

### 🔑 Key decisions
- Use a server-side public status proxy instead of injecting `DEVICE_SECRET` into browser JavaScript.
- Keep this scoped to `usage-api` frontend/static assets and route serving; firmware was not touched.

### 📊 State changes (active projects)
- Usage API web root: health/API only -> responsive usage monitor screen.

### 🚨 Follow-ups
- [ ] Parent can rebuild/redeploy the live `usage-api` service behind Cloudflare when ready.

## 2026-05-17 12:45 SGT — esp32-usage-monitor-firmware-live

**Actor**: codex-cli
**Branch**: main
**Trigger**: User provided WiFi credentials for ESP32

### ✅ Done
- Saved WiFi credentials into ignored local `firmware/secrets/wifi.env`.
- Created minimal ESP-IDF firmware at `firmware/usage-monitor` for the confirmed `sp-esp32-s3-1.54-muma` board.
- Firmware initializes ST7789 display pins, backlight, WiFi STA, HTTPS client with ESP x509 certificate bundle, and direct JSON parsing for `https://vibecode.sonpython.com/status`.
- `prepare_secrets.sh` generates ignored `main/secrets.h` from local WiFi env + `usage-api/.env` device secret.
- Built firmware with ESP-IDF v5.5 and custom 4MB factory partition.
- Flashed firmware to `/dev/cu.usbmodem83101`.
- Serial verification:
  - WiFi connected to `mp`.
  - Device got IP `192.168.1.35`.
  - Cloudflare certificate validated.
  - `GET /status` returned HTTP 200 with 301-byte payload.
  - Device stayed running after fetch; no reboot after stack-size fix.

### 📁 Files changed
- `firmware/usage-monitor/*` — NEW, minimal ESP-IDF firmware
- `.gitignore` — UPDATED, ignore local firmware secrets/build/sdkconfig outputs
- `docs/codex-memory.md` — UPDATED, prepended this entry
- `firmware/secrets/wifi.env` — LOCAL IGNORED, WiFi credentials
- `firmware/usage-monitor/main/secrets.h` — LOCAL IGNORED, generated WiFi/API secret header

### 🔑 Key decisions
- Use a clean ESP-IDF firmware rather than stripping the full Xiaozhi app in-place. This avoids audio/cloud dependencies and got a working HTTPS usage display faster.
- Use hardcoded local secrets for this MVP; provisioning AP can come later if needed.
- Increase `CONFIG_ESP_MAIN_TASK_STACK_SIZE` to 12288 because ESP HTTP client overflowed the default main stack during TLS setup.

### 📊 State changes (active projects)
- Firmware Phase 05/06: planned -> MVP flashed and live.

### 🚨 Follow-ups
- [ ] Confirm visually that the rendered bars/text orientation looks right on the physical display.
- [ ] Add deep sleep + GPIO0 wake now that fetch/display loop works.
- [ ] Add battery ADC display if desired.
- [ ] Consider moving HTTPS/fetch work to its own task and adding backoff/offline UI polish.

## 2026-05-17 12:32 SGT — public-domain-and-production-token-rotation

**Actor**: codex-cli
**Branch**: main
**Trigger**: User confirmed Cloudflare hostname `https://vibecode.sonpython.com/`

### ✅ Done
- Verified public Cloudflare route:
  - `GET https://vibecode.sonpython.com/healthz` -> HTTP 200
  - `GET https://vibecode.sonpython.com/status` with the current device secret -> HTTP 200
- Rotated local API secrets from dev values to random production-style values in ignored `usage-api/.env`.
- Recreated Docker services with the new secrets: `usage-api`, `codex-collector`, `cloudflared`.
- Refreshed Claude collector once using the new local collector token.
- Verified old dev `X-Device-Secret: dev-device-secret` now returns HTTP 401.
- Verified current public status reports `claude.status=ok` and `codex.status=ok`.

### 📁 Files changed
- `usage-api/.env` — LOCAL IGNORED, production-style random tokens set
- `docs/codex-memory.md` — UPDATED, prepended this entry

### 🔑 Key decisions
- Keep the production device secret out of chat and git; firmware generation should read it from local ignored config.
- Use `https://vibecode.sonpython.com/status` as the ESP32 production API endpoint.

### 📊 State changes (active projects)
- Public usage API: domain unverified -> live via Cloudflare.
- API auth: dev token accepted -> dev token rejected, production local secret required.

### 🚨 Follow-ups
- [ ] Firmware Phase 05/06 needs WiFi credentials for the ESP32 or an onboard provisioning flow before it can fetch the public API autonomously.
- [ ] Rotate Cloudflare tunnel token and ChatGPT/Codex browser auth later because both were pasted into chat.

## 2026-05-17 12:25 SGT — cloudflare-tunnel-and-codex-auth-live

**Actor**: codex-cli
**Branch**: main
**Trigger**: User provided Cloudflare tunnel token and ChatGPT/Codex `wham/usage` cURL auth material

### ✅ Done
- Saved Cloudflare tunnel token into ignored local `usage-api/.env` and started `cloudflared` with Docker Compose profile `tunnel`.
- Verified tunnel container registered successfully with Cloudflare for tunnel ID `95c8a269-fcf8-4459-97ec-6d0b5e392571`.
- Updated Codex cURL parser to support Chrome's `curl -b '<cookie jar>'` form in addition to `-H 'Cookie: ...'`.
- Parsed current browser auth from clipboard into ignored local `secrets/codex_auth.json`, restarted `codex-collector`, and verified `/status` now reports `codex.status=ok`.
- Ran focused tests: `pytest -q collectors/codex/test_codex_collector.py` -> `3 passed`.

### 📁 Files changed
- `collectors/codex/parse_curl.py` — UPDATED, support `-b/--cookie` cURL cookie input
- `collectors/codex/test_codex_collector.py` — UPDATED, regression test for `-b` parser path
- `usage-api/.env` — LOCAL IGNORED, tunnel token configured
- `secrets/codex_auth.json` — LOCAL IGNORED, current Codex browser auth configured
- `docs/codex-memory.md` — UPDATED, prepended this entry

### 🔑 Key decisions
- Do not commit any Cloudflare token, bearer token, session cookie, or auth JSON.
- Keep the collector running locally now that the real auth is valid.

### 📊 State changes (active projects)
- Codex collector: `auth_expired` -> `ok`.
- Cloudflare tunnel: scaffolded -> running locally.

### 🚨 Follow-ups
- [ ] Rotate Cloudflare tunnel token and ChatGPT/Codex browser session later because both were pasted into chat.
- [ ] Replace dev `DEVICE_SECRET` and collector bearer tokens with production random values before wiring ESP32/public endpoint.

## 2026-05-17 12:15 SGT — usage-monitor-local-stack-and-board-baseline

**Actor**: codex-cli
**Branch**: main
**Trigger**: User said they were going to sleep and asked Codex to follow the plan sequentially, test carefully, self-decide, with Docker running and ESP32 plugged in

### ✅ Done
- Implemented the local usage-monitor stack: FastAPI + SQLite API, Docker Compose, Claude collector, Codex collector, auth-refresh helper, Cloudflare tunnel scaffold, launchd/smoke-test templates, and top-level README.
- Verified API/collector tests: `pytest -q usage-api/tests collectors/claude collectors/codex` -> `6 passed`.
- Brought up Docker services: `usage-api` on `127.0.0.1:8080` and `codex-collector` container.
- Posted a real Claude snapshot through `npx ccusage@latest` via `collectors/claude/claude_collector.py`; `/status` reports `claude.status=ok`.
- Exercised Codex collector with invalid dev auth; `/status` reports `codex.status=auth_expired`, proving the degraded path is surfaced.
- Installed/used ESP-IDF v5.5 from `~/esp/esp-idf-v5.5` with the Python 3.13 IDF env workaround.
- Cloned `xiaozhi-esp32` into `~/projects/xiaozhi-esp32-fork`; upstream has no `v2.0.8` tag, so `v2.0.5` was used as the nearest 2.0.x baseline.
- Built and flashed stock `xiaozhi-esp32` for `sp-esp32-s3-1.54-muma`; boot log confirms `SKU=sp-esp32-s3-1.54-muma`, LVGL/display/backlight/WiFi provisioning all start.
- Wrote Phase 01 board-identification report with GPIO map, build/flash notes, and observed ES8311 audio NACK issue.

### 📁 Files changed
- `usage-api/*` — NEW, API, Dockerfile, compose, tests, examples
- `collectors/claude/*` — NEW, ccusage collector, launchd template, installer, tests
- `collectors/codex/*` — NEW, wham/usage collector, auth cURL parser, Dockerfile, docs, tests
- `ops/*` — NEW, daily smoke-test script and launchd installer/template
- `README.md` — NEW, setup/runbook for the local stack
- `plans/260517-1045-esp32-usage-monitor/*` — UPDATED, phase statuses and Phase 01 report
- `.gitignore` — UPDATED, ignore local secrets/runtime data for the new stack
- `docs/codex-memory.md` — UPDATED, prepended this handoff entry

### 🔑 Key decisions
- Keep API/collector credentials outside git in ignored `.env`, `secrets/`, and `usage-api/data/`.
- Leave Docker `usage-api` + `codex-collector` running with dev credentials so local `/status` is immediately inspectable.
- Do not claim Codex usage is live until real browser auth is placed into `secrets/codex_auth.json`.
- Treat stock audio codec errors as non-blocking because the target firmware should remove audio entirely in Phase 05.

### 📊 State changes (active projects)
- esp32-usage-monitor Phase 01: pending -> done.
- Phase 02: pending -> done locally with Docker.
- Phase 03: pending -> local done with live Claude snapshot.
- Phase 04: pending -> local done, blocked only by real Codex auth refresh.
- Phase 08: pending -> scaffolded; production Cloudflare tunnel still needs user domain/token.

### 🚨 Follow-ups
- [ ] Replace dev `secrets/codex_auth.json` with real ChatGPT browser auth using `collectors/codex/README-auth-refresh.md`.
- [ ] Replace dev tokens in `usage-api/.env` with production random values before exposing the API.
- [ ] Configure Cloudflare Tunnel token/domain if ESP32 must fetch outside LAN.
- [ ] Start Phase 05: create stripped usage-monitor firmware from `sp-esp32-s3-1.54-muma`, removing audio/Xiaozhi cloud while preserving WiFi/LVGL/button/battery.
- [ ] Measure idle/deep-sleep current with a meter; this could not be validated from software.

### ⚠ Blockers / open questions
- Current Codex collector status is intentionally `auth_expired` because only fake dev auth is present.
- Fresh full-flash backup retries failed due serial stream corruption; preserved factory backup remains available at `~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin`.

## 2026-05-17 11:50 SGT — git-init-and-origin-setup

**Actor**: codex-cli
**Branch**: main
**Trigger**: User asked to create git repo, then provided remote `https://github.com/sonpython/vibecode-pupy`

### ✅ Done
- Initialized local git repository in `/Users/michaelphan/projects/vibecode-pupy` with default branch `main`.
- Added remote `origin` pointing to `https://github.com/sonpython/vibecode-pupy`.
- Checked remote heads with `git ls-remote --heads origin`; remote currently has no branch heads, so initial push to `main` is safe.
- Updated `.gitignore` so important project context (`.claude/`, `plans/`, docs, handoff files) can be tracked while runtime logs, session state, and virtualenv remain ignored.

### 📁 Files changed
- `.gitignore` — UPDATED, track project ClaudeKit/plans while ignoring local runtime/generated state
- `docs/codex-memory.md` — UPDATED, prepended this session entry

### 🔑 Key decisions
- Use `main` as initial branch.
- Track `.claude/` and `plans/` because this workspace's handoff, rules, skills, and active implementation plan are the source of truth for future agents.

### 🚨 Follow-ups
- [ ] Push initial commit to `origin/main`.

## 2026-05-17 11:48 SGT — codex-ramp-up-and-claudekit-map

**Actor**: codex-cli
**Branch**: (no git yet — project not init'd)
**Trigger**: User asked Codex to pull latest code, scout Claude/Codex handoff docs, add startup handoff rule, and map ClaudeKit assets for Codex use

### ✅ Done
- Checked git state: `/Users/michaelphan/projects/vibecode-pupy` is not a git repository, so `git pull` cannot run until the workspace is initialized or cloned with `.git`.
- Read current handoff/context docs: `docs/codex-memory.md`, `docs/session-sync.md`, `docs/app-journey-story.md`, `docs/codex-memory-protocol.md`, `docs/codex-starter-prompt.md`, `CLAUDE.md`, and active plan `plans/260517-1045-esp32-usage-monitor/plan.md`.
- Synced ClaudeKit assets from project `.claude/` into Codex global directories: skills to `~/.codex/skills/`, agents/scripts/config to `~/.codex/claudekit/`, rules to `~/.codex/rules/claudekit/`, and plan templates to `~/.codex/templates/claudekit/plans/`.
- Added root `AGENTS.md` with mandatory startup handoff protocol for Codex: check git/pull if possible, read memory/session/app-story/active-plan/rules, confirm state, and log state-changing sessions.

### 📁 Files changed
- `AGENTS.md` — NEW, Codex startup rules + ClaudeKit mapping notes
- `docs/codex-memory.md` — UPDATED, prepended this session entry
- `~/.codex/skills/*` — UPDATED/NEW, synced project ClaudeKit skills except `.venv`
- `~/.codex/claudekit/*` — UPDATED, synced agents/scripts/config
- `~/.codex/rules/claudekit/*` — UPDATED, synced ClaudeKit project rules
- `~/.codex/templates/claudekit/plans/*` — UPDATED, synced plan templates

### 🔑 Key decisions
- Did not copy `.claude/skills/.venv` into Codex because virtual environments are machine-specific generated state; Codex can use scripts and reinstall dependencies when needed.
- Kept project `.claude/` as the workspace source of truth; global `~/.codex/` copies are compatibility mapping for Codex.

### 📊 State changes (active projects)
- Codex handoff support: missing root `AGENTS.md` → enabled
- ClaudeKit-to-Codex mapping: partial existing global copy → refreshed from this workspace

### 🚨 Follow-ups
- [ ] Decide whether to `git init` this workspace or clone the real remote repo into this path so memory-bridge can pull/commit/push.
- [ ] If this project has an intended remote, add `origin` and run first commit/push for `AGENTS.md` + memory files.

### ⚠ Blockers / open questions
- No `.git` directory exists in the workspace, so latest remote code cannot be pulled from here.

## 2026-05-17 11:40 SGT — esp32-xiaozhi-usage-monitor-brainstorm-plan

**Actor**: claude-code-opus-4.7
**Branch**: (no git yet — project not init'd)
**Trigger**: User invoked `/brainstorm` to convert existing Xiaozhi ESP32 alarm-clock robot into single-purpose desk gadget showing Claude/Codex usage percentages (5h block + weekly limits) on power-button press

### ✅ Done
- Live USB probe → confirmed hardware: ESP32-S3 N16R8 (16MB flash + 8MB PSRAM, native USB-JTAG, MAC `a0:f2:62:e8:a4:40`), currently running `xiaozhi-esp32` v2.0.8 (github.com/78/xiaozhi-esp32, ESP-IDF v5.5, Mar 7 2026 build). TTY: `/dev/cu.usbmodem83101`.
- Discovered Codex Cloud has clean JSON API `GET https://chatgpt.com/backend-api/wham/usage` returning `rate_limit.primary_window.used_percent` + `secondary_window` + `reset_at` (unix ts). Auth: Bearer JWT (~10d TTL) + session cookies (`__Secure-next-auth.session-token`, `cf_clearance`, `_puid`). Replaces Playwright DOM scraping.
- Wrote brainstorm summary at `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md` (15KB, 9 sections).
- Created 8-phase implementation plan at `plans/260517-1045-esp32-usage-monitor/` (plan.md + phase-01..08 *.md, ~6.5d total effort).
- Full flash backup at `~/esp-backups/xiaozhi-jqrnz-A0F262E8A440-2026-05-17-factory.bin` (16MiB, SHA256 `3a6a8a1f8a3a46be3993cd46ca3d0371c4003b953257995be0b2de789f6b583f`).
- `esptool erase-flash` (17s) to silence the Chinese-speaking factory firmware.
- `memory init` — set up `docs/` from skill templates (no git so no commit/push).

### 📁 Files changed
- `plans/reports/brainstorm-260517-1045-esp32-usage-monitor.md` — NEW
- `plans/260517-1045-esp32-usage-monitor/plan.md` — NEW
- `plans/260517-1045-esp32-usage-monitor/phase-{01..08}-*.md` — NEW (8 files)
- `docs/session-sync.md` `docs/codex-memory.md` `docs/app-journey-story.md` `docs/codex-memory-protocol.md` `docs/codex-starter-prompt.md` — NEW (memory-bridge init from templates)

### 🔑 Key decisions
- **Firmware base**: fork `xiaozhi-esp32` v2.0.8 (strip audio/AI/cloud, keep WiFi+display+button+LVGL+wifi_provisioning). Not from-scratch ESP-IDF. Saves ~2d.
- **Codex collector**: hit `/backend-api/wham/usage` directly (httpx + manual auth file), Docker container on 192.168.1.120, 5min poll, webhook on 401. Not Playwright. Saves ~2-3d vs scraping approach.
- **Claude collector**: `ccusage` CLI parsing local `~/.claude/projects/*.jsonl` on Mac via launchd timer (no public Claude usage API for subscription plans exists).
- **Exposure**: Cloudflare Tunnel HTTPS to `usage.<domain>` (real TLS cert, ESP32 verifies via ESP-IDF cert bundle).
- **No OTA** — USB-C re-flash acceptable for personal device.

### 📊 State changes (active projects)
- esp32-usage-monitor: nonexistent → planned (Phase 01-08 ready)
- Xiaozhi device hardware: running factory firmware → flash erased (silent, factory backup preserved)

### 🚨 Follow-ups
- [ ] Phase 01: identify exact xiaozhi-esp32 board variant matching this hardware (try `idf.py monitor` first; fallback open PCB silkscreen). Capture display controller + GPIO map + button GPIO + backlight pin.
- [ ] Phase 01: install ESP-IDF v5.5 toolchain (`~/esp/esp-idf`).
- [ ] Consider `git init` on `vibecode-pupy` — currently no git, so memory-bridge can't push cross-environment.
- [ ] Phase 04: write Codex auth refresh runbook (cf_clearance is the fastest-rotating cookie).

### ⚠ Blockers / open questions
- Exact board variant name within `xiaozhi-esp32/main/boards/*` still unknown — Alibaba spec sheet (JQRNZ / Estella, "1024×768 AMOLED") is marketing fiction; real likely 240×280 IPS LCD. Resolved in Phase 01.

### 💡 Lessons learned
- Always probe ESP32 hardware live before trusting datasheets. Alibaba product specs frequently bullshit screen specs.
- Vendor dashboards that look like they need scraping usually expose a clean JSON API under the hood — check Network tab before reaching for Playwright.
- `ck plan create` doesn't exist in ck CLI v3.35.0 (only `agents`, `commands`, `config`, `skills`, etc.). The ck-plan skill template references it as if it does — fall back to manual file creation.
