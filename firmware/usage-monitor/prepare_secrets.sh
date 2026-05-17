#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WIFI_ENV="$ROOT_DIR/firmware/secrets/wifi.env"
API_ENV="$ROOT_DIR/usage-api/.env"
OUT="$ROOT_DIR/firmware/usage-monitor/main/secrets.h"

if [[ ! -f "$WIFI_ENV" ]]; then
  echo "missing $WIFI_ENV" >&2
  exit 1
fi
if [[ ! -f "$API_ENV" ]]; then
  echo "missing $API_ENV" >&2
  exit 1
fi

set -a
source "$WIFI_ENV"
source "$API_ENV"
set +a

: "${WIFI_SSID:?missing WIFI_SSID}"
: "${WIFI_PASSWORD:?missing WIFI_PASSWORD}"
: "${DEVICE_SECRET:?missing DEVICE_SECRET}"

escape_c() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

umask 077
cat > "$OUT" <<EOF
#pragma once

#define WIFI_SSID "$(escape_c "$WIFI_SSID")"
#define WIFI_PASSWORD "$(escape_c "$WIFI_PASSWORD")"
#define USAGE_API_URL "https://vibecode.sonpython.com/status"
#define DEVICE_SECRET "$(escape_c "$DEVICE_SECRET")"
EOF

echo "generated $OUT"
