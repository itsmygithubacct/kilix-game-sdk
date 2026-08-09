"""Validate a game entry through the authoritative kilix-content package."""

from __future__ import annotations

from pathlib import Path
import re

from .common import ToolError


GAME_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


def validate_catalog(path: Path, game_id: str) -> tuple[str, str]:
    if not isinstance(game_id, str) or GAME_ID.fullmatch(game_id) is None:
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
