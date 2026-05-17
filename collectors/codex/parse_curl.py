#!/usr/bin/env python3
from __future__ import annotations

import json
import shlex
import sys
from http.cookies import SimpleCookie


KEEP_HEADERS = {
    "oai-device-id",
    "oai-client-version",
    "user-agent",
    "referer",
    "origin",
}


def parse_curl(command: str) -> dict[str, object]:
    parts = shlex.split(command)
    headers: dict[str, str] = {}
    cookies: dict[str, str] = {}
    bearer = ""

    i = 0
    while i < len(parts):
        if parts[i] in ("-H", "--header") and i + 1 < len(parts):
            raw = parts[i + 1]
            if ":" in raw:
                key, value = raw.split(":", 1)
                key_l = key.strip().lower()
                value = value.strip()
                if key_l == "authorization" and value.lower().startswith("bearer "):
                    bearer = value.split(" ", 1)[1]
                elif key_l == "cookie":
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

    if not bearer:
        raise SystemExit("missing Authorization: Bearer header")
    if not cookies:
        raise SystemExit("missing Cookie header")
    return {"bearer": bearer, "cookies": cookies, "headers": headers}


def main() -> int:
    command = sys.stdin.read().strip()
    print(json.dumps(parse_curl(command), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
