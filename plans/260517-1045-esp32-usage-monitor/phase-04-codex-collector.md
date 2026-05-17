---
phase: 4
title: "Codex collector (wham/usage JSON API)"
status: local done; auth needed
priority: P1
effort: "0.5-1d"
dependencies: [2]
---

# Phase 04: Codex Collector

## Overview
Dockerized Python service that calls `GET https://chatgpt.com/backend-api/wham/usage` every 5min with extracted JWT + cookies, normalizes the 5-hour + weekly window data, POSTs to Usage-API. Handles 401 by marking auth_expired and firing webhook alert.

## Context Links
- Brainstorm §5.2 (after API discovery)
- Live response sample captured during brainstorm (see report)
- Endpoint: `GET https://chatgpt.com/backend-api/wham/usage`

## Requirements
### Functional
- Periodic call to `/wham/usage` with: `Authorization: Bearer <JWT>` + browser cookies + OAI headers
- Extract `rate_limit.primary_window.used_percent` (5h) and `rate_limit.secondary_window.used_percent` (weekly)
- Convert `reset_at` unix timestamps to ISO format with timezone
- POST snapshot to Usage-API
- On HTTP 401 (auth expired): POST `status: auth_expired` snapshot + fire webhook alert

### Non-functional
- Run as Docker container alongside usage-api on <docker-host-ip>
- Auth credentials in mounted secret file (not in image)
- Webhook target configurable (Discord webhook URL or Pushover)
- Poll cadence env-tunable (default 300s)

## Architecture

```
docker container ─► every N sec ─► httpx.get(/wham/usage, JWT+cookies+headers)
                         │
                         ├─► 200 OK   → parse → POST /collect/codex (status=ok)
                         ├─► 401     → POST /collect/codex (status=auth_expired)
                         │             └─► webhook.post("Codex auth expired, refresh tokens")
                         └─► other   → log, skip cycle
```

## Related Code Files
- Create: `collectors/codex/codex_collector.py` (~100 LoC, snake_case)
- Create: `collectors/codex/Dockerfile`
- Create: `collectors/codex/docker-compose.yml` (or add to usage-api compose)
- Create: `collectors/codex/.env.example`
- Create: `collectors/codex/auth.json.example` (cookies + JWT + headers template)
- Create: `collectors/codex/README-auth-refresh.md` (step-by-step manual extract)

## Implementation Steps

1. **Auth bundle file** (`/secrets/codex_auth.json`, mounted):
   ```json
   {
     "bearer": "eyJhbGc...",
     "cookies": {
       "__Secure-next-auth.session-token": "...",
       "cf_clearance": "...",
       "_puid": "...",
       "__cf_bm": "..."
     },
     "headers": {
       "oai-device-id": "...",
       "oai-client-version": "...",
       "user-agent": "Mozilla/5.0 ..."
     }
   }
   ```

