# Starter Prompt (copy-paste cho web AI agents)

> **Khi nào dùng**: ChatGPT web Codex (codex.openai.com), ChatGPT thường, hoặc bất kỳ AI tool nào không auto-load `AGENTS.md`.
> **Cách dùng**: Copy nguyên block, paste vào turn đầu của session.

---

## Block 1 — Ramp-up prompt (PASTE THIS)

```
Bạn là AI agent làm việc trên project {{PROJECT_NAME}}. Repo: {{REPO_URL}}.

TRƯỚC KHI làm bất kỳ task gì, đọc 4 files này theo thứ tự (git pull trước):

1. AGENTS.md (root) — project rules + session lifecycle
2. docs/app-journey-story.md — project arc + active state (BẮT BUỘC)
3. docs/codex-memory.md — top 3 entries (xem agent khác làm gì gần nhất)
4. docs/session-sync.md — top 1 entry (Claude làm gì gần nhất)

Sau khi đọc xong, confirm hiểu state bằng 1-line summary.

CRITICAL RULES:
- {{Identity SOT or other project-specific rules}}
- Date format: absolute ISO (YYYY-MM-DD), không relative
- KHÔNG edit ~/.claude/ (Claude memory local)
- KHÔNG commit secrets

KẾT THÚC SESSION:
1. Append entry vào docs/codex-memory.md (prepend top — format trong file)
2. git add + commit -m "memory: <slug>" + push
3. Tell user "Done. Logged."

Xác nhận đã đọc và sẵn sàng nhận task.
```

---

## Block 2 — Task prompt (sau Block 1)

```
Task: <mô tả task chi tiết>
Acceptance criteria:
- <criteria 1>
Files cần touch:
- <path>
Deadline: <date>

Khi xong, log entry + commit + push.
```

---

## Block 3 — End session prompt

```
Session ending. Vui lòng:
1. Append entry vào docs/codex-memory.md
2. Fill format: Actor=codex-web | Branch | Trigger | ✅ Done | 📁 Files changed | 🚨 Follow-ups
3. git add + commit -m "memory: <slug>" + push
4. Trả về commit hash + 1-line summary

Nếu environment KHÔNG có git access (sandbox), output entry content cho user copy + commit local.
```

---

## Web Codex constraints

- **Plus/Pro plan + GitHub OAuth**: direct push
- **Sandbox-only**: output content → user paste local + commit

Block 3 covers both.

---

## ChatGPT (không phải Codex)

Không có file system. Paste context inline:

```
[Paste AGENTS.md content]
[Paste top 3 entries codex-memory.md]
[Paste TL;DR + Section 7 app-journey-story.md]

Bạn là AI agent... [rest of Block 1]
```

---

## OpenCode CLI

Add vào `opencode.json`:
```json
{
  "instructions": [
    "AGENTS.md",
    "docs/app-journey-story.md",
    "docs/codex-memory.md"
  ]
}
```
