"""Bounded filesystem and JSON helpers shared by game authoring tools."""

from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
from typing import Any


class ToolError(ValueError):
    """A deterministic validation failure suitable for command-line output."""


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ToolError(f"cannot read JSON object {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ToolError(f"{path}: expected a JSON object")
    return value


def safe_relative_path(value: object, *, suffix: str | None = None) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise ToolError("path must be a non-empty string")
    logical = PurePosixPath(value)
    if logical.is_absolute() or "." in logical.parts or ".." in logical.parts:
        raise ToolError(f"unsafe relative path: {value}")
    if suffix is not None and logical.suffix.lower() != suffix.lower():
        raise ToolError(f"path must end in {suffix}: {value}")
    return logical


def resolve_regular_file(root: Path, value: object, *, suffix: str | None = None) -> Path:
    logical = safe_relative_path(value, suffix=suffix)
    path = root.joinpath(*logical.parts)
    if path.is_symlink() or not path.is_file():
        raise ToolError(f"missing regular file: {logical}")
    try:
        path.resolve(strict=True).relative_to(root.resolve(strict=True))
    except (OSError, ValueError) as exc:
        raise ToolError(f"file escapes source root: {logical}") from exc
    return path


def sha256_file(path: Path) -> str:
    digest = sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise ToolError(f"cannot hash {path}: {exc}") from exc
    return digest.hexdigest()


def require_sha256(value: object, *, context: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise ToolError(f"invalid SHA-256 declaration: {context}")
    return value
