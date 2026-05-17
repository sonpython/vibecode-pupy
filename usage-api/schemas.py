from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator


Source = Literal["claude", "codex"]
SnapshotStatus = Literal["ok", "auth_expired", "error"]
DisplayStatus = Literal["ok", "auth_expired", "error", "stale", "missing"]


class CollectIn(BaseModel):
    model_config = ConfigDict(extra="forbid")

    current_pct: int = Field(ge=-1, le=100)
    weekly_pct: int = Field(ge=-1, le=100)
    current_resets_at: str
    weekly_resets_at: str
    status: SnapshotStatus = "ok"

    @field_validator("current_resets_at", "weekly_resets_at")
    @classmethod
    def validate_reset_time(cls, value: str) -> str:
        if value == "":
            return value
        datetime.fromisoformat(value)
        return value


class SourceStatus(BaseModel):
    current_pct: int
    current_resets_in_sec: int
    current_resets_at_gmt7: str
    weekly_pct: int
    weekly_resets_in_sec: int
    status: DisplayStatus
    stale_sec: int


class StatusOut(BaseModel):
    ts: str
    claude: SourceStatus
    codex: SourceStatus
