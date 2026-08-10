from __future__ import annotations

from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from kilix_game_tools.common import ToolError
from kilix_game_tools.graphics import (
    MAX_PNG_BYTES,
    png_info,
    ppm_info,
    validate_graphics,
)

from support import PNG_SIGNATURE, png_bytes, png_chunk, png_raster, write_ppm


def ihdr(
    width: int = 4,
    height: int = 3,
    *,
    depth: int = 8,
    color_type: int = 6,
    compression: int = 0,
    filtering: int = 0,
    interlace: int = 0,
) -> bytes:
    return png_chunk(
        b"IHDR",
        struct.pack(
            ">IIBBBBB",
            width,
            height,
            depth,
            color_type,
            compression,
            filtering,
            interlace,
        ),
    )


def direct_png(header: bytes, *chunks: bytes, trailing: bytes = b"") -> bytes:
    return PNG_SIGNATURE + header + b"".join(chunks) + trailing


class PngTests(unittest.TestCase):
    def test_valid_color_types_multiple_idat_and_adam7(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = (
                ("rgba.png", png_bytes(4, 3), (4, 3, 6)),
                ("rgb.png", png_bytes(5, 2, color_type=2, idat_parts=3), (5, 2, 2)),
                ("gray.png", png_bytes(7, 4, depth=1, color_type=0), (7, 4, 0)),
                ("indexed.png", png_bytes(3, 3, depth=1, color_type=3), (3, 3, 3)),
                ("adam7.png", png_bytes(9, 9, interlace=1), (9, 9, 6)),
            )
            for name, encoded, expected in cases:
                with self.subTest(name=name):
                    path = root / name
                    path.write_bytes(encoded)
                    self.assertEqual(png_info(path), expected)

    def test_signature_header_and_chunk_boundaries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.png"
            invalid = {
                "signature": b"not png",
                "header-first": PNG_SIGNATURE + png_chunk(b"tEXt"),
                "short-ihdr": direct_png(png_chunk(b"IHDR", b"short")),
                "zero-width": png_bytes(width=0),
                "bad-depth": png_bytes(depth=4, color_type=6),
                "bad-compression": png_bytes(compression=1),
                "bad-filter-method": png_bytes(filtering=1),
                "bad-interlace": png_bytes(interlace=2),
                "bad-type": direct_png(ihdr(), png_chunk(b"ab1D")),
                "reserved-bit": direct_png(ihdr(), png_chunk(b"abcd")),
                "critical": direct_png(ihdr(), png_chunk(b"ABCD")),
                "no-iend": png_bytes(include_iend=False),
                "iend-before-data": direct_png(ihdr(), png_chunk(b"IEND")),
                "after-iend": png_bytes(trailing=b"x"),
            }
            for name, encoded in invalid.items():
                with self.subTest(name=name):
                    path.write_bytes(encoded)
                    with self.assertRaises(ToolError):
                        png_info(path)

            encoded = bytearray(png_bytes())
            encoded[-1] ^= 1
            path.write_bytes(encoded)
            with self.assertRaisesRegex(ToolError, "checksum mismatch"):
                png_info(path)

            with path.open("wb") as stream:
                stream.truncate(MAX_PNG_BYTES + 1)
            with self.assertRaisesRegex(ToolError, "PNG exceeds"):
                png_info(path)

    def test_palette_and_idat_order_are_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.png"
            indexed_raw = png_raster(2, 2, depth=1, color_type=3)
            compressed = zlib.compress(indexed_raw)
            invalid = {
                "missing-palette": direct_png(
                    ihdr(2, 2, depth=1, color_type=3),
                    png_chunk(b"IDAT", compressed),
                    png_chunk(b"IEND"),
                ),
                "empty-palette": png_bytes(2, 2, depth=1, color_type=3, palette=b""),
                "odd-palette": png_bytes(2, 2, depth=2, color_type=3, palette=b"1234"),
                "large-index-palette": png_bytes(
                    2, 2, depth=1, color_type=3, palette=b"\0\0\0" * 3
                ),
                "gray-palette": png_bytes(
                    2, 2, color_type=0, palette=b"\0\0\0"
                ),
                "duplicate-palette": png_bytes(
                    2,
                    2,
                    depth=1,
                    color_type=3,
                    before_idat=(png_chunk(b"PLTE", b"\0\0\0"),),
                    palette=b"\xff\xff\xff",
                ),
                "nonconsecutive-idat": png_bytes(
                    idat_parts=2, between_idat=(png_chunk(b"tEXt", b"x"),)
                ),
            }
            for name, encoded in invalid.items():
                with self.subTest(name=name):
                    path.write_bytes(encoded)
                    with self.assertRaises(ToolError):
                        png_info(path)

    def test_compressed_raster_size_and_filters_are_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.png"
            expected = png_raster(4, 3)
            invalid = {
                "filter": png_bytes(row_filter=5),
                "short-raster": png_bytes(raw=expected[:-1]),
                "long-raster": png_bytes(raw=expected + b"x"),
                "bad-zlib": direct_png(
                    ihdr(), png_chunk(b"IDAT", b"not-zlib"), png_chunk(b"IEND")
                ),
                "truncated-zlib": direct_png(
                    ihdr(),
                    png_chunk(b"IDAT", zlib.compress(expected)[:-1]),
                    png_chunk(b"IEND"),
                ),
                "concatenated-zlib": direct_png(
                    ihdr(),
                    png_chunk(
                        b"IDAT", zlib.compress(expected) + zlib.compress(b"")
                    ),
                    png_chunk(b"IEND"),
                ),
                "decoded-limit": direct_png(ihdr(100_000, 100_000)),
            }
            for name, encoded in invalid.items():
                with self.subTest(name=name):
                    path.write_bytes(encoded)
                    with self.assertRaises(ToolError):
                        png_info(path)

            link = Path(directory) / "link.png"
            path.write_bytes(png_bytes())
            link.symlink_to(path)
            with self.assertRaises(ToolError):
                png_info(link)


class PpmTests(unittest.TestCase):
    def test_streaming_ppm_parser_accepts_comments_and_crlf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            normal = root / "normal.ppm"
            write_ppm(normal, 2, 1)
            self.assertEqual(ppm_info(normal), (2, 1))
            comments = root / "comments.ppm"
            write_ppm(
                comments,
                2,
                1,
                header=b"P6\r\n# generated fixture\r\n2 # width\n1\r\n255\r\n",
            )
            self.assertEqual(ppm_info(comments), (2, 1))

    def test_ppm_rejects_invalid_headers_sizes_and_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.ppm"
            invalid = {
                "magic": b"P3\n1 1\n255\n\0\0\0",
                "magic-delimiter": b"P6x1 1\n255\n\0\0\0",
                "integer": b"P6\n+1 1\n255\n\0\0\0",
                "maxval": b"P6\n1 1\n65535\n\0\0\0",
                "zero": b"P6\n0 1\n255\n",
                "short-payload": b"P6\n1 1\n255\n\0\0",
                "long-payload": b"P6\n1 1\n255\n\0\0\0\0",
                "truncated": b"P6\n1",
                "integer-width": b"P6\n" + b"1" * 21 + b" 1\n255\n",
                "decoded-limit": b"P6\n100000 100000\n255\n",
                "header-limit": b"P6\n" + b" " * 65536,
            }
            for name, encoded in invalid.items():
                with self.subTest(name=name):
                    path.write_bytes(encoded)
                    with self.assertRaises(ToolError):
                        ppm_info(path)


class GraphicsManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        atlas = self.root / "assets/graphics/atlases/test.png"
        source = self.root / "assets/graphics/source/test.src"
        bitmap_png = self.root / "assets/graphics/bitmaps/card.png"
        bitmap_ppm = self.root / "assets/graphics/bitmaps/card.ppm"
        for path in (atlas, source, bitmap_png, bitmap_ppm):
            path.parent.mkdir(parents=True, exist_ok=True)
        atlas.write_bytes(png_bytes(4, 2))
        source.write_bytes(b"source")
        bitmap_png.write_bytes(png_bytes(2, 1, color_type=2))
        write_ppm(bitmap_ppm, 2, 1)
        self.manifest = self.root / "assets/graphics/manifest.json"
        self.document = {
            "schema_version": 1,
            "game": "fixture",
            "provenance": {"clean_room": True},
            "atlases": [
                {
                    "id": "test",
                    "path": "assets/graphics/atlases/test.png",
                    "source": "assets/graphics/source/test.src",
                    "sha256": sha256(atlas.read_bytes()).hexdigest(),
                    "alpha_required": True,
                    "grid": {
                        "columns": 2,
                        "rows": 1,
                        "width": 4,
                        "height": 2,
                        "cell_width": 2,
                        "cell_height": 2,
                    },
                    "row_labels": ["row"],
                    "column_labels": ["left", "right"],
                }
            ],
            "bitmaps": [
                {
                    "id": "card",
                    "png": "assets/graphics/bitmaps/card.png",
                    "ppm": "assets/graphics/bitmaps/card.ppm",
                    "sha256_png": sha256(bitmap_png.read_bytes()).hexdigest(),
                    "sha256_ppm": sha256(bitmap_ppm.read_bytes()).hexdigest(),
                    "width": 2,
                    "height": 1,
                }
            ],
        }
        self.write()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, document: dict | None = None) -> None:
        self.manifest.write_text(
            json.dumps(self.document if document is None else document), encoding="utf-8"
        )

    def rejected(self, change) -> None:
        document = deepcopy(self.document)
        change(document)
        self.write(document)
        with self.assertRaises(ToolError):
            validate_graphics(self.manifest, root=self.root)

    def test_complete_manifest_and_inferred_root(self) -> None:
        report = validate_graphics(self.manifest)
        self.assertEqual((report.game, report.atlases, report.bitmaps), ("fixture", 1, 1))
        no_provenance = deepcopy(self.document)
        del no_provenance["provenance"]
        self.write(no_provenance)
        validate_graphics(self.manifest, root=self.root, require_provenance=False)

    def test_manifest_metadata_and_record_shapes(self) -> None:
        changes = (
            lambda d: d.update(schema_version=2),
            lambda d: d.update(game=""),
            lambda d: d.update(provenance={}),
            lambda d: d.update(atlases={}),
            lambda d: d.update(bitmaps=["bad"]),
            lambda d: d["atlases"][0].update(id=""),
            lambda d: d["atlases"].append(deepcopy(d["atlases"][0])),
            lambda d: d["bitmaps"][0].update(id=""),
            lambda d: d["bitmaps"].append(deepcopy(d["bitmaps"][0])),
        )
        for index, change in enumerate(changes):
            with self.subTest(case=index):
                self.rejected(change)

        self.write(self.document)
        with self.assertRaises(ToolError):
            validate_graphics(self.manifest, root=self.root / "missing")
        with self.assertRaises(ToolError):
            validate_graphics(self.manifest, root=self.manifest)

    def test_atlas_paths_hash_grid_labels_and_alpha(self) -> None:
        changes = (
            lambda d: d["atlases"][0].update(path="missing.png"),
            lambda d: d["atlases"][0].update(source="missing.src"),
            lambda d: d["atlases"][0].update(sha256="0" * 64),
            lambda d: d["atlases"][0].update(grid=[]),
            lambda d: d["atlases"][0]["grid"].update(width=5),
            lambda d: d["atlases"][0]["grid"].update(columns=3),
            lambda d: d["atlases"][0]["grid"].update(rows=True),
            lambda d: d["atlases"][0].update(row_labels=[""]),
            lambda d: d["atlases"][0].update(column_labels=["only"]),
        )
        for index, change in enumerate(changes):
            with self.subTest(case=index):
                self.rejected(change)

        rgb = self.root / "assets/graphics/atlases/rgb.png"
        rgb.write_bytes(png_bytes(4, 2, color_type=2))
        self.rejected(
            lambda d: d["atlases"][0].update(
                path="assets/graphics/atlases/rgb.png",
                sha256=sha256(rgb.read_bytes()).hexdigest(),
            )
        )

    def test_source_lists_and_duplicate_asset_paths(self) -> None:
        extra = self.root / "assets/graphics/source/extra.src"
        extra.write_bytes(b"extra")
        document = deepcopy(self.document)
        document["atlases"][0].pop("source")
        document["atlases"][0]["source_paths"] = [
            "assets/graphics/source/test.src",
            "assets/graphics/source/extra.src",
        ]
        self.write(document)
        validate_graphics(self.manifest, root=self.root)

        changes = (
            lambda d: d["atlases"][0].update(source_paths=[]),
            lambda d: d["atlases"][0].update(source_paths=[3]),
            lambda d: d["atlases"][0].update(source_paths=["missing"]),
            lambda d: d["atlases"][0].update(
                source_paths=[
                    "assets/graphics/source/test.src",
                    "assets/graphics/source/test.src",
                ]
            ),
            lambda d: d["bitmaps"][0].update(
                png="assets/graphics/atlases/test.png",
                sha256_png=d["atlases"][0]["sha256"],
            ),
        )
        for index, change in enumerate(changes):
            with self.subTest(case=index):
                self.rejected(change)

    def test_bitmap_paths_hashes_and_dimensions(self) -> None:
        changes = (
            lambda d: d["bitmaps"][0].update(png="missing.png"),
            lambda d: d["bitmaps"][0].update(ppm="missing.ppm"),
            lambda d: d["bitmaps"][0].update(sha256_png="0" * 64),
            lambda d: d["bitmaps"][0].update(sha256_ppm="0" * 64),
            lambda d: d["bitmaps"][0].update(width=3),
            lambda d: d["bitmaps"][0].update(height=False),
        )
        for index, change in enumerate(changes):
            with self.subTest(case=index):
                self.rejected(change)


if __name__ == "__main__":
    unittest.main()
