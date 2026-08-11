from __future__ import annotations

from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from kilix_game_tools.common import ToolError
from kilix_game_tools.contact import (
    MAX_CELL,
    MAX_COLUMNS,
    build_contact_sheet,
)

from support import PNG_SIGNATURE, png_chunk


def solid_png(
    path: Path,
    width: int,
    height: int,
    colour: tuple[int, int, int],
    *,
    depth: int = 8,
    interlace: int = 0,
) -> Path:
    """A flat rectangle, so a test can assert the exact colour that survives."""
    header = struct.pack(">IIBBBBB", width, height, depth, 2, 0, 0, interlace)
    sample = bytes(colour) if depth == 8 else b"".join(
        struct.pack(">H", value * 257) for value in colour)
    raw = bytearray()
    for _ in range(height):
        raw.append(0)
        raw += sample * width
    path.write_bytes(
        PNG_SIGNATURE
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw)))
        + png_chunk(b"IEND"))
    return path


def read_rgb(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    offset = len(PNG_SIGNATURE)
    width = height = None
    compressed = bytearray()
    while offset + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[offset:offset + 8])
        offset += 8
        payload = data[offset:offset + length]
        offset += length + 4
        if kind == b"IHDR":
            width, height = struct.unpack(">II", payload[:8])
        elif kind == b"IDAT":
            compressed += payload
        elif kind == b"IEND":
            break
    raw = zlib.decompress(bytes(compressed))
    stride = width * 3
    out = bytearray()
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position:position + stride])
        position += stride
        assert filter_type == 0, "the writer emits filter 0 only"
        out += line
        previous = line
    return width, height, bytes(out)


class ContactSheetTests(unittest.TestCase):
    def test_grid_geometry_and_determinism(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = [
                solid_png(root / "a.png", 40, 30, (255, 0, 0)),
                solid_png(root / "b.png", 40, 30, (0, 255, 0)),
                solid_png(root / "c.png", 40, 30, (0, 0, 255)),
            ]
            first = root / "one.png"
            report = build_contact_sheet(first, sources, columns=2,
                                         cell=(40, 30))
            self.assertEqual(report.images, 3)
            self.assertEqual((report.columns, report.rows), (2, 2))
            self.assertEqual((report.width, report.height), (80, 60))

            # Byte-identical on a second run: a sheet is a review record, and a
            # record that changes when nothing changed is not one.
            second = root / "two.png"
            build_contact_sheet(second, sources, columns=2, cell=(40, 30))
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_cells_carry_their_source_colour(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = [
                solid_png(root / "a.png", 20, 20, (200, 40, 40)),
                solid_png(root / "b.png", 20, 20, (40, 200, 40)),
            ]
            sheet = root / "sheet.png"
            build_contact_sheet(sheet, sources, columns=2, cell=(20, 20))
            width, height, rgb = read_rgb(sheet)
            self.assertEqual((width, height), (40, 20))

            def pixel(x: int, y: int) -> tuple[int, int, int]:
                offset = (y * width + x) * 3
                return rgb[offset], rgb[offset + 1], rgb[offset + 2]

            # A flat source must survive a box-average exactly; if this drifts,
            # the averaging is reading outside its cell.
            self.assertEqual(pixel(10, 10), (200, 40, 40))
            self.assertEqual(pixel(30, 10), (40, 200, 40))

    def test_aspect_is_preserved_and_padded(self) -> None:
        """A wide source in a square cell must be letterboxed, not squashed.

        A squashed plate is a plate whose squashing gets reviewed instead of
        its art, so the padding is the point rather than a detail.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = solid_png(root / "wide.png", 80, 20, (10, 20, 30))
            sheet = root / "sheet.png"
            build_contact_sheet(sheet, [source], columns=1, cell=(80, 80))
            width, _height, rgb = read_rgb(sheet)

            def pixel(x: int, y: int) -> tuple[int, int, int]:
                offset = (y * width + x) * 3
                return rgb[offset], rgb[offset + 1], rgb[offset + 2]

            self.assertEqual(pixel(40, 40), (10, 20, 30))     # the image
            self.assertEqual(pixel(40, 2), (28, 30, 36))      # the padding

    def test_sixteen_bit_is_accepted(self) -> None:
        """Several generators emit 16-bit PNG, which the runtime refuses.

        A reviewer still has to be able to see it: that is art which exists and
        is not yet loadable, and refusing to show it would mean the only tool
        for judging generated art cannot open the art as generated.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = solid_png(root / "deep.png", 20, 20, (90, 120, 150),
                               depth=16)
            sheet = root / "sheet.png"
            build_contact_sheet(sheet, [source], columns=1, cell=(20, 20))
            width, _height, rgb = read_rgb(sheet)
            offset = (10 * width + 10) * 3
            self.assertEqual(
                (rgb[offset], rgb[offset + 1], rgb[offset + 2]),
                (90, 120, 150))

    def test_refusals(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = solid_png(root / "a.png", 10, 10, (1, 2, 3))
            sheet = root / "sheet.png"

            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [])
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [source], columns=0)
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [source],
                                    columns=MAX_COLUMNS + 1)
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [source],
                                    cell=(MAX_CELL + 1, 32))
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [root / "missing.png"])
            # Interlaced is refused rather than half-decoded.
            interlaced = solid_png(root / "i.png", 10, 10, (1, 2, 3),
                                   interlace=1)
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [interlaced])
            # A corrupt chunk must not be composited as noise.
            broken = root / "broken.png"
            broken.write_bytes(source.read_bytes()[:-8] + b"\x00" * 8)
            with self.assertRaises(ToolError):
                build_contact_sheet(sheet, [broken])


if __name__ == "__main__":
    unittest.main()
