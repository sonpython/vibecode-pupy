#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Any

from playwright.async_api import async_playwright


AUTH_FILE = Path(os.environ.get("CLAUDE_AUTH_FILE", "/secrets/claude_auth.json"))
INTERVAL = int(os.environ.get("KEEPALIVE_INTERVAL_SEC", "900"))
TARGET_URL = os.environ.get("CLAUDE_KEEPALIVE_URL", "https://claude.ai/settings/usage")


def load_auth() -> dict[str, Any]:
    with AUTH_FILE.open() as fh:
        return json.load(fh)


def cookie_objects(cookies: dict[str, str]) -> list[dict[str, Any]]:
    return [
        {
            "name": name,
            "value": value,
            "url": "https://claude.ai",
            "secure": True,
            "httpOnly": name in {"sessionKey", "cf_clearance"} or name.startswith("__"),
            "sameSite": "Lax",
        }
        for name, value in cookies.items()
        if value
    ]


async def keepalive_once(auth: dict[str, Any]) -> None:
    headers = auth.get("headers", {})
    user_agent = headers.get("user-agent")
    extra_headers = {
        key: value
        for key, value in headers.items()
        if key.lower() not in {"cookie", "host", "content-length", "user-agent"}
    }

    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        context = await browser.new_context(
            user_agent=user_agent,
            extra_http_headers=extra_headers,
        )
        await context.add_cookies(cookie_objects(auth.get("cookies", {})))
        page = await context.new_page()

        response = await page.goto(TARGET_URL, wait_until="domcontentloaded", timeout=30000)
        status = response.status if response else 0
        usage_response = await page.request.get(auth["usage_url"], timeout=30000)
        sys.stdout.write(
            f"keepalive page_status={status} usage_status={usage_response.status}\n"
        )
        await browser.close()


async def main() -> int:
    while True:
        try:
            await keepalive_once(load_auth())
        except Exception as exc:
            sys.stderr.write(f"keepalive_failed: {exc}\n")
        await asyncio.sleep(INTERVAL)


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
