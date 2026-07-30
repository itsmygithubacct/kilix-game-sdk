from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import zipfile

from kilix_game_tools.archive import (
    collect_entry,
    write_tar_gz,
    write_zip,
)
from kilix_game_tools.audio import validate_audio
from kilix_game_tools.common import ToolError, safe_relative_path
from kilix_game_tools.graphics import validate_graphics


def png_header(width: int, height: int, color_type: int = 6) -> bytes:
    return (
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", 13)
        + b"IHDR"
        + struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    )


class ToolTests(unittest.TestCase):
    def test_audio_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wav = root / "cue.wav"
            wav.write_bytes(b"fixture")
            manifest = root / "manifest.json"
            ledger = root / "ledger.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "source_licensing": "CC0 1.0",
                        "artifacts": [
                            {
                                "file": "cue.wav",
                                "sha256": sha256(b"fixture").hexdigest(),
                                "sources": ["tap"],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            ledger.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "missing": [],
                        "invalid": [],
                        "collections": [
                            {
                                "license": "CC0",
                                "source_page": "https://example.invalid/cc0",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            report = validate_audio(manifest, ledger)
            self.assertEqual((report.artifacts, report.foley_artifacts), (1, 1))
            wav.write_bytes(b"changed")
            with self.assertRaises(ToolError):
                validate_audio(manifest, ledger)

    def test_graphics_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            atlas = root / "assets/graphics/atlases/test.png"
            source = root / "assets/graphics/source/test.png"
            atlas.parent.mkdir(parents=True)
            source.parent.mkdir(parents=True)
            atlas.write_bytes(png_header(16, 8))
            source.write_bytes(b"source")
            manifest = root / "assets/graphics/manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "game": "fixture",
                        "provenance": {"clean_room": True},
                        "atlases": [
                            {
                                "id": "test",
                                "path": "assets/graphics/atlases/test.png",
                                "source": "assets/graphics/source/test.png",
                                "sha256": sha256(atlas.read_bytes()).hexdigest(),
                                "alpha_required": True,
                                "grid": {
                                    "columns": 2,
                                    "rows": 1,
                                    "width": 16,
                                    "height": 8,
                                    "cell_width": 8,
                                    "cell_height": 8,
                                },
                                "row_labels": ["row"],
                                "column_labels": ["a", "b"],
                            }
                        ],
                        "bitmaps": [],
                    }
                ),
                encoding="utf-8",
            )
            report = validate_graphics(manifest, root=root)
            self.assertEqual((report.game, report.atlases), ("fixture", 1))

    def test_archives_are_reproducible_and_safe(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text("fixture\n", encoding="utf-8")
            entry = collect_entry(root, "README.md")
            first_tar = root / "one.tar.gz"
            second_tar = root / "two.tar.gz"
            first_zip = root / "one.zip"
            second_zip = root / "two.zip"
            write_tar_gz(first_tar, "fixture-1", [entry])
            write_tar_gz(second_tar, "fixture-1", [entry])
            write_zip(first_zip, [entry])
            write_zip(second_zip, [entry])
            self.assertEqual(first_tar.read_bytes(), second_tar.read_bytes())
            self.assertEqual(first_zip.read_bytes(), second_zip.read_bytes())
            with tarfile.open(first_tar, "r:gz") as archive:
                self.assertEqual(archive.getnames(), ["fixture-1/README.md"])
            with zipfile.ZipFile(first_zip) as archive:
                self.assertEqual(archive.namelist(), ["README.md"])
            with self.assertRaises(ToolError):
                safe_relative_path("../escape")


if __name__ == "__main__":
    unittest.main()
