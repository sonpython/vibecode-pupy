# AGENTS.md

Project instructions for Codex and other AI agents working in this workspace.

## Startup Handoff Protocol

Before doing any task in this workspace, run this ramp-up sequence:

1. Try `git status --short --branch` and `git pull --ff-only` if this directory is a git repo.
   - If there is no `.git`, state that the workspace is not a git repo and continue with local files.
2. Read these files in order:
   - `docs/codex-memory.md` — newest 3 entries
   - `docs/session-sync.md` — newest Claude handoff entry
   - `docs/app-journey-story.md` — current project arc and active state
   - `plans/260517-1045-esp32-usage-monitor/plan.md` — active plan
   - `CLAUDE.md` and `.claude/rules/*.md` — project workflow and rules
3. Confirm the current state in one concise line before implementation.
4. If the task changes project state, prepend a new entry to `docs/codex-memory.md` before final response.

## Current State Snapshot

- Workspace: `vibecode-pupy`, personal AI tooling + hardware sandbox.
- Active project: `esp32-usage-monitor`.
- Status: planned, ready for Phase 01.
- Hardware: Xiaozhi ESP32-S3 N16R8, MAC `a0:f2:62:e8:a4:40`, TTY `/dev/cu.usbmodem83101`.
- Firmware state: factory firmware was backed up and flash was erased.
- Active plan: `plans/260517-1045-esp32-usage-monitor/`.

## ClaudeKit Mapping For Codex

ClaudeKit project assets live in `.claude/`. Codex-compatible copies are mapped under:

- Skills: `~/.codex/skills/`
- ClaudeKit agents/scripts/config: `~/.codex/claudekit/`
- ClaudeKit rules: `~/.codex/rules/claudekit/`
- Plan templates: `~/.codex/templates/claudekit/plans/`

When a task matches an available skill, use that skill. Keep ClaudeKit source files in this repo as the source of truth for this workspace unless the user asks to update global Codex files.

## Development Rules

- Follow `.claude/rules/development-rules.md`: YAGNI, KISS, DRY.
- Use kebab-case for new file names.
- Keep code files under 200 LoC when practical.
- Do not commit secrets, tokens, cookies, JWTs, or device credentials.
- Do not edit `~/.claude/` for this project unless explicitly asked.
- Prefer updating existing files over creating duplicate enhanced variants.
- For ESP32 work, keep full flash backups before erase/write operations.

## Memory Logging

Use `docs/codex-memory.md` as the cross-agent log.

- Prepend new entries below `<!-- ENTRY MARKER — agents prepend here -->`.
- Do not edit old entries.
- Use actor `codex-cli` for local Codex CLI work.
- If the workspace has no git repo, record `Branch: (no git yet — project not init'd)`.
