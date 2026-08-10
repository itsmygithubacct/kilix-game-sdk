"""Bounded filesystem and JSON helpers shared by game authoring tools."""

from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
from typing import Any


class ToolError(ValueError):
    """A deterministic validation failure suitable for command-line output."""


MAX_JSON_BYTES = 16 * 1024 * 1024


def load_json_object(
    path: Path, *, max_bytes: int = MAX_JSON_BYTES
) -> dict[str, Any]:
    """Load one bounded, finite JSON object with unique member names."""

    if max_bytes <= 0:
        raise ToolError("JSON byte limit must be positive")
    try:
        with path.open("rb") as stream:
            encoded = stream.read(max_bytes + 1)
        if len(encoded) > max_bytes:
            raise ToolError(f"JSON object exceeds {max_bytes} bytes: {path}")
        text = encoded.decode("utf-8")

        def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
            result: dict[str, Any] = {}
            for key, item in pairs:
                if key in result:
                    raise ToolError(f"{path}: duplicate JSON member: {key}")
                result[key] = item
            return result

        def reject_constant(value: str) -> None:
            raise ToolError(f"{path}: non-finite JSON number: {value}")

        value = json.loads(
            text,
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
    except ToolError:
        raise
    except (OSError, UnicodeError, ValueError, RecursionError) as exc:
        raise ToolError(f"cannot read JSON object {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ToolError(f"{path}: expected a JSON object")
    return value


def safe_relative_path(value: object, *, suffix: str | None = None) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise ToolError("path must be a non-empty string")
    if (
        "\\" in value
        or ":" in value
        or any(
            ord(character) < 32
            or ord(character) == 127
            or 0xD800 <= ord(character) <= 0xDFFF
            for character in value
        )
    ):
        raise ToolError(f"unsafe relative path: {value!r}")
    segments = value.split("/")
    if any(segment in ("", ".", "..") for segment in segments):
        raise ToolError(f"unsafe relative path: {value!r}")
    logical = PurePosixPath(value)
    if logical.is_absolute():
        raise ToolError(f"unsafe relative path: {value!r}")
    if suffix is not None and logical.suffix.lower() != suffix.lower():
        raise ToolError(f"path must end in {suffix}: {value}")
    return logical


def resolve_regular_file(root: Path, value: object, *, suffix: str | None = None) -> Path:
    logical = safe_relative_path(value, suffix=suffix)
    try:
        resolved_root = root.resolve(strict=True)
        if not resolved_root.is_dir():
            raise ToolError(f"source root is not a directory: {root}")
        path = resolved_root.joinpath(*logical.parts)
        if path.is_symlink() or not path.is_file():
            raise ToolError(f"missing regular file: {logical}")
        resolved = path.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except ToolError:
        raise
    except (OSError, ValueError) as exc:
        raise ToolError(f"file escapes source root: {logical}") from exc
    return resolved


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
