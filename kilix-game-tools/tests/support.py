from __future__ import annotations

from pathlib import Path
import struct
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
ADAM7 = (
    (0, 0, 8, 8),
    (4, 0, 8, 8),
    (0, 4, 4, 8),
    (2, 0, 4, 4),
    (0, 2, 2, 4),
    (1, 0, 2, 2),
    (0, 1, 1, 2),
)


def png_chunk(kind: bytes, data: bytes = b"") -> bytes:
    checksum = zlib.crc32(kind)
    checksum = zlib.crc32(data, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)


def _extent(length: int, start: int, step: int) -> int:
    return 0 if length <= start else (length - start + step - 1) // step


def png_raster(
    width: int,
    height: int,
    *,
    depth: int = 8,
    color_type: int = 6,
    interlace: int = 0,
    row_filter: int = 0,
) -> bytes:
    bits_per_pixel = CHANNELS[color_type] * depth
    if interlace:
        dimensions = (
            (_extent(width, x, dx), _extent(height, y, dy))
            for x, y, dx, dy in ADAM7
        )
    else:
        dimensions = ((width, height),)
    output = bytearray()
    for pass_width, pass_height in dimensions:
        if not pass_width or not pass_height:
            continue
        row_bytes = (pass_width * bits_per_pixel + 7) // 8
        for _ in range(pass_height):
            output.append(row_filter)
            output.extend(b"\0" * row_bytes)
    return bytes(output)


def png_bytes(
    width: int = 4,
    height: int = 3,
    *,
    depth: int = 8,
    color_type: int = 6,
    compression: int = 0,
    filtering: int = 0,
    interlace: int = 0,
    row_filter: int = 0,
    palette: bytes | None = None,
    raw: bytes | None = None,
    idat_parts: int = 1,
    before_idat: tuple[bytes, ...] = (),
    between_idat: tuple[bytes, ...] = (),
    after_idat: tuple[bytes, ...] = (),
    include_iend: bool = True,
    trailing: bytes = b"",
) -> bytes:
    header = struct.pack(
        ">IIBBBBB",
        width,
        height,
        depth,
        color_type,
        compression,
        filtering,
        interlace,
    )
    result = bytearray(PNG_SIGNATURE + png_chunk(b"IHDR", header))
    for item in before_idat:
        result.extend(item)
    if color_type == 3 and palette is None:
        palette = b"\0\0\0\xff\xff\xff"
    if palette is not None:
        result.extend(png_chunk(b"PLTE", palette))
    if raw is None:
        raw = png_raster(
            width,
            height,
            depth=depth,
            color_type=color_type,
            interlace=interlace,
            row_filter=row_filter,
        )
    compressed = zlib.compress(raw)
    split = max(1, len(compressed) // idat_parts)
    pieces = [compressed[index : index + split] for index in range(0, len(compressed), split)]
    for index, piece in enumerate(pieces):
        result.extend(png_chunk(b"IDAT", piece))
        if index == 0:
            for item in between_idat:
                result.extend(item)
    for item in after_idat:
        result.extend(item)
    if include_iend:
        result.extend(png_chunk(b"IEND"))
    result.extend(trailing)
    return bytes(result)


def write_ppm(path: Path, width: int, height: int, *, header: bytes | None = None) -> None:
    if header is None:
        header = f"P6\n{width} {height}\n255\n".encode("ascii")
    path.write_bytes(header + b"\0" * (width * height * 3))
