---
phase: 3
title: "Claude collector (ccusage on Mac)"
status: local done
priority: P1
effort: "0.5d"
dependencies: [2]
---

# Phase 03: Claude Collector

## Overview
Python script on the Mac that runs `ccusage blocks --json` every 60s via launchd, normalizes the output to the API's snapshot schema, and POSTs to Usage-API. Backs the "Claude" half of the device display.

## Context Links
- Brainstorm §5.1
- `ccusage` package: https://www.npmjs.com/package/ccusage
- Local data source: `~/.claude/projects/*.jsonl` (parsed by ccusage)

## Requirements
### Functional
- Invoke `ccusage blocks --json` and parse output
- Compute `current_pct` (5h block used %) and `weekly_pct` (weekly Max-plan used %)
- Extract reset times as absolute ISO timestamps
- POST to `https://usage.<domain>/collect/claude` with bearer token

### Non-functional
- Single-file Python, no virtualenv (use system Python 3.11+ on macOS)
- launchd timer every 60s, no overlapping runs (`AbandonProcessGroup` handles stragglers)
- Logs to `~/Library/Logs/claude-collector.log` (rotated by launchd)
- Failure: log + exit non-zero; never retry storm

## Architecture

```
launchd timer ──► claude-collector.py
                      │
                      ├─► subprocess: npx ccusage blocks --json
                      │
                      ├─► parse: extract active block + weekly totals
                      │
                      ├─► transform: ccusage shape → API schema
                      │
                      └─► httpx.post(API_URL, bearer_token)
```

## Related Code Files
- Create: `collectors/claude/claude_collector.py` (~80 LoC, snake_case per Python convention)
- Create: `collectors/claude/com.user.claude-collector.plist` (launchd LaunchAgent)
- Create: `collectors/claude/install.sh` (loads plist into launchd)
- Create: `collectors/claude/.env.example`

## Implementation Steps

1. **`claude_collector.py`** structure:
   ```python
   import json, os, subprocess, sys, time
   from datetime import datetime, timezone
   import httpx

   API_URL = os.environ["USAGE_API_URL"]              # e.g. https://usage.x.com
   TOKEN   = os.environ["CLAUDE_COLLECTOR_TOKEN"]

   def run_ccusage() -> dict:
       out = subprocess.check_output(
           ["npx", "-y", "ccusage@latest", "blocks", "--json"],
           timeout=30, text=True)
       return json.loads(out)

   def to_snapshot(cc: dict) -> dict:
       # Map ccusage shape → API schema (placeholder — verify exact ccusage keys in dev)
       return {
           "current_pct":       cc["activeBlock"]["usedPct"],
           "current_resets_at": cc["activeBlock"]["endsAt"],
           "weekly_pct":        cc["weekly"]["usedPct"],
           "weekly_resets_at":  cc["weekly"]["endsAt"],
           "status": "ok",
       }

   def main():
       try:
           snap = to_snapshot(run_ccusage())
       except Exception as e:
           sys.stderr.write(f"collect_failed: {e}\n")
           sys.exit(1)
       httpx.post(f"{API_URL}/collect/claude", json=snap,
                  headers={"Authorization": f"Bearer {TOKEN}"},
                  timeout=10).raise_for_status()

   if __name__ == "__main__":
       main()
   ```

2. **Verify ccusage JSON shape during dev**: run `npx -y ccusage@latest blocks --json | jq` and map exact key paths into `to_snapshot()`. The placeholder above must be replaced with real keys.

3. **launchd plist** at `~/Library/LaunchAgents/com.user.claude-collector.plist`:
   - `StartInterval`: 60
   - `ProgramArguments`: `["/usr/bin/python3", "<abs path>/claude_collector.py"]`
   - `EnvironmentVariables`: `USAGE_API_URL`, `CLAUDE_COLLECTOR_TOKEN`
   - `StandardOutPath` / `StandardErrorPath`: `~/Library/Logs/claude-collector.log`
   - `AbandonProcessGroup`: true (prevents zombie chains if ccusage hangs)

4. **install.sh**:
   ```bash
   cp com.user.claude-collector.plist ~/Library/LaunchAgents/
   launchctl unload ~/Library/LaunchAgents/com.user.claude-collector.plist 2>/dev/null
   launchctl load ~/Library/LaunchAgents/com.user.claude-collector.plist
   ```

5. **Validation**:
   - `launchctl list | grep claude-collector` → status 0
   - After 90s, `tail ~/Library/Logs/claude-collector.log` → no errors
   - On API host: `sqlite3 /data/usage.db "SELECT * FROM snapshots WHERE source='claude'"` → recent row

## Todo List
- [ ] Verify real ccusage JSON schema
- [ ] Map fields into to_snapshot()
- [ ] launchd plist installed + loaded
- [ ] First successful POST observable in API SQLite
- [ ] Test stale handling: stop collector → API reports `stale` after 10min

## Success Criteria
- [ ] launchd reports timer active
- [ ] API receives fresh snapshot every minute (±5s)
- [ ] On ccusage failure, exit non-zero (caught by launchd, retried next tick)

## Risk Assessment
| Risk | Mitigation |
|---|---|
| ccusage breaking change | Pin major version (`ccusage@^X`) once verified; track upstream release notes |
| `npx` cold-start cost (~3-5s) | Acceptable for 60s cadence; or pre-install globally to skip download |
| Network down → repeated POST failures | Just log + exit; launchd retries; no exponential backoff needed |
| Mac asleep → no data | Documented limitation; API marks `stale` after 10min, ESP32 shows offline indicator |

## Security Considerations
- Token in plist `EnvironmentVariables` → stored plaintext under user's home (acceptable for single-user Mac, file readable only by user)
- HTTPS to `usage.<domain>` only after Phase 08 cloudflared (during dev, can use local IP + http)

## Next Steps
→ Independent of Phase 04 (Codex). Both feed Phase 08 (cloudflared exposure) for production.
