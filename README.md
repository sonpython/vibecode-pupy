# vibecode-pupy

Workspace for the ESP32 Xiaozhi Claude/Codex usage monitor.

## Current System

```
Claude collector on Mac ─┐
                         ├─> Usage API (FastAPI + SQLite) ──> ESP32 /status
Codex collector in Docker ┘
```

The active implementation plan is in `plans/260517-1045-esp32-usage-monitor/`.
Agent handoff starts from `AGENTS.md`, `docs/codex-memory.md`, and
`docs/session-sync.md`.

## Local API

Create local secrets:

```bash
cp usage-api/.env.example usage-api/.env
$EDITOR usage-api/.env
```

Run:

```bash
docker compose -f usage-api/docker-compose.yml up -d --build usage-api
curl http://127.0.0.1:8080/healthz
curl -H "X-Device-Secret: $DEVICE_SECRET" http://127.0.0.1:8080/status
```

## Claude Collector

The Claude collector calls the authenticated claude.ai usage endpoint,
normalizes it, and posts to `/collect/claude`.

```bash
mkdir -p secrets
pbpaste | python3 collectors/claude/parse_claude_curl.py > secrets/claude_auth.json
chmod 600 secrets/claude_auth.json
docker compose -f usage-api/docker-compose.yml up -d --build claude-collector claude-session-keeper
```

Copy the cURL from `https://claude.ai/settings/usage` for
`/api/organizations/.../usage`. The auth file is local-only and must not be
committed.

## Codex Collector

Create auth bundle from a browser cURL request:

```bash
mkdir -p secrets
pbpaste | python3 collectors/codex/parse_curl.py > secrets/codex_auth.json
chmod 600 secrets/codex_auth.json
```

Run collector:

```bash
docker compose -f usage-api/docker-compose.yml up -d --build codex-collector
docker compose -f usage-api/docker-compose.yml logs --tail=50 codex-collector
```

If auth expires, `/status` shows `codex.status = "auth_expired"`.

## Cloudflare Tunnel

Use token mode for MVP:

```yaml
cloudflared:
  image: cloudflare/cloudflared:latest
  restart: unless-stopped
  command: tunnel --no-autoupdate run --token ${CF_TUNNEL_TOKEN}
  depends_on:
    - usage-api
```

Keep `CF_TUNNEL_TOKEN` in `usage-api/.env`. Do not commit tunnel credential JSON.

## Smoke Test

Manual:

```bash
USAGE_STATUS_URL=http://127.0.0.1:8080/status \
DEVICE_SECRET=... \
bash ops/daily_smoke_test.sh
```

Install launchd:

```bash
bash ops/install_smoke_test.sh
```

## Firmware Notes

Hardware already probed:

- ESP32-S3 QFN56 rev v0.2
- 16 MB flash, 8 MB PSRAM
- Native USB serial/JTAG
- MAC `a0:f2:62:e8:a4:40`
- Port `/dev/cu.usbmodem83101`

ESP-IDF v5.5 is installed at `~/esp/esp-idf-v5.5`.
Upstream firmware clone is at `~/projects/xiaozhi-esp32-fork`.

## Security

Never commit:

- `usage-api/.env`
- `secrets/codex_auth.json`
- `secrets/claude_auth.json`
- Cloudflare tunnel credentials
- ESP32 flash backups
- ChatGPT cookies/JWTs
- Claude cookies/session keys
