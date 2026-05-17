from __future__ import annotations

import importlib
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def make_client(monkeypatch, tmp_path) -> TestClient:
    monkeypatch.setenv("USAGE_DB_PATH", str(tmp_path / "usage.db"))
    monkeypatch.setenv("CLAUDE_COLLECTOR_TOKEN", "claude-token")
    monkeypatch.setenv("CODEX_COLLECTOR_TOKEN", "codex-token")
    monkeypatch.setenv("DEVICE_SECRET", "device-secret")
    monkeypatch.setenv("STALE_AFTER_SEC", "600")
    import app

    importlib.reload(app)
    return TestClient(app.app)


def snapshot(current: int = 50) -> dict[str, object]:
    now = datetime.now(timezone.utc)
    return {
        "current_pct": current,
        "weekly_pct": 11,
        "current_resets_at": (now + timedelta(hours=1)).isoformat(),
        "weekly_resets_at": (now + timedelta(days=6)).isoformat(),
        "status": "ok",
    }


def test_collect_and_status(monkeypatch, tmp_path):
    client = make_client(monkeypatch, tmp_path)

    res = client.post(
        "/collect/claude",
        headers={"Authorization": "Bearer claude-token"},
        json=snapshot(),
    )
    assert res.status_code == 204

    res = client.get("/status", headers={"X-Device-Secret": "device-secret"})
    assert res.status_code == 200
    body = res.json()
    assert body["claude"]["current_pct"] == 50
    assert body["claude"]["current_resets_at_gmt7"] != "--:--"
    assert body["claude"]["status"] == "ok"
    assert body["codex"]["status"] == "missing"
    assert body["codex"]["current_resets_at_gmt7"] == "--:--"


def test_auth_failures(monkeypatch, tmp_path):
    client = make_client(monkeypatch, tmp_path)

    assert client.post("/collect/claude", json=snapshot()).status_code == 401
    assert client.get("/status").status_code == 401


def test_root_serves_usage_monitor_ui(monkeypatch, tmp_path):
    client = make_client(monkeypatch, tmp_path)

    res = client.get("/")

    assert res.status_code == 200
    assert "Claude + Codex" in res.text
    assert "/static/app.js" in res.text


def test_public_status_does_not_require_device_secret(monkeypatch, tmp_path):
    client = make_client(monkeypatch, tmp_path)
    client.post(
        "/collect/codex",
        headers={"Authorization": "Bearer codex-token"},
        json=snapshot(42),
    )

    res = client.get("/public/status")

    assert res.status_code == 200
    assert res.json()["codex"]["current_pct"] == 42


def test_stale_snapshot(monkeypatch, tmp_path):
    client = make_client(monkeypatch, tmp_path)
    client.post(
        "/collect/codex",
        headers={"Authorization": "Bearer codex-token"},
        json=snapshot(5),
    )

    import app

    app.conn.execute("UPDATE snapshots SET ts = ts - 601 WHERE source = 'codex'")
    app.conn.commit()

    res = client.get("/status", headers={"X-Device-Secret": "device-secret"})
    assert res.json()["codex"]["status"] == "stale"
