#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from typing import Any

import httpx


def run_ccusage() -> dict[str, Any]:
    output = subprocess.check_output(
        ["npx", "-y", "ccusage@latest", "blocks", "--json"],
        text=True,
        timeout=45,
    )
    return json.loads(output)


def parse_dt(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def pct(used: float, limit: float) -> int:
    if limit <= 0:
        return 0
    return max(0, min(100, round((used / limit) * 100)))


def find_active_block(data: dict[str, Any], now: datetime) -> dict[str, Any]:
    blocks = data.get("blocks") or []
    active = data.get("activeBlock")
    if isinstance(active, dict):
        return active

    for block in blocks:
        start_raw = block.get("startTime") or block.get("start") or block.get("startsAt")
        end_raw = block.get("endTime") or block.get("end") or block.get("endsAt")
        if not start_raw or not end_raw:
            continue
        start, end = parse_dt(start_raw), parse_dt(end_raw)
        if start <= now <= end:
            return block
    if blocks:
        return blocks[-1]
    raise ValueError("ccusage returned no blocks")


def number_at(obj: dict[str, Any], *keys: str, default: float = 0) -> float:
    for key in keys:
        value = obj.get(key)
        if isinstance(value, (int, float)):
            return float(value)
    return default


def block_used_percent(block: dict[str, Any]) -> int:
    if isinstance(block.get("usedPct"), (int, float)):
        return round(float(block["usedPct"]))
    if isinstance(block.get("usageLimitPercent"), (int, float)):
        return round(float(block["usageLimitPercent"]))
    used = number_at(block, "costUSD", "totalCost", "cost")
    limit = number_at(block, "limitUSD", "usageLimit", "limit")
    return pct(used, limit)


def block_end(block: dict[str, Any]) -> str:
    for key in ("endsAt", "endTime", "end"):
        value = block.get(key)
        if isinstance(value, str):
            return parse_dt(value).isoformat()
    return (datetime.now(timezone.utc) + timedelta(hours=5)).isoformat()


def weekly_percent(data: dict[str, Any]) -> int:
    weekly = data.get("weekly")
    if isinstance(weekly, dict):
        if isinstance(weekly.get("usedPct"), (int, float)):
            return round(float(weekly["usedPct"]))
        return pct(
            number_at(weekly, "costUSD", "totalCost", "cost"),
            number_at(weekly, "limitUSD", "usageLimit", "limit"),
        )

    blocks = data.get("blocks") or []
    weekly_used = sum(number_at(b, "costUSD", "totalCost", "cost") for b in blocks)
    weekly_limit = number_at(data, "weeklyLimitUSD", "weeklyLimit", "limit")
    return pct(weekly_used, weekly_limit) if weekly_limit else 0


def weekly_end(now: datetime) -> str:
    days_until_monday = (7 - now.weekday()) % 7
    if days_until_monday == 0:
        days_until_monday = 7
    reset = (now + timedelta(days=days_until_monday)).replace(
        hour=0, minute=0, second=0, microsecond=0
    )
    return reset.isoformat()


def to_snapshot(data: dict[str, Any]) -> dict[str, Any]:
    now = datetime.now(timezone.utc)
    block = find_active_block(data, now)
    weekly = data.get("weekly") if isinstance(data.get("weekly"), dict) else {}
    weekly_reset = weekly.get("endsAt") or weekly.get("endTime") or weekly.get("resetAt")
    return {
        "current_pct": block_used_percent(block),
        "current_resets_at": block_end(block),
        "weekly_pct": weekly_percent(data),
        "weekly_resets_at": parse_dt(weekly_reset).isoformat() if weekly_reset else weekly_end(now),
        "status": "ok",
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


def main() -> int:
    try:
        push(to_snapshot(run_ccusage()))
    except Exception as exc:
        sys.stderr.write(f"collect_failed: {exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
