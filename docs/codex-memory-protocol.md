# Codex ↔ Claude Memory Protocol

> **Mục tiêu**: Cross-environment memory sync giữa Codex (GPT) và Claude Code, để user switch tool bất kỳ lúc nào mà KHÔNG mất context.
> **Audience**: AI agents (Codex, Claude, OpenCode, other) + user.

---

## Architecture

```
                  ┌─────────────────────────┐
                  │     Git repository       │
                  │     (origin/main)        │
                  └────────────┬─────────────┘
                               │
       ┌───────────────────────┼───────────────────────┐
       │                       │                       │
┌──────▼────────┐     ┌────────▼─────────┐    ┌────────▼────────┐
│ Claude        │     │ Codex / Other    │    │ Cloud agents    │
│               │     │                  │    │ (routines, cron)│
│ writes:       │     │ writes:          │    │ writes:         │
│ session-sync  │     │ codex-memory     │    │ codex-memory    │
│ + codex-mem   │     │                  │    │ (brief format)  │
│               │     │                  │    │                 │
│ reads all     │     │ reads all        │    │ reads all       │
└───────────────┘     └──────────────────┘    └─────────────────┘
```

### Files in protocol

| File | Owner | Sync |
|---|---|---|
| `docs/codex-memory.md` | All agents (append) | git |
| `docs/session-sync.md` | Claude (append) | git |
| `docs/app-journey-story.md` | Anyone updating | git |
| `AGENTS.md` (root) | Both Claude + Codex | git |
| `CLAUDE.md` (root) | Claude | git |
| `~/.claude/projects/.../memory/*.md` | Claude local only | **NOT in git** |

---

## Session lifecycle

### Any agent session START
1. `git pull`
2. Read `docs/codex-memory.md` top 3 entries
3. Read `docs/app-journey-story.md` if first time
4. Read `docs/session-sync.md` top 1 entry (gap analysis)
5. Confirm understanding with user (1-line summary)

### Any agent session END
1. Prepend entry to `docs/codex-memory.md`
2. (Claude only) Also prepend to `docs/session-sync.md`
3. (Optional) Update `docs/app-journey-story.md` Section 7 if state changed
4. `git add` + `git commit -m "memory: <slug>"` + `git push`

---

## Conflict resolution

### Same file edited by 2 agents
- Standard git merge
- Trust commit history

### State contradicts between files
- File with newer commit timestamp wins
- Fallback: activity DB if exists

### Agent didn't see latest entry
- Cause: session start before push
- Fix: rerun `git pull` mid-session

---

## Trigger rules (when to write)

**MUST write entry when:**
- Hoàn thành task user request
- Trước khi exit session (final flush)
- Sau khi modify SOT files
- Trước khi switch project/branch

**CAN SKIP when:**
- Pure read-only query
- User explicitly "không cần log"
- Task < 1 phút không state change

---

## Schema versioning

Current: **v1.0** ({{DATE}})

Khi schema thay đổi:
1. Bump version trong header `codex-memory.md`
2. Document breaking changes ở entry mới
3. KHÔNG migrate entries cũ

---

## Security & privacy

❌ **NEVER log**:
- API keys, tokens, passwords, OAuth secrets
- HR sensitive (salary, probation, performance)
- Customer PII raw

✅ **OK to log**:
- Project status, decisions, technical actions
- Public deliverables, deadlines
- Workstream coordination

---

## Implementation checklist

- [ ] `docs/codex-memory.md` exists (template + first entry)
- [ ] `docs/codex-memory-protocol.md` (this file)
- [ ] `docs/app-journey-story.md` filled (Sections 1-7)
- [ ] `AGENTS.md` (root) — Codex auto-load
- [ ] `CLAUDE.md` (root) — Claude rules
- [ ] Skill `memory-bridge` installed at `~/.claude/skills/memory-bridge/`
- [ ] Global rule `~/.claude/rules/memory-bridge-auto-load.md` linked from user CLAUDE.md
