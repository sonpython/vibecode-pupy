#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
plist_src="$project_dir/ops/com.user.usage-smoke.plist"
plist_dst="$HOME/Library/LaunchAgents/com.user.usage-smoke.plist"

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
sed \
  -e "s#__PROJECT_DIR__#$project_dir#g" \
  -e "s#__HOME__#$HOME#g" \
  "$plist_src" > "$plist_dst"

launchctl unload "$plist_dst" 2>/dev/null || true
launchctl load "$plist_dst"
launchctl list | grep com.user.usage-smoke || true
