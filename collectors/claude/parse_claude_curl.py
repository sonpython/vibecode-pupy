#!/usr/bin/env python3
from __future__ import annotations

import json
import shlex
import sys
from http.cookies import SimpleCookie
from urllib.parse import urlparse


KEEP_HEADERS = {
    "accept",
    "accept-language",
    "anthropic-anonymous-id",
    "anthropic-client-platform",
    "anthropic-client-sha",
    "anthropic-client-version",
    "anthropic-device-id",
    "content-type",
    "referer",
    "user-agent",
    "x-activity-session-id",
}


def parse_curl(command: str) -> dict[str, object]:
    parts = shlex.split(command)
    headers: dict[str, str] = {}
    cookies: dict[str, str] = {}
    usage_url = ""

    for part in parts:
        if part.startswith("https://claude.ai/api/organizations/") and "/usage" in part:
            usage_url = part
            break

    i = 0
    while i < len(parts):
        if parts[i] in ("-H", "--header") and i + 1 < len(parts):
            raw = parts[i + 1]
            if ":" in raw:
                key, value = raw.split(":", 1)
                key_l = key.strip().lower()
                value = value.strip()
                if key_l == "cookie":
                    jar = SimpleCookie()
                    jar.load(value)
                    cookies.update({name: morsel.value for name, morsel in jar.items()})
                elif key_l in KEEP_HEADERS:
                    headers[key_l] = value
            i += 2
            continue
        if parts[i] in ("-b", "--cookie", "--cookie-jar") and i + 1 < len(parts):
            jar = SimpleCookie()
            jar.load(parts[i + 1])
            cookies.update({name: morsel.value for name, morsel in jar.items()})
            i += 2
            continue
        i += 1

    if not usage_url:
        raise SystemExit("missing claude.ai organization usage URL")
    parsed = urlparse(usage_url)
    if parsed.scheme != "https" or parsed.netloc != "claude.ai":
        raise SystemExit("usage URL must be https://claude.ai/...")
    if not cookies:
        raise SystemExit("missing Cookie header")
    return {"usage_url": usage_url, "cookies": cookies, "headers": headers}


def main() -> int:
    command = sys.stdin.read().strip()
    print(json.dumps(parse_curl(command), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
