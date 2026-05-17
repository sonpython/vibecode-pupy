from __future__ import annotations

from claude_collector import to_snapshot
from parse_claude_curl import parse_curl


def test_to_snapshot_from_claude_usage_api():
    snap = to_snapshot(
        {
            "five_hour": {
                "utilization": 2.0,
                "resets_at": "2026-05-17T12:09:59.852306+00:00",
            },
            "seven_day": {
                "utilization": 5.0,
                "resets_at": "2026-05-17T21:00:00.852332+00:00",
            },
        }
    )

    assert snap == {
        "current_pct": 2,
        "current_resets_at": "2026-05-17T12:09:59.852306+00:00",
        "weekly_pct": 5,
        "weekly_resets_at": "2026-05-17T21:00:00.852332+00:00",
        "status": "ok",
    }


def test_parse_curl_keeps_claude_auth_fields():
    auth = parse_curl(
        """curl 'https://claude.ai/api/organizations/org_123/usage' \
          -H 'anthropic-client-platform: web_claude_ai' \
          -H 'anthropic-device-id: device-123' \
          -H 'user-agent: TestBrowser' \
          -b 'sessionKey=sk-test; cf_clearance=cf-test'"""
    )

    assert auth["usage_url"] == "https://claude.ai/api/organizations/org_123/usage"
    assert auth["cookies"] == {"sessionKey": "sk-test", "cf_clearance": "cf-test"}
    assert auth["headers"] == {
        "anthropic-client-platform": "web_claude_ai",
        "anthropic-device-id": "device-123",
        "user-agent": "TestBrowser",
    }
