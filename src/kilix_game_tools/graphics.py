"""Validate clean-room atlas manifests and runtime PNG/PPM bitmaps."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Any

from .common import (
    ToolError,
    load_json_object,
    require_sha256,
    resolve_regular_file,
    sha256_file,
)


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


@dataclass(frozen=True)
class GraphicsReport:
    game: str
    atlases: int
    bitmaps: int


def png_info(path: Path) -> tuple[int, int, int]:
    try:
        with path.open("rb") as stream:
            header = stream.read(29)
    except OSError as exc:
        raise ToolError(f"cannot read PNG {path}: {exc}") from exc
    if (
        len(header) != 29
        or header[:8] != PNG_SIGNATURE
        or header[12:16] != b"IHDR"
    ):
        raise ToolError(f"invalid PNG header: {path}")
    width, height, _depth, color_type, compression, filtering, interlace = (
        struct.unpack(">IIBBBBB", header[16:29])
    )
    if compression != 0 or filtering != 0 or interlace not in (0, 1):
        raise ToolError(f"invalid PNG IHDR methods: {path}")
    return width, height, color_type


def ppm_info(path: Path) -> tuple[int, int]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ToolError(f"cannot read PPM {path}: {exc}") from exc
    if not data.startswith(b"P6"):
        raise ToolError(f"not a binary P6 PPM: {path}")
    tokens: list[bytes] = []
    cursor = 2
    while len(tokens) < 3:
        while cursor < len(data) and data[cursor] in b" \t\r\n":
            cursor += 1
        if cursor < len(data) and data[cursor] == ord("#"):
            while cursor < len(data) and data[cursor] not in b"\r\n":
                cursor += 1
            continue
        start = cursor
        while cursor < len(data) and data[cursor] not in b" \t\r\n":
            cursor += 1
        if start == cursor:
            raise ToolError(f"truncated PPM header: {path}")
        tokens.append(data[start:cursor])
    try:
        width, height, maximum = (int(token) for token in tokens)
    except ValueError as exc:
        raise ToolError(f"invalid PPM dimensions: {path}") from exc
    if width <= 0 or height <= 0 or maximum != 255:
        raise ToolError(f"unsupported PPM header: {path}")
    if cursor >= len(data) or data[cursor] not in b" \t\r\n":
        raise ToolError(f"missing PPM pixel delimiter: {path}")
    cursor += 2 if data[cursor : cursor + 2] == b"\r\n" else 1
    if len(data) - cursor != width * height * 3:
        raise ToolError(f"PPM pixel payload mismatch: {path}")
    return width, height


def _records(value: object, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        raise ToolError(f"{context} must be a list of objects")
    return value


def _positive_integer(record: dict[str, Any], name: str, context: str) -> int:
    value = record.get(name)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ToolError(f"{context}.{name} must be a positive integer")
    return value


def validate_graphics(
    manifest_path: Path, *, root: Path | None = None, require_provenance: bool = True
) -> GraphicsReport:
    document = load_json_object(manifest_path)
    if document.get("schema_version") != 1:
        raise ToolError("unsupported graphics manifest schema")
    game = document.get("game")
    if not isinstance(game, str) or not game:
        raise ToolError("graphics manifest has no game ID")
    if root is None:
        try:
            root = manifest_path.resolve(strict=True).parents[2]
        except (OSError, IndexError) as exc:
            raise ToolError("cannot infer graphics source root") from exc
    root = root.resolve(strict=True)
    if require_provenance:
        provenance = document.get("provenance")
        original = document.get("clean_room")
        accepted = (
            isinstance(provenance, dict)
            and (
                provenance.get("clean_room") is True
                or provenance.get("original_project_material_only") is True
            )
        ) or (
            isinstance(original, dict)
            and original.get("commercial_reference_images_used_as_generation_inputs")
            is False
        )
        if not accepted:
            raise ToolError("clean-room provenance must be explicit")

    atlases = _records(document.get("atlases"), "atlases")
    bitmaps = _records(document.get("bitmaps"), "bitmaps")
    atlas_paths: set[str] = set()
    atlas_ids: set[str] = set()
    for atlas in atlases:
        identifier = atlas.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in atlas_ids:
            raise ToolError("atlas IDs must be unique non-empty strings")
        atlas_ids.add(identifier)
        path = resolve_regular_file(root, atlas.get("path"), suffix=".png")
        logical = path.relative_to(root).as_posix()
        if logical in atlas_paths:
            raise ToolError(f"duplicate atlas path: {logical}")
        atlas_paths.add(logical)
        source = atlas.get("source")
        if source is not None:
            resolve_regular_file(root, source)
        expected_hash = atlas.get("sha256")
        if expected_hash is not None:
            if sha256_file(path) != require_sha256(expected_hash, context=logical):
                raise ToolError(f"atlas checksum mismatch: {logical}")
        width, height, color_type = png_info(path)
        grid = atlas.get("grid")
        if not isinstance(grid, dict):
            raise ToolError(f"atlas grid must be an object: {identifier}")
        declared_width = _positive_integer(grid, "width", identifier)
        declared_height = _positive_integer(grid, "height", identifier)
        columns = _positive_integer(grid, "columns", identifier)
        rows = _positive_integer(grid, "rows", identifier)
        cell_width = _positive_integer(grid, "cell_width", identifier)
        cell_height = _positive_integer(grid, "cell_height", identifier)
        if (width, height) != (declared_width, declared_height):
            raise ToolError(f"atlas dimensions do not match grid: {identifier}")
        if (
            width % columns
            or height % rows
            or cell_width != width // columns
            or cell_height != height // rows
        ):
            raise ToolError(f"atlas grid is inconsistent: {identifier}")
        row_labels = atlas.get("row_labels")
        column_labels = atlas.get("column_labels")
        if row_labels is not None and (
            not isinstance(row_labels, list) or len(row_labels) != rows
        ):
            raise ToolError(f"atlas row labels do not match grid: {identifier}")
        if column_labels is not None and (
            not isinstance(column_labels, list) or len(column_labels) != columns
        ):
            raise ToolError(f"atlas column labels do not match grid: {identifier}")
        if atlas.get("alpha_required") is True and color_type not in (4, 6):
            raise ToolError(f"atlas requires an alpha channel: {identifier}")

    bitmap_ids: set[str] = set()
    for bitmap in bitmaps:
        identifier = bitmap.get("id")
        if not isinstance(identifier, str) or not identifier or identifier in bitmap_ids:
            raise ToolError("bitmap IDs must be unique non-empty strings")
        bitmap_ids.add(identifier)
        png = resolve_regular_file(root, bitmap.get("png"), suffix=".png")
        ppm = resolve_regular_file(root, bitmap.get("ppm"), suffix=".ppm")
        expected_png = require_sha256(
            bitmap.get("sha256_png"), context=f"{identifier}.png"
        )
        expected_ppm = require_sha256(
            bitmap.get("sha256_ppm"), context=f"{identifier}.ppm"
        )
        if sha256_file(png) != expected_png or sha256_file(ppm) != expected_ppm:
            raise ToolError(f"bitmap checksum mismatch: {identifier}")
        width = _positive_integer(bitmap, "width", identifier)
        height = _positive_integer(bitmap, "height", identifier)
        png_width, png_height, _ = png_info(png)
        if (png_width, png_height) != (width, height) or ppm_info(ppm) != (
            width,
            height,
        ):
            raise ToolError(f"bitmap dimensions do not match: {identifier}")
    return GraphicsReport(game=game, atlases=len(atlases), bitmaps=len(bitmaps))
