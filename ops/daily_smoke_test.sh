#!/usr/bin/env bash
set -euo pipefail

: "${USAGE_STATUS_URL:?USAGE_STATUS_URL is required}"
: "${DEVICE_SECRET:?DEVICE_SECRET is required}"

status="$(curl -fsSL -H "X-Device-Secret: $DEVICE_SECRET" "$USAGE_STATUS_URL")"
claude_stale="$(printf '%s' "$status" | jq -r '.claude.stale_sec')"
codex_stale="$(printf '%s' "$status" | jq -r '.codex.stale_sec')"
claude_status="$(printf '%s' "$status" | jq -r '.claude.status')"
codex_status="$(printf '%s' "$status" | jq -r '.codex.status')"

degraded=0

alert() {
  local message="$1"
  if [[ -n "${ALERT_WEBHOOK:-}" ]]; then
    curl -fsSL -X POST "$ALERT_WEBHOOK" \
      -H 'Content-Type: application/json' \
      -d "{\"content\":\"$message\"}" >/dev/null
  else
    printf '%s\n' "$message" >&2
  fi
}

if [[ "$claude_status" != "ok" || "$claude_stale" -gt 300 ]]; then
  degraded=1
  alert "Claude collector degraded: status=$claude_status stale=${claude_stale}s"
fi

if [[ "$codex_status" != "ok" || "$codex_stale" -gt 900 ]]; then
  degraded=1
  alert "Codex collector degraded: status=$codex_status stale=${codex_stale}s"
fi

printf 'ok claude=%s/%ss codex=%s/%ss\n' \
  "$claude_status" "$claude_stale" "$codex_status" "$codex_stale"

exit "$degraded"
