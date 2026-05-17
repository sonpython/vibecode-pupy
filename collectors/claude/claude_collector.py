#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import httpx


AUTH_FILE = Path(os.environ.get("CLAUDE_AUTH_FILE", "/secrets/claude_auth.json"))
WEBHOOK = os.environ.get("AUTH_ALERT_WEBHOOK", "")
INTERVAL = int(os.environ.get("POLL_INTERVAL_SEC", "300"))


def load_auth() -> dict[str, Any]:
    with AUTH_FILE.open() as fh:
        return json.load(fh)


def fetch_usage(auth: dict[str, Any]) -> httpx.Response:
    return httpx.get(
        auth["usage_url"],
        headers=auth.get("headers", {}),
        cookies=auth.get("cookies", {}),
        timeout=15,
    )


def parse_dt(value: str | None) -> str:
    if not value:
        return datetime.now(timezone.utc).isoformat()
    return datetime.fromisoformat(value.replace("Z", "+00:00")).isoformat()


def percent(obj: dict[str, Any] | None) -> int:
    if not isinstance(obj, dict):
        return 0
    value = obj.get("utilization")
    if not isinstance(value, (int, float)):
        return 0
    return max(0, min(100, round(float(value))))


def to_snapshot(payload: dict[str, Any]) -> dict[str, Any]:
    five_hour = payload.get("five_hour") or {}
    seven_day = payload.get("seven_day") or {}
    return {
        "current_pct": percent(five_hour),
        "current_resets_at": parse_dt(five_hour.get("resets_at")),
        "weekly_pct": percent(seven_day),
        "weekly_resets_at": parse_dt(seven_day.get("resets_at")),
        "status": "ok",
    }


def auth_expired_snapshot() -> dict[str, Any]:
    now = datetime.now(timezone.utc).isoformat()
    return {
        "current_pct": -1,
        "current_resets_at": now,
        "weekly_pct": -1,
        "weekly_resets_at": now,
        "status": "auth_expired",
    }


def push(snapshot: dict[str, Any]) -> None:
    api_url = os.environ["USAGE_API_URL"].rstrip("/")
    token = os.environ["CLAUDE_COLLECTOR_TOKEN"]
    response = httpx.post(
        f"{api_url}/collect/claude",
        json=snapshot,
        headers={"Authorization": f"Bearer {token}"},
        timeout=10,
    )
    response.raise_for_status()


def alert(message: str) -> None:
    if not WEBHOOK:
        return
    try:
        httpx.post(WEBHOOK, json={"content": message}, timeout=5).raise_for_status()
    except Exception as exc:
        sys.stderr.write(f"alert_failed: {exc}\n")


def run_once() -> None:
    response = fetch_usage(load_auth())
    if response.status_code in {401, 403}:
        push(auth_expired_snapshot())
        alert("Claude auth expired. Refresh claude.ai cookies.")
        return
    if response.status_code != 200:
        raise RuntimeError(f"unexpected {response.status_code}: {response.text[:200]}")
    push(to_snapshot(response.json()))


def loop() -> int:
    while True:
        try:
            run_once()
        except Exception as exc:
            sys.stderr.write(f"loop_error: {exc}\n")
        time.sleep(INTERVAL)


if __name__ == "__main__":
    raise SystemExit(loop())
