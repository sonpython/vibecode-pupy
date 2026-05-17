from __future__ import annotations

from codex_collector import to_snapshot
from parse_curl import parse_curl


def test_to_snapshot():
    snap = to_snapshot(
        {
            "rate_limit": {
                "primary_window": {"used_percent": 12.4, "reset_at": 1760000000},
                "secondary_window": {"used_percent": 55.6, "reset_at": 1760500000},
            }
        }
    )

    assert snap["current_pct"] == 12
    assert snap["weekly_pct"] == 56
    assert snap["status"] == "ok"


def test_parse_curl():
    parsed = parse_curl(
        "curl 'https://chatgpt.com/backend-api/wham/usage' "
        "-H 'Authorization: Bearer abc.def' "
        "-H 'Cookie: cf_clearance=clear; _puid=puid' "
        "-H 'User-Agent: Test UA'"
    )

    assert parsed["bearer"] == "abc.def"
    assert parsed["cookies"]["cf_clearance"] == "clear"
    assert parsed["headers"]["user-agent"] == "Test UA"
