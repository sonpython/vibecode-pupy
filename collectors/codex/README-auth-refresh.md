# Codex Auth Refresh

`codex_auth.json` is equivalent to an active ChatGPT session. Keep it out of git.

## Extract

1. Open `https://chatgpt.com` in a logged-in browser.
2. Open DevTools Network tab.
3. Trigger or reload a request to `/backend-api/wham/usage`.
4. Right click the request and copy as cURL.
5. Convert it:

```bash
pbpaste | python collectors/codex/parse_curl.py > secrets/codex_auth.json
```

6. Restart the collector container:

```bash
docker compose -f usage-api/docker-compose.yml up -d --build codex-collector
```

## Verify

```bash
docker compose -f usage-api/docker-compose.yml logs --tail=50 codex-collector
curl -H "X-Device-Secret: $DEVICE_SECRET" http://127.0.0.1:8080/status | jq .codex
```
