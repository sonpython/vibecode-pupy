---
phase: 2
title: "Usage-API skeleton (FastAPI + SQLite)"
status: pending
priority: P1
effort: "0.5d"
dependencies: []
---

# Phase 02: Usage-API Skeleton

## Overview
Stand up a tiny FastAPI service in Docker on 192.168.1.120 with two endpoints (`POST /collect/{source}` from collectors, `GET /status` for ESP32) and SQLite storage of the latest per-source snapshot. Dummy data only — real collectors hook up in Phases 03/04.

## Context Links
- Brainstorm §5.3
- Schema and reset-time semantics already locked

## Requirements
### Functional
- `POST /collect/{source}` accepts JSON, validates bearer token, upserts snapshot, returns 204
- `GET /status` aggregates latest from both sources, recomputes `resets_in_sec` from absolute `resets_at`, returns combined JSON
- Stale detection: if `now - ts > 600s` → `status: "stale"` in response (but still return last-good data)
- Sources supported: `claude`, `codex`

### Non-functional
- Memory <100MB (single FastAPI worker fine)
- Response time <50ms for `/status` (SQLite hot in OS cache)
- Per-source auth tokens (different bearer for Claude collector vs. Codex collector)
- ESP32 endpoint uses shared `X-Device-Secret` header (no per-device tokens for MVP)

## Architecture

```
clients ─┐
         ├─► POST /collect/{source}  ─► auth check ─► upsert snapshot (SQLite)
         │
ESP32  ──┴─► GET /status            ─► auth check ─► aggregate + recompute relative ─► JSON
```

### Storage schema (SQLite)
```sql
CREATE TABLE snapshots (
  source TEXT PRIMARY KEY,            -- 'claude' | 'codex'
  ts INTEGER NOT NULL,                -- unix ts of when snapshot received
  payload TEXT NOT NULL,              -- raw JSON from collector
  status TEXT NOT NULL                -- 'ok' | 'auth_expired' | 'error'
);
```

### Response JSON (GET /status)
```json
{
  "ts": "2026-05-17T10:45:00+07:00",
  "claude": {"current_pct": 50, "current_resets_in_sec": 4920, "weekly_pct": 11, "weekly_resets_in_sec": 547200, "status": "ok", "stale_sec": 30},
  "codex":  {"current_pct": 1,  "current_resets_in_sec": 11376, "weekly_pct": 0,  "weekly_resets_in_sec": 580165, "status": "ok", "stale_sec": 120}
}
```

## Related Code Files
- Create: `usage-api/app.py` (FastAPI app, ~120 LoC)
- Create: `usage-api/schemas.py` (Pydantic models for in/out)
- Create: `usage-api/Dockerfile`
- Create: `usage-api/docker-compose.yml` (just api service in this phase; cloudflared in Phase 08)
- Create: `usage-api/requirements.txt` (`fastapi`, `uvicorn[standard]`, `pydantic`)
- Create: `usage-api/.env.example` (token names, no values)

## Implementation Steps

1. **Scaffold Python project**:
   ```bash
   mkdir -p usage-api && cd usage-api
   echo "fastapi\nuvicorn[standard]\npydantic" > requirements.txt
   ```

2. **`schemas.py`** — Pydantic models:
   - `CollectIn`: source-specific shape (Claude vs Codex), both must include `current_pct`, `weekly_pct`, `current_resets_at` (ISO), `weekly_resets_at` (ISO)
   - `StatusOut`: aggregated response shape
   - Use `model_config = {"extra": "forbid"}` to reject unknown fields

3. **`app.py`**:
   - FastAPI with two routes
   - Bearer-token check via `Depends(verify_collector_token)` reading per-source token from env: `CLAUDE_COLLECTOR_TOKEN`, `CODEX_COLLECTOR_TOKEN`
   - SQLite at `/data/usage.db`, single connection with `check_same_thread=False`, WAL mode
   - `GET /status` requires header `X-Device-Secret` matching `DEVICE_SECRET` env

4. **Reset-time recompute** (in `/status` handler):
   ```python
   def secs_until(iso_ts: str) -> int:
       dt = datetime.fromisoformat(iso_ts)
       return max(0, int((dt - datetime.now(dt.tzinfo)).total_seconds()))
   ```

5. **Stale detection**:
   ```python
   stale_sec = int(time.time() - snapshot.ts)
   status = snapshot.status if stale_sec < 600 else "stale"
   ```

6. **Dockerfile** — slim Python base, install requirements, copy app, expose 8080, run `uvicorn app:app --host 0.0.0.0 --port 8080`

7. **docker-compose.yml** — single service, mount volume `./data:/data`, env from `.env`, port `127.0.0.1:8080:8080` (loopback-only until cloudflared added)

8. **Manual smoke test**:
   ```bash
   curl -X POST http://localhost:8080/collect/claude \
     -H "Authorization: Bearer $CLAUDE_COLLECTOR_TOKEN" \
     -d '{"current_pct":50,"weekly_pct":11,...}'

   curl http://localhost:8080/status -H "X-Device-Secret: $DEVICE_SECRET"
   ```

## Todo List
- [ ] Scaffold + requirements
- [ ] schemas.py with both source shapes
- [ ] app.py with both endpoints + auth + SQLite
- [ ] Dockerfile + docker-compose.yml
- [ ] Smoke test passes both endpoints
- [ ] Deployed on 192.168.1.120 (loopback for now)

## Success Criteria
- [ ] POST + GET work via curl
- [ ] Wrong/missing token returns 401
- [ ] Stale data (>10min) flagged in status field
- [ ] Container restart preserves data (SQLite volume persists)

## Risk Assessment
| Risk | Mitigation |
|---|---|
| Token leak via logs | uvicorn access log redacts `Authorization` header by default; verify config |
| SQLite write contention | Two collectors max, low qps → no real concurrency issue. WAL mode covers it. |
| Schema drift between collectors | Pydantic `extra: forbid` will surface mismatches loudly during dev |

## Security Considerations
- All secrets via env file mounted from outside repo (`.env` in .gitignore)
- No public exposure in this phase — bound to loopback. Public HTTPS via cloudflared in Phase 08.
- Per-source tokens prevent one compromised collector from spoofing the other

## Next Steps
→ Phases 03 + 04 can proceed in parallel (each consumes this API).
