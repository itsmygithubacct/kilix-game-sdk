"""Build a contact sheet from generated art, for review by a human.

Every game in this fleet that generates art hits the same problem: dozens of
1024–1920 px masters that have to be judged as a set, not one at a time. Judged
one at a time you accept a plate that is fine alone and wrong beside its
neighbours — the light comes from a different side, the pot is a different
size, one species is a photograph and the next is a cartoon. Consistency across
a set is exactly the property a per-file checksum cannot see and a reviewer
cannot hold in their head across forty tabs.

So: one image, a grid, downscaled so the whole set is on screen at once.

Two decisions worth stating:

**Box-average downscale, not point sampling.** A point-sampled thumbnail of a
detailed plate is aliased noise, and a reviewer would be judging the aliasing
rather than the art. Averaging every source pixel that falls in a destination
cell is slower and is the only version that tells the truth.

**Deterministic.** The same inputs produce byte-identical output, so a sheet
can be committed as a review record and a later sheet diffed against it.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

from .common import ToolError
from .graphics import (
    MAX_DECODED_BITMAP_BYTES,
    MAX_PNG_BYTES,
    MAX_PNG_CHUNKS,
    PNG_SIGNATURE,
    _PNG_CHANNELS,
)

# A sheet is for looking at, so its cells are bounded by what a screen shows.
MAX_COLUMNS = 16
MAX_CELL = 1024
MIN_CELL = 16
MAX_IMAGES = 256


@dataclass(frozen=True)
class ContactSheetReport:
    images: int
    columns: int
    rows: int
    width: int
    height: int
    output: Path


@dataclass(frozen=True)
class _Raster:
    width: int
    height: int
    channels: int
    pixels: bytes


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _decode_png(path: Path) -> _Raster:
    """Decode a non-interlaced, 8-bit PNG into raw samples.

    Accepts 8- and 16-bit, refuses interlaced and palette.

    16-bit is accepted deliberately, and it is the one place this tool differs
    from validate-graphics on purpose. Several image generators emit 16-bit
    PNG, which kilix-assets refuses at load time — so a 16-bit master is art
    that exists and is not yet loadable. That is exactly the state a reviewer
    needs to see: refusing to show it would mean the only tool for judging
    generated art cannot open the art as generated. Conversion to 8-bit is the
    asset pipeline's job; showing it is this tool's.
    """
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ToolError(f"cannot read PNG: {path}") from exc
    if len(data) > MAX_PNG_BYTES:
        raise ToolError(f"PNG exceeds {MAX_PNG_BYTES} bytes: {path}")
    if not data.startswith(PNG_SIGNATURE):
        raise ToolError(f"invalid PNG signature: {path}")

    offset = len(PNG_SIGNATURE)
    width = height = depth = color_type = interlace = None
    compressed = bytearray()
    chunks = 0
    while offset + 8 <= len(data):
        chunks += 1
        if chunks > MAX_PNG_CHUNKS:
            raise ToolError(f"PNG has too many chunks: {path}")
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        offset += 8
        if length > len(data) - offset - 4:
            raise ToolError(f"truncated PNG chunk: {path}")
        payload = data[offset:offset + length]
        offset += length
        stored = struct.unpack(">I", data[offset:offset + 4])[0]
        offset += 4
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != stored:
            raise ToolError(f"PNG {kind.decode('ascii', 'replace')} checksum "
                            f"mismatch: {path}")
        if kind == b"IHDR":
            (width, height, depth, color_type, _compression, _filter,
             interlace) = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed += payload
        elif kind == b"IEND":
            break

    if width is None or not width or not height:
        raise ToolError(f"PNG has no usable header: {path}")
    if depth not in (8, 16):
        raise ToolError(f"contact sheets need 8- or 16-bit PNG, got "
                        f"{depth}-bit: {path}")
    if interlace:
        raise ToolError(f"interlaced PNG is not supported: {path}")
    if color_type not in _PNG_CHANNELS or color_type == 3:
        raise ToolError(f"unsupported PNG colour type {color_type}: {path}")

    channels = _PNG_CHANNELS[color_type]
    sample_bytes = depth // 8
    stride = width * channels * sample_bytes
    if stride * height > MAX_DECODED_BITMAP_BYTES:
        raise ToolError(f"decoded PNG exceeds "
                        f"{MAX_DECODED_BITMAP_BYTES} bytes: {path}")
    try:
        raw = zlib.decompress(bytes(compressed))
    except zlib.error as exc:
        raise ToolError(f"corrupt PNG image data: {path}") from exc
    if len(raw) != (stride + 1) * height:
        raise ToolError(f"PNG image data has the wrong length: {path}")

    out = bytearray(width * channels * height)
    previous = bytearray(stride)
    position = 0
    for row in range(height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position:position + stride])
        position += stride
        if filter_type:
            for index in range(stride):
                back = channels * sample_bytes
                left = line[index - back] if index >= back else 0
                up = previous[index]
                upleft = previous[index - back] if index >= back else 0
                if filter_type == 1:
                    line[index] = (line[index] + left) & 0xFF
                elif filter_type == 2:
                    line[index] = (line[index] + up) & 0xFF
                elif filter_type == 3:
                    line[index] = (line[index] + ((left + up) >> 1)) & 0xFF
                elif filter_type == 4:
                    line[index] = (line[index]
                                   + _paeth(left, up, upleft)) & 0xFF
                else:
                    raise ToolError(f"unknown PNG filter {filter_type}: "
                                    f"{path}")
        if sample_bytes == 1:
            out[row * width * channels:(row + 1) * width * channels] = line
        else:
            # High byte per sample. For display that is the correct
            # downconversion and it is exact for any 16-bit value a generator
            # actually produces.
            narrow = bytes(line[index] for index in range(0, stride, 2))
            out[row * width * channels:(row + 1) * width * channels] = narrow
        previous = line
    return _Raster(width, height, channels, bytes(out))


def _sample_rgb(raster: _Raster, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * raster.width + x) * raster.channels
    pixels = raster.pixels
    if raster.channels >= 3:
        return pixels[offset], pixels[offset + 1], pixels[offset + 2]
    grey = pixels[offset]
    return grey, grey, grey


def _write_png(path: Path, width: int, height: int, rgb: bytearray) -> None:
    raw = bytearray()
    stride = width * 3
    for row in range(height):
        raw.append(0)
        raw += rgb[row * stride:(row + 1) * stride]

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    try:
        path.write_bytes(
            PNG_SIGNATURE
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
            + chunk(b"IEND", b""))
    except OSError as exc:
        raise ToolError(f"cannot write contact sheet: {path}") from exc


def build_contact_sheet(
    output: Path,
    images: list[Path],
    *,
    columns: int = 4,
    cell: tuple[int, int] = (320, 240),
    background: tuple[int, int, int] = (28, 30, 36),
) -> ContactSheetReport:
    """Composite `images` into a grid at `output`.

    Each source is fitted into its cell preserving aspect ratio and centred,
    so a landscape plate beside a square one still reads as the same set.
    """
    if not images:
        raise ToolError("a contact sheet needs at least one image")
    if len(images) > MAX_IMAGES:
        raise ToolError(f"a contact sheet holds at most {MAX_IMAGES} images")
    if not 1 <= columns <= MAX_COLUMNS:
        raise ToolError(f"columns must be 1..{MAX_COLUMNS}, got {columns}")
    cell_width, cell_height = cell
    if not (MIN_CELL <= cell_width <= MAX_CELL
            and MIN_CELL <= cell_height <= MAX_CELL):
        raise ToolError(f"cell must be within {MIN_CELL}..{MAX_CELL} px, got "
                        f"{cell_width}x{cell_height}")

    rows = (len(images) + columns - 1) // columns
    width = cell_width * columns
    height = cell_height * rows
    sheet = bytearray(bytes(background) * (width * height))

    for index, source in enumerate(images):
        raster = _decode_png(source)
        # Preserve aspect: a plate squashed to fit is a plate the reviewer
        # judges the squashing of.
        scale = min(cell_width / raster.width, cell_height / raster.height)
        target_w = max(1, int(raster.width * scale))
        target_h = max(1, int(raster.height * scale))
        pad_x = (cell_width - target_w) // 2
        pad_y = (cell_height - target_h) // 2
        origin_x = (index % columns) * cell_width + pad_x
        origin_y = (index // columns) * cell_height + pad_y

        step_x = raster.width / target_w
        step_y = raster.height / target_h
        for y in range(target_h):
            y0 = int(y * step_y)
            y1 = max(y0 + 1, min(int((y + 1) * step_y), raster.height))
            for x in range(target_w):
                x0 = int(x * step_x)
                x1 = max(x0 + 1, min(int((x + 1) * step_x), raster.width))
                red = green = blue = count = 0
                for sample_y in range(y0, y1):
                    for sample_x in range(x0, x1):
                        r, g, b = _sample_rgb(raster, sample_x, sample_y)
                        red += r
                        green += g
                        blue += b
                        count += 1
                if not count:
                    continue
                offset = ((origin_y + y) * width + origin_x + x) * 3
                sheet[offset] = red // count
                sheet[offset + 1] = green // count
                sheet[offset + 2] = blue // count

    _write_png(output, width, height, sheet)
    return ContactSheetReport(len(images), columns, rows, width, height,
                              output)
