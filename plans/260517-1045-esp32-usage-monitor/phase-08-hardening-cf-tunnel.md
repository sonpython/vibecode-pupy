---
phase: 8
title: "Hardening and Cloudflare Tunnel"
status: scaffolded
priority: P2
effort: "0.5d"
dependencies: [2, 3, 4]
---

# Phase 08: Hardening + Cloudflare Tunnel

## Overview
Expose Usage-API publicly via Cloudflare Tunnel so the ESP32 talks to a real HTTPS endpoint regardless of network. Rotate all dev tokens to production values. Add daily smoke-test cron that verifies both collectors + ESP32 path are alive, alerts on degradation. Write the README that lets future-you onboard a second device.

## Context Links
- Phases 02, 03, 04 must be operational on loopback first
- Brainstorm §5.3 cloudflared decision

## Requirements
### Functional
- `cloudflared` container in same docker-compose, points to `usage-api:8080`
- Public hostname `usage.<userdomain>.com` resolves to tunnel
- Cloudflare Access policy (optional) restricts who can reach it (device service token + your email)
- Daily smoke test: cron on Mac runs end-to-end check; failure → webhook alert
- Production token rotation: all dev `*_TOKEN` values regenerated, dev values invalidated
- README documents: cold-start install, token rotation, auth refresh, troubleshooting

### Non-functional
- Tunnel adds <300ms p95 latency
- Cloudflare Tunnel container restart-resilient
- All secrets stay out of git

## Architecture

```
ESP32 ──HTTPS──> usage.<domain>.com (Cloudflare edge)
                          │
                          └─Tunnel─► cloudflared container (Docker 192.168.1.120)
                                              │
                                              └─► usage-api:8080 (Docker network)
```

## Related Code Files
- Modify: `usage-api/docker-compose.yml` (add `cloudflared` service)
- Create: `usage-api/cloudflared/config.yml`
- Create: `usage-api/cloudflared/credentials.json.example`
- Create: `ops/daily_smoke_test.sh` (Mac launchd cron)
- Create: `ops/com.user.usage-smoke.plist` (launchd)
- Create: `README.md` (top-level project, ~250 lines)

## Implementation Steps

1. **Cloudflare side**:
   - Cloudflare Zero Trust dashboard → Networks → Tunnels → Create tunnel `usage-monitor`
   - Download credentials JSON, save to `usage-api/cloudflared/<tunnel-id>.json` (gitignored)
   - Public hostname: `usage.<domain>` → service `http://usage-api:8080`

2. **cloudflared container**:
   ```yaml
   cloudflared:
     image: cloudflare/cloudflared:latest
     restart: unless-stopped
     command: tunnel --no-autoupdate run --token ${CF_TUNNEL_TOKEN}
     depends_on: [usage-api]
   ```
   (Token-based config is the simpler path; cert-based with `config.yml` also acceptable.)

3. **Rotate tokens** (all):
   - Generate: `CLAUDE_COLLECTOR_TOKEN`, `CODEX_COLLECTOR_TOKEN`, `DEVICE_SECRET`
   - Update `.env` on Docker host
   - Update launchd plist on Mac (Claude collector token)
   - Update Codex container `.env`
   - Restart all services
   - Verify old tokens fail with 401

4. **Cloudflare Access (optional but recommended)**:
   - Application: `usage.<domain>/*`
   - Policy: service token for ESP32 device + email rule for you
   - ESP32 sends `CF-Access-Client-Id` + `CF-Access-Client-Secret` headers (additional auth layer above DEVICE_SECRET)

5. **Daily smoke test** (`ops/daily_smoke_test.sh`):
   ```bash
   #!/usr/bin/env bash
   set -e
   STATUS=$(curl -fsSL -H "X-Device-Secret: $DEVICE_SECRET" \
     "https://usage.$DOMAIN/status")
   CLAUDE_STALE=$(echo "$STATUS" | jq '.claude.stale_sec')
   CODEX_STALE=$(echo  "$STATUS" | jq '.codex.stale_sec')
   if [ "$CLAUDE_STALE" -gt 300 ]; then curl "$ALERT_WEBHOOK" -d "{\"content\":\"Claude collector stale: ${CLAUDE_STALE}s\"}"; fi
   if [ "$CODEX_STALE"  -gt 900 ]; then curl "$ALERT_WEBHOOK" -d "{\"content\":\"Codex collector stale: ${CODEX_STALE}s\"}"; fi
   ```

6. **launchd plist** for smoke test — daily at 09:00 local time.

7. **README** sections:
   - Architecture diagram
   - Quick-start (clone, secrets, docker compose up)
   - Wifi provisioning instructions for ESP32 (first-boot captive portal SSID)
   - Codex auth refresh procedure
   - Common failure modes + fixes
   - How to add a second device (new DEVICE_SECRET, re-flash)

## Todo List
- [ ] Cloudflare tunnel created + DNS hostname
- [ ] cloudflared container running, public URL reachable
- [ ] All tokens rotated to production values, dev values invalidated
- [ ] Daily smoke test cron operational
- [ ] README complete enough for cold restart
- [ ] One full end-to-end test: power-cycle ESP32 → captive portal → join WiFi → button press → real public-route status visible on screen

## Success Criteria
- [ ] `curl https://usage.<domain>/status -H X-Device-Secret: …` returns latest snapshot from public internet
- [ ] ESP32 (on any network) can hit it
- [ ] Smoke-test alert proven by deliberately stopping a collector for >10min
- [ ] README enables fresh setup in <1 hour by future-you

## Risk Assessment
| Risk | Mitigation |
|---|---|
| Cloudflare Tunnel quota limits (free tier 50 users) | Personal device only; well within limits |
| ESP32 ESP-IDF cert bundle missing Cloudflare root | Use full cert bundle (~250KB) or pin Cloudflare ECC root |
| `cloudflared` updates auto-break tunnel | Pin image tag (`cloudflare/cloudflared:2026.04.0`); review monthly |
| DNS propagation delay | Trivial — test from external network only after `dig` returns expected IPs |

## Security Considerations
- Cloudflare Access service token = additional layer; protects against random scanners
- All secrets in `.env`, never committed
- Periodically rotate `DEVICE_SECRET` (firmware re-flash required — accept ~quarterly cadence)
- Smoke-test webhook URL also a secret

## Next Steps
→ Final acceptance test from cold restart per README. Then journal phase via `/ck:journal`.
