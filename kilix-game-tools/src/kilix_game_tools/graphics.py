"""Validate clean-room atlas manifests and runtime PNG/PPM bitmaps."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import os
from pathlib import Path
import re
import stat
import struct
from typing import Any, BinaryIO, Iterator
import zlib

from .common import (
    ToolError,
    load_json_object,
    require_sha256,
    resolve_regular_file,
    sha256_file,
)


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PNG_BYTES = 512 * 1024 * 1024
MAX_DECODED_BITMAP_BYTES = 512 * 1024 * 1024
MAX_PNG_CHUNKS = 100_000
MAX_PPM_HEADER_BYTES = 64 * 1024
STREAM_BLOCK_BYTES = 1024 * 1024
GAME_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
ASSET_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,127}$")

_PNG_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
_PNG_DEPTHS = {
    0: frozenset((1, 2, 4, 8, 16)),
    2: frozenset((8, 16)),
    3: frozenset((1, 2, 4, 8)),
    4: frozenset((8, 16)),
    6: frozenset((8, 16)),
}
_ADAM7 = (
    (0, 0, 8, 8),
    (4, 0, 8, 8),
    (0, 4, 4, 8),
    (2, 0, 4, 4),
    (0, 2, 2, 4),
    (1, 0, 2, 2),
    (0, 1, 1, 2),
)


@dataclass(frozen=True)
class GraphicsReport:
    game: str
    atlases: int
    bitmaps: int


@contextmanager
def _open_regular(path: Path, kind: str) -> Iterator[tuple[BinaryIO, os.stat_result]]:
    descriptor: int | None = None
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
        source_stat = os.fstat(descriptor)
        if not stat.S_ISREG(source_stat.st_mode):
            raise ToolError(f"{kind} is not a regular file: {path}")
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = None
            yield stream, source_stat
    except ToolError:
        raise
    except OSError as exc:
        raise ToolError(f"cannot read {kind} {path}: {exc}") from exc
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _read_exact(stream: BinaryIO, size: int, path: Path, context: str) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise ToolError(f"truncated PNG {context}: {path}")
    return data


def _read_png_chunk(
    stream: BinaryIO,
    length: int,
    chunk_type: bytes,
    path: Path,
    *,
    capture: bool = False,
    consume: Any = None,
) -> bytes:
    checksum = zlib.crc32(chunk_type)
    remaining = length
    captured = bytearray()
    while remaining:
        block = _read_exact(
            stream, min(remaining, STREAM_BLOCK_BYTES), path, "chunk data"
        )
        checksum = zlib.crc32(block, checksum)
        if capture:
            captured.extend(block)
        if consume is not None:
            consume(block)
        remaining -= len(block)
    declared_checksum = struct.unpack(
        ">I", _read_exact(stream, 4, path, "chunk checksum")
    )[0]
    if checksum & 0xFFFFFFFF != declared_checksum:
        name = chunk_type.decode("ascii", errors="replace")
        raise ToolError(f"PNG {name} checksum mismatch: {path}")
    return bytes(captured)


def _pass_extent(length: int, start: int, step: int) -> int:
    if length <= start:
        return 0
    return (length - start + step - 1) // step


class _RasterValidator:
    def __init__(
        self, width: int, height: int, depth: int, color_type: int, interlace: int
    ) -> None:
        bits_per_pixel = _PNG_CHANNELS[color_type] * depth
        if interlace == 0:
            dimensions = ((width, height),)
        else:
            dimensions = tuple(
                (
                    _pass_extent(width, x_start, x_step),
                    _pass_extent(height, y_start, y_step),
                )
                for x_start, y_start, x_step, y_step in _ADAM7
            )
        self._passes = tuple(
            (((pass_width * bits_per_pixel + 7) // 8), pass_height)
            for pass_width, pass_height in dimensions
            if pass_width and pass_height
        )
        self.expected = sum((row_bytes + 1) * rows for row_bytes, rows in self._passes)
        if self.expected > MAX_DECODED_BITMAP_BYTES:
            raise ToolError(
                f"decoded PNG exceeds {MAX_DECODED_BITMAP_BYTES} bytes"
            )
        self.total = 0
        self._pass_index = 0
        self._rows_left = 0
        self._row_bytes = 0
        self._row_left = 0
        self._needs_filter = True
        self._advance_pass()

    def _advance_pass(self) -> None:
        while self._pass_index < len(self._passes):
            self._row_bytes, self._rows_left = self._passes[self._pass_index]
            self._pass_index += 1
            if self._rows_left:
                self._needs_filter = True
                self._row_left = 0
                return
        self._rows_left = 0

    def consume(self, data: bytes) -> None:
        cursor = 0
        if self.total + len(data) > self.expected:
            raise ToolError("PNG raster is larger than its IHDR dimensions")
        while cursor < len(data):
            if not self._rows_left:
                raise ToolError("PNG raster is larger than its IHDR dimensions")
            if self._needs_filter:
                if data[cursor] > 4:
                    raise ToolError(f"invalid PNG row filter: {data[cursor]}")
                cursor += 1
                self.total += 1
                self._row_left = self._row_bytes
                self._needs_filter = False
                continue
            consumed = min(self._row_left, len(data) - cursor)
            cursor += consumed
            self.total += consumed
            self._row_left -= consumed
            if self._row_left == 0:
                self._rows_left -= 1
                if self._rows_left:
                    self._needs_filter = True
                else:
                    self._advance_pass()

    def finish(self) -> None:
        if self.total != self.expected or self._rows_left:
            raise ToolError("PNG raster size does not match its IHDR dimensions")


class _PngInflater:
    def __init__(self, raster: _RasterValidator) -> None:
        self._decoder = zlib.decompressobj()
        self._raster = raster

    def consume(self, compressed: bytes) -> None:
        pending = compressed
        while pending:
            remaining = self._raster.expected - self._raster.total
            decoded = self._decoder.decompress(
                pending, min(STREAM_BLOCK_BYTES, remaining + 1)
            )
            self._raster.consume(decoded)
            if self._decoder.unused_data:
                raise ToolError("PNG IDAT contains trailing compressed data")
            pending = self._decoder.unconsumed_tail

    def finish(self) -> None:
        while True:
            remaining = self._raster.expected - self._raster.total
            decoded = self._decoder.decompress(
                b"", min(STREAM_BLOCK_BYTES, remaining + 1)
            )
            if not decoded:
                break
            self._raster.consume(decoded)
        self._raster.consume(self._decoder.flush())
        if not self._decoder.eof or self._decoder.unused_data:
            raise ToolError("truncated or concatenated PNG compressed stream")
        self._raster.finish()


def png_info(path: Path) -> tuple[int, int, int]:
    try:
        with _open_regular(path, "PNG") as (stream, source_stat):
            if source_stat.st_size > MAX_PNG_BYTES:
                raise ToolError(f"PNG exceeds {MAX_PNG_BYTES} bytes: {path}")
            if (
                _read_exact(stream, len(PNG_SIGNATURE), path, "signature")
                != PNG_SIGNATURE
            ):
                raise ToolError(f"invalid PNG signature: {path}")

            width = height = depth = color_type = interlace = None
            seen_header = False
            seen_palette = False
            seen_data = False
            data_closed = False
            inflater: _PngInflater | None = None

            for chunk_number in range(1, MAX_PNG_CHUNKS + 1):
                chunk_header = _read_exact(stream, 8, path, "chunk header")
                length, chunk_type = struct.unpack(">I4s", chunk_header)
                if length > 0x7FFFFFFF:
                    raise ToolError(f"oversized PNG chunk: {path}")
                if not all(
                    ord("A") <= byte <= ord("Z")
                    or ord("a") <= byte <= ord("z")
                    for byte in chunk_type
                ) or not ord("A") <= chunk_type[2] <= ord("Z"):
                    raise ToolError(f"invalid PNG chunk type: {path}")
                if chunk_number == 1 and chunk_type != b"IHDR":
                    raise ToolError(f"PNG IHDR is not first: {path}")

                if chunk_type == b"IHDR":
                    if seen_header or length != 13:
                        raise ToolError(f"invalid PNG IHDR: {path}")
                    header = _read_png_chunk(
                        stream, length, chunk_type, path, capture=True
                    )
                    (
                        width,
                        height,
                        depth,
                        color_type,
                        compression,
                        filtering,
                        interlace,
                    ) = struct.unpack(">IIBBBBB", header)
                    if (
                        width == 0
                        or height == 0
                        or width > 0x7FFFFFFF
                        or height > 0x7FFFFFFF
                    ):
                        raise ToolError(f"invalid PNG dimensions: {path}")
                    if (
                        color_type not in _PNG_DEPTHS
                        or depth not in _PNG_DEPTHS[color_type]
                    ):
                        raise ToolError(f"invalid PNG color type or depth: {path}")
                    if compression != 0 or filtering != 0 or interlace not in (0, 1):
                        raise ToolError(f"invalid PNG IHDR methods: {path}")
                    raster = _RasterValidator(
                        width, height, depth, color_type, interlace
                    )
                    inflater = _PngInflater(raster)
                    seen_header = True
                    continue

                if not seen_header:
                    raise ToolError(f"PNG has no IHDR: {path}")
                if chunk_type == b"PLTE":
                    if seen_palette or seen_data or color_type in (0, 4):
                        raise ToolError(f"invalid PNG palette order: {path}")
                    palette = _read_png_chunk(
                        stream, length, chunk_type, path, capture=True
                    )
                    entries = len(palette) // 3
                    if (
                        not palette
                        or len(palette) % 3
                        or entries > 256
                        or (color_type == 3 and entries > 2**depth)
                    ):
                        raise ToolError(f"invalid PNG palette: {path}")
                    seen_palette = True
                    continue
                if chunk_type == b"IDAT":
                    if data_closed or (color_type == 3 and not seen_palette):
                        raise ToolError(f"invalid PNG IDAT order: {path}")
                    assert inflater is not None
                    _read_png_chunk(
                        stream,
                        length,
                        chunk_type,
                        path,
                        consume=inflater.consume,
                    )
                    seen_data = True
                    continue

                if seen_data:
                    data_closed = True
                if chunk_type == b"IEND":
                    if length != 0 or not seen_data:
                        raise ToolError(f"invalid PNG IEND: {path}")
                    _read_png_chunk(stream, length, chunk_type, path)
                    assert inflater is not None
                    inflater.finish()
                    if stream.read(1):
                        raise ToolError(f"data follows PNG IEND: {path}")
                    return width, height, color_type
                if chunk_type[0] & 0x20 == 0:
                    name = chunk_type.decode("ascii", errors="replace")
                    raise ToolError(f"unknown critical PNG chunk {name}: {path}")
                _read_png_chunk(stream, length, chunk_type, path)
            raise ToolError(f"PNG has more than {MAX_PNG_CHUNKS} chunks: {path}")
    except zlib.error as exc:
        raise ToolError(f"invalid PNG compressed stream {path}: {exc}") from exc


def ppm_info(path: Path) -> tuple[int, int]:
    with _open_regular(path, "PPM") as (stream, source_stat):
        consumed = 0

        def read_header_byte() -> bytes:
            nonlocal consumed
            if consumed >= MAX_PPM_HEADER_BYTES:
                raise ToolError(f"PPM header exceeds {MAX_PPM_HEADER_BYTES} bytes: {path}")
            value = stream.read(1)
            if not value:
                raise ToolError(f"truncated PPM header: {path}")
            consumed += 1
            return value

        if read_header_byte() + read_header_byte() != b"P6":
            raise ToolError(f"not a binary P6 PPM: {path}")
        if read_header_byte() not in b" \t\r\n":
            raise ToolError(f"invalid PPM magic delimiter: {path}")

        def token() -> tuple[bytes, bytes]:
            value = read_header_byte()
            while True:
                while value in b" \t\r\n":
                    value = read_header_byte()
                if value != b"#":
                    break
                while value not in b"\r\n":
                    value = read_header_byte()
            result = bytearray()
            while value not in b" \t\r\n":
                if len(result) >= 20:
                    raise ToolError(f"oversized PPM header integer: {path}")
                result.extend(value)
                value = read_header_byte()
            if not result or not bytes(result).isdigit():
                raise ToolError(f"invalid PPM header integer: {path}")
            return bytes(result), value

        width_token, _ = token()
        height_token, _ = token()
        maximum_token, delimiter = token()
        width, height, maximum = map(int, (width_token, height_token, maximum_token))
        if width <= 0 or height <= 0 or maximum != 255:
            raise ToolError(f"unsupported PPM header: {path}")

        # The token parser consumed the required separator after maxval. Treat
        # CRLF as one line ending without consuming a possible first pixel.
        if delimiter == b"\r":
            following = stream.read(1)
            if following and following != b"\n":
                stream.seek(-1, os.SEEK_CUR)
        payload_offset = stream.tell()
        payload_size = width * height * 3
        if payload_size > MAX_DECODED_BITMAP_BYTES:
            raise ToolError(
                f"PPM payload exceeds {MAX_DECODED_BITMAP_BYTES} bytes: {path}"
            )
        if source_stat.st_size - payload_offset != payload_size:
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


def _labels(value: object, count: int, context: str) -> None:
    if value is None:
        return
    if (
        not isinstance(value, list)
        or len(value) != count
        or any(not isinstance(label, str) or not label for label in value)
    ):
        raise ToolError(f"{context} labels do not match grid")


def validate_graphics(
    manifest_path: Path, *, root: Path | None = None, require_provenance: bool = True
) -> GraphicsReport:
    document = load_json_object(manifest_path)
    if document.get("schema_version") != 1:
        raise ToolError("unsupported graphics manifest schema")
    game = document.get("game")
    if not isinstance(game, str) or GAME_ID.fullmatch(game) is None:
        raise ToolError("graphics manifest has no game ID")
    if root is None:
        try:
            root = manifest_path.resolve(strict=True).parents[2]
        except (OSError, IndexError) as exc:
            raise ToolError("cannot infer graphics source root") from exc
    try:
        root = root.resolve(strict=True)
    except OSError as exc:
        raise ToolError(f"cannot resolve graphics source root: {root}") from exc
    if not root.is_dir():
        raise ToolError(f"graphics source root is not a directory: {root}")
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
    asset_paths: set[str] = set()
    atlas_ids: set[str] = set()
    for atlas in atlases:
        identifier = atlas.get("id")
        if (
            not isinstance(identifier, str)
            or ASSET_ID.fullmatch(identifier) is None
            or identifier in atlas_ids
        ):
            raise ToolError("atlas IDs must be unique canonical strings")
        atlas_ids.add(identifier)
        path = resolve_regular_file(root, atlas.get("path"), suffix=".png")
        logical = path.relative_to(root).as_posix()
        if logical in asset_paths:
            raise ToolError(f"duplicate graphics path: {logical}")
        asset_paths.add(logical)
        source = atlas.get("source")
        if source is not None:
            resolve_regular_file(root, source)
        source_paths = atlas.get("source_paths")
        if source_paths is not None:
            if (
                not isinstance(source_paths, list)
                or not source_paths
                or any(not isinstance(item, str) for item in source_paths)
            ):
                raise ToolError(f"invalid atlas source paths: {identifier}")
            seen_sources: set[str] = set()
            for source_path in source_paths:
                resolved_source = resolve_regular_file(root, source_path)
                logical_source = resolved_source.relative_to(root).as_posix()
                if logical_source in seen_sources:
                    raise ToolError(f"duplicate atlas source path: {logical_source}")
                seen_sources.add(logical_source)
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
        _labels(row_labels, rows, f"atlas row {identifier}")
        _labels(column_labels, columns, f"atlas column {identifier}")
        if atlas.get("alpha_required") is True and color_type not in (4, 6):
            raise ToolError(f"atlas requires an alpha channel: {identifier}")

    bitmap_ids: set[str] = set()
    for bitmap in bitmaps:
        identifier = bitmap.get("id")
        if (
            not isinstance(identifier, str)
            or ASSET_ID.fullmatch(identifier) is None
            or identifier in bitmap_ids
        ):
            raise ToolError("bitmap IDs must be unique canonical strings")
        bitmap_ids.add(identifier)
        png = resolve_regular_file(root, bitmap.get("png"), suffix=".png")
        ppm = resolve_regular_file(root, bitmap.get("ppm"), suffix=".ppm")
        for asset in (png, ppm):
            logical_asset = asset.relative_to(root).as_posix()
            if logical_asset in asset_paths:
                raise ToolError(f"duplicate graphics path: {logical_asset}")
            asset_paths.add(logical_asset)
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
