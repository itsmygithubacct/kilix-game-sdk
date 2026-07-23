"""Validate a game entry through the authoritative kilix-content package."""

from __future__ import annotations

from pathlib import Path

from .common import ToolError


def validate_catalog(path: Path, game_id: str) -> tuple[str, str]:
    if not game_id or "/" in game_id or game_id in (".", ".."):
        raise ToolError("invalid game ID")
    try:
        from kilix_content import Catalog
    except ImportError as exc:
        raise ToolError(
            "kilix_content is unavailable; add its src directory to PYTHONPATH"
        ) from exc
    try:
        catalog = Catalog.load(path)
        entry = catalog.require(game_id)
    except (OSError, ValueError, KeyError) as exc:
        raise ToolError(f"catalog validation failed: {exc}") from exc
    if entry.kind != "game" or entry.binary != game_id:
        raise ToolError("catalog entry does not describe the game binary")
    if (
        entry.launch_mode != "terminal"
        or "kitty-graphics" not in entry.capabilities
    ):
        raise ToolError("catalog entry is missing terminal capabilities")
    return entry.content_id, entry.launch_mode
