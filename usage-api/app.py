from __future__ import annotations

import json
import os
import sqlite3
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Request, Response, status
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from schemas import CollectIn, Source, SourceStatus, StatusOut


DB_PATH = Path(os.environ.get("USAGE_DB_PATH", "/data/usage.db"))
APP_DIR = Path(__file__).resolve().parent
STATIC_DIR = APP_DIR / "static"
STALE_AFTER_SEC = int(os.environ.get("STALE_AFTER_SEC", "600"))
SOURCES: tuple[Source, Source] = ("claude", "codex")
GMT7 = timezone(timedelta(hours=7))

app = FastAPI(title="Usage Monitor API", version="0.1.0")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


def db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS snapshots (
          source TEXT PRIMARY KEY,
          ts INTEGER NOT NULL,
          payload TEXT NOT NULL,
          status TEXT NOT NULL
        )
        """
    )
    return conn


conn = db()


def expected_token(source: Source) -> str:
    env_name = f"{source.upper()}_COLLECTOR_TOKEN"
    token = os.environ.get(env_name)
    if not token:
        raise HTTPException(status.HTTP_500_INTERNAL_SERVER_ERROR, f"{env_name} missing")
    return token


def verify_collector_token(request: Request, source: Source) -> None:
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer ") or auth.removeprefix("Bearer ") != expected_token(source):
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "invalid collector token")


def verify_device_secret(x_device_secret: str | None = Header(default=None)) -> None:
    expected = os.environ.get("DEVICE_SECRET")
    if not expected:
        raise HTTPException(status.HTTP_500_INTERNAL_SERVER_ERROR, "DEVICE_SECRET missing")
    if x_device_secret != expected:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "invalid device secret")


def seconds_until(iso_ts: str) -> int:
    if not iso_ts:
        return 0
    reset_at = datetime.fromisoformat(iso_ts)
    now = datetime.now(reset_at.tzinfo or timezone.utc)
    return max(0, int((reset_at - now).total_seconds()))


def gmt7_time(iso_ts: str) -> str:
    if not iso_ts:
        return "--:--"
    reset_at = datetime.fromisoformat(iso_ts)
    if reset_at.tzinfo is None:
        reset_at = reset_at.replace(tzinfo=timezone.utc)
    return reset_at.astimezone(GMT7).strftime("%H:%M")


def missing_status() -> SourceStatus:
    return SourceStatus(
        current_pct=-1,
        current_resets_in_sec=0,
        current_resets_at_gmt7="--:--",
        weekly_pct=-1,
        weekly_resets_in_sec=0,
        status="missing",
        stale_sec=-1,
    )


@app.post("/collect/{source}", status_code=status.HTTP_204_NO_CONTENT)
def collect(source: Source, payload: CollectIn, request: Request) -> Response:
    verify_collector_token(request, source)
    now = int(time.time())
    conn.execute(
        """
        INSERT INTO snapshots (source, ts, payload, status)
        VALUES (?, ?, ?, ?)
        ON CONFLICT(source) DO UPDATE SET
          ts = excluded.ts,
          payload = excluded.payload,
          status = excluded.status
        """,
        (source, now, payload.model_dump_json(), payload.status),
    )
    conn.commit()
    return Response(status_code=status.HTTP_204_NO_CONTENT)


@app.get("/healthz")
def healthz() -> dict[str, str]:
    return {"status": "ok"}


def build_status() -> StatusOut:
    rows = {
        row["source"]: row
        for row in conn.execute("SELECT source, ts, payload, status FROM snapshots").fetchall()
    }
    now = int(time.time())
    result: dict[str, SourceStatus] = {}

    for source in SOURCES:
        row = rows.get(source)
        if row is None:
            result[source] = missing_status()
            continue

        payload = json.loads(row["payload"])
        stale_sec = max(0, now - int(row["ts"]))
        source_status = row["status"] if stale_sec <= STALE_AFTER_SEC else "stale"
        result[source] = SourceStatus(
            current_pct=payload["current_pct"],
            current_resets_in_sec=seconds_until(payload["current_resets_at"]),
            current_resets_at_gmt7=gmt7_time(payload["current_resets_at"]),
            weekly_pct=payload["weekly_pct"],
            weekly_resets_in_sec=seconds_until(payload["weekly_resets_at"]),
            status=source_status,
            stale_sec=stale_sec,
        )

    return StatusOut(ts=datetime.now(timezone.utc).isoformat(), **result)


@app.get("/", include_in_schema=False)
def root() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/public/status", response_model=StatusOut)
def public_status() -> StatusOut:
    return build_status()


@app.get("/status", response_model=StatusOut, dependencies=[Depends(verify_device_secret)])
def get_status() -> StatusOut:
    return build_status()
