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


ENDPOINT = "https://chatgpt.com/backend-api/wham/usage"
AUTH_FILE = Path(os.environ.get("CODEX_AUTH_FILE", "/secrets/codex_auth.json"))
WEBHOOK = os.environ.get("AUTH_ALERT_WEBHOOK", "")
INTERVAL = int(os.environ.get("POLL_INTERVAL_SEC", "300"))


def load_auth() -> dict[str, Any]:
    with AUTH_FILE.open() as fh:
        return json.load(fh)


def fetch_usage(auth: dict[str, Any]) -> httpx.Response:
    headers = {
        "Authorization": f"Bearer {auth['bearer']}",
        **auth.get("headers", {}),
    }
    return httpx.get(
        ENDPOINT,
        headers=headers,
        cookies=auth.get("cookies", {}),
        timeout=15,
    )


def iso_from_unix(value: int | float) -> str:
    return datetime.fromtimestamp(value, tz=timezone.utc).isoformat()


def to_snapshot(payload: dict[str, Any]) -> dict[str, Any]:
    rate_limit = payload["rate_limit"]
    primary = rate_limit["primary_window"]
    secondary = rate_limit["secondary_window"]
    return {
        "current_pct": round(float(primary["used_percent"])),
        "current_resets_at": iso_from_unix(primary["reset_at"]),
        "weekly_pct": round(float(secondary["used_percent"])),
        "weekly_resets_at": iso_from_unix(secondary["reset_at"]),
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
    token = os.environ["CODEX_COLLECTOR_TOKEN"]
    response = httpx.post(
        f"{api_url}/collect/codex",
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
    if response.status_code == 401:
        push(auth_expired_snapshot())
        alert("Codex auth expired. Refresh ChatGPT cookies and bearer token.")
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
