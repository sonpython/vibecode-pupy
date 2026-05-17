from __future__ import annotations

from datetime import datetime, timedelta, timezone

from claude_collector import to_snapshot


def test_to_snapshot_with_active_block():
    now = datetime.now(timezone.utc)
    data = {
        "blocks": [
            {
                "startTime": (now - timedelta(hours=1)).isoformat(),
                "endTime": (now + timedelta(hours=4)).isoformat(),
                "costUSD": 2,
                "limitUSD": 10,
            }
        ],
        "weekly": {
            "costUSD": 3,
            "limitUSD": 20,
            "endsAt": (now + timedelta(days=3)).isoformat(),
        },
    }

    snap = to_snapshot(data)

    assert snap["current_pct"] == 20
    assert snap["weekly_pct"] == 15
    assert snap["status"] == "ok"