2. **`codex_collector.py`**:
   ```python
   import json, os, time, sys
   from datetime import datetime, timezone, timedelta
   import httpx

   ENDPOINT = "https://chatgpt.com/backend-api/wham/usage"
   API_URL  = os.environ["USAGE_API_URL"]
   TOKEN    = os.environ["CODEX_COLLECTOR_TOKEN"]
   WEBHOOK  = os.environ.get("AUTH_ALERT_WEBHOOK")
   INTERVAL = int(os.environ.get("POLL_INTERVAL_SEC", "300"))
   TZ       = timezone(timedelta(hours=7))  # Asia/Saigon

   def load_auth():
       with open("/secrets/codex_auth.json") as f:
           return json.load(f)

   def fetch_usage(auth):
       hdrs = {"authorization": f"Bearer {auth['bearer']}", **auth["headers"]}
       r = httpx.get(ENDPOINT, headers=hdrs, cookies=auth["cookies"], timeout=15)
       return r

   def to_snapshot(j):
       rl = j["rate_limit"]
       pw, sw = rl["primary_window"], rl["secondary_window"]
       def iso(unix): return datetime.fromtimestamp(unix, tz=TZ).isoformat()
       return {
           "current_pct":       pw["used_percent"],
           "current_resets_at": iso(pw["reset_at"]),
           "weekly_pct":        sw["used_percent"],
           "weekly_resets_at":  iso(sw["reset_at"]),
           "status": "ok",
       }

   def alert(msg):
       if WEBHOOK:
           httpx.post(WEBHOOK, json={"content": msg}, timeout=5)

   def push(snap):
       httpx.post(f"{API_URL}/collect/codex", json=snap,
                  headers={"authorization": f"Bearer {TOKEN}"}, timeout=10).raise_for_status()

   def loop():
       auth = load_auth()
       while True:
           try:
               r = fetch_usage(auth)
               if r.status_code == 401:
                   push({"status": "auth_expired", "current_pct": -1, "weekly_pct": -1,
                         "current_resets_at": "", "weekly_resets_at": ""})
                   alert("⚠️ Codex auth expired. Re-extract cookies + JWT.")
               elif r.status_code == 200:
                   push(to_snapshot(r.json()))
               else:
                   sys.stderr.write(f"unexpected {r.status_code}: {r.text[:200]}\n")
           except Exception as e:
               sys.stderr.write(f"loop_error: {e}\n")
           time.sleep(INTERVAL)

   if __name__ == "__main__":
       loop()
   ```

3. **Dockerfile**: python:3.12-slim + `pip install httpx` + COPY script + ENTRYPOINT.

4. **docker-compose** entry:
   ```yaml
   codex-collector:
     build: ./collectors/codex
     restart: unless-stopped
     environment:
       USAGE_API_URL: http://usage-api:8080
       CODEX_COLLECTOR_TOKEN: ${CODEX_COLLECTOR_TOKEN}
       AUTH_ALERT_WEBHOOK: ${AUTH_ALERT_WEBHOOK}
     volumes:
       - ./secrets:/secrets:ro
     depends_on: [usage-api]
   ```

5. **Manual auth-extraction doc** (`README-auth-refresh.md`):
   - Step 1: Open chatgpt.com in browser, log in
   - Step 2: F12 → Network → reload analytics page → find `/wham/usage` request
   - Step 3: Right-click → "Copy as cURL"
   - Step 4: Use provided `parse_curl.py` helper (~30 LoC) to extract `authorization` + cookies + headers into `codex_auth.json` format
   - Step 5: `scp` to <docker-host-ip>:/path/to/secrets/codex_auth.json; restart container

6. **Validation**:
   - First call returns 200 with `used_percent` matching the dashboard
   - Force 401 by truncating JWT → webhook fires, status row updates to `auth_expired`

## Todo List
- [ ] codex_collector.py
- [ ] Dockerfile + compose entry
- [ ] codex_auth.json template + manual extract doc
- [ ] parse_curl.py helper
- [ ] First live snapshot received by API
- [ ] Webhook alert verified (force 401)

## Success Criteria
- [ ] Healthy poll loop, snapshot pushed every 5min
- [ ] 401 triggers webhook + sets status=auth_expired (does not crash)
- [ ] Restart container → resumes cleanly from mounted auth file

## Risk Assessment
| Risk | Mitigation |
|---|---|
| `cf_clearance` rotates faster than weekly | Webhook → user re-extracts within minutes. If <24h cadence becomes routine, escalate to Phase B (Playwright session keep-alive) |
| JWT 10-day TTL: silent expiry | 401 webhook covers it. Optionally pre-warn at 8d via JWT `exp` decode |
| OpenAI deprecates `/wham/usage` | Daily smoke test (separate cron): if 404 → alert |
| Cloudflare anti-bot challenges container IP | Use residential-friendly UA headers; if blocked, escalate to Playwright Phase B |

## Security Considerations
- `codex_auth.json` contains live ChatGPT session — treat as password equivalent
- Mounted read-only into container
- Never commit; `.gitignore` enforces. Provide `.example` only.
- Webhook URL is a secret too (Discord webhooks can be abused) — env-only

## Next Steps
→ Independent of Phase 03. Both gate Phase 08 (public exposure).
