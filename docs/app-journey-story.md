# vibecode-pupy — App Journey Story

> **Mục đích**: Cung cấp context cô đọng cho AI agent mới ramp up nhanh về dự án trong < 5 phút.
> **Audience**: AI agent + new team members.
> **Format**: Narrative + key facts + current state.
> **Last updated**: 2026-05-17 SGT

---

## TL;DR (60 giây)

`vibecode-pupy` là sandbox workspace cá nhân của Michael Phan (sonunix@gmail.com) cho các side-project xoay quanh AI tooling + hardware. Project chính đang hoạt động: **ESP32 Xiaozhi → Claude/Codex usage monitor** — biến con robot Xiaozhi ESP32-S3 thành thiết bị desk gadget chỉ hiển thị usage Claude Code + Codex Cloud trên màn hình mỗi lần bấm nút. Workspace dùng ClaudeKit (kit `engineer`) + skill ecosystem rộng để brainstorm/plan/cook các project.

---

## 1. Bối cảnh

- Workspace cho experiment + hardware tinkering, không phải production code.
- Tận dụng existing hardware (Xiaozhi ESP32-S3 robot) thay vì mua mới.
- Pain point: phải mở web/CLI để check usage Claude/Codex liên tục → muốn 1 thiết bị physical.

---

## 2. Cấu trúc

```
vibecode-pupy/ (single-user personal)
├── plans/
│   ├── reports/         # brainstorm + research outputs
│   └── {date}-{slug}/   # implementation plans
├── docs/                # memory-bridge SoT files + project docs
└── .claude/             # rules, skills config
```

Owner: Michael Phan (sonunix@gmail.com).

---

## 3. Strategy

1. Personal AI tooling — biến knowledge work workflows thành physical/ambient experiences.
2. Reuse existing hardware before buying new (Xiaozhi clock = first example).
3. Self-host khi possible (Proxmox/Docker @ 192.168.1.120 + Cloudflare Tunnel cho HTTPS).

---

## 4. Stack & Infrastructure

| System | Path / URL | Role |
|---|---|---|
| ClaudeKit | `~/.claude/` + `.claude/` in project | Brainstorm + plan + cook workflow |
| Proxmox homelab | 192.168.1.120 (Docker host) | Services + agg layer |
| Cloudflare Tunnel | `cloudflared` | HTTPS exposure for LAN services |
| ESP32-S3 Xiaozhi | MAC `a0:f2:62:e8:a4:40`, TTY `/dev/cu.usbmodem83101` | Hardware target |
| ESP-IDF v5.5 | `~/esp/esp-idf` (planned) | Firmware toolchain |
| `xiaozhi-esp32` v2.0.8 | github.com/78/xiaozhi-esp32 | Firmware base to fork |

---

## 5. Product & Pricing

Personal projects. No commercial scope.

---

## 6. Story arc — milestones

### 2026-05
- 2026-05-17: First brainstorm + plan session. ESP32 usage monitor scoped (8 phases, ~6.5d). Xiaozhi firmware backed up + erased to silence.

---

## 7. Active projects (current state, 2026-05-17 SGT)

| Project | Status | Owner | Notes |
|---|---|---|---|
| esp32-usage-monitor | **planned** | sonunix | Plan `plans/260517-1045-esp32-usage-monitor/`. Hardware verified ESP32-S3 N16R8. Flash erased. Ready for Phase 01 (board variant identify + ESP-IDF setup). |

---

## 8. Rules & guardrails

1. Follow `.claude/rules/development-rules.md` — YAGNI/KISS/DRY, kebab-case filenames, file <200 LoC.
2. Plans go in `plans/{date}-{slug}/`, reports in `plans/reports/`.
3. Don't commit secrets (especially Codex JWT/cookies, device tokens).
4. Always full-flash backup before erasing ESP32 firmware.
5. Hardware probe (esptool chip_id/flash_id) before trusting Alibaba spec sheets.

→ Full rules: `.claude/rules/*.md`

---

## 9. Where to find context (priority order)

| File | Purpose |
|---|---|
| `docs/session-sync.md` | Claude session log (top = newest) |
| `docs/app-journey-story.md` | **THIS FILE** — project arc |
| `docs/codex-memory.md` | Cross-agent log |
| `plans/260517-1045-esp32-usage-monitor/plan.md` | Current active plan |
| `plans/reports/brainstorm-260517-*.md` | Source-of-truth brainstorm output |
| `CLAUDE.md` (root) | Claude project rules |

---

## 10. Glossary

- **Xiaozhi** — Open-source Chinese ESP32-based AI alarm-clock platform (`github.com/78/xiaozhi-esp32`). Comes pre-flashed on JQRNZ/Estella OEM clocks sold on Alibaba.
- **ccusage** — npm tool that parses local `~/.claude/projects/*.jsonl` to compute Claude Code usage % (5h block + weekly Max plan).
- **wham/usage** — Codex Cloud internal endpoint (`/backend-api/wham/usage`) returning JSON usage stats; auth via JWT + ChatGPT session cookies.
- **N16R8** — ESP32-S3 module variant: 16MB flash + 8MB PSRAM (R = PSRAM, 8 = MB).
- **cf_clearance** — Cloudflare anti-bot cookie that rotates fastest; main operational pain for Codex collector auth.

---

## Unresolved / Open questions

- Exact `xiaozhi-esp32` board variant matching the JQRNZ hardware (resolved in Phase 01).
- Whether to `git init` this workspace (memory-bridge currently can't push without git).
- Whether to add OTA support to firmware in v2 (skipped for MVP).
