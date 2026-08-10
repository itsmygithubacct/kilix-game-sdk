from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from hashlib import sha256
import io
import json
from pathlib import Path
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

from kilix_game_tools import __version__
from kilix_game_tools.__main__ import main
from kilix_game_tools.audio import AudioReport, validate_audio
from kilix_game_tools.catalog import validate_catalog
from kilix_game_tools.common import ToolError
from kilix_game_tools.graphics import GraphicsReport


class AudioTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "cue.wav").write_bytes(b"cue")
        (self.root / "music.wav").write_bytes(b"music")
        self.manifest_path = self.root / "manifest.json"
        self.ledger_path = self.root / "ledger.json"
        self.manifest = {
            "schema_version": 1,
            "source_licensing": "CC0 1.0",
            "artifacts": [
                {
                    "file": "cue.wav",
                    "sha256": sha256(b"cue").hexdigest(),
                    "sources": ["sources/tap.wav"],
                },
                {
                    "file": "music.wav",
                    "sha256": sha256(b"music").hexdigest(),
                    "sources": [],
                },
            ],
        }
        self.ledger = {
            "schema_version": 1,
            "missing": [],
            "invalid": [],
            "collections": [
                {
                    "license": "CC0",
                    "source_page": "https://example.invalid/library?q=cc0",
                },
                {
                    "license": "Public Domain",
                    "owner_provenance": "generator/provenance.json",
                },
            ],
        }
        self.write()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self) -> None:
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.ledger_path.write_text(json.dumps(self.ledger), encoding="utf-8")

    def rejects(self) -> None:
        self.write()
        with self.assertRaises(ToolError):
            validate_audio(self.manifest_path, self.ledger_path)

    def test_complete_audio_manifest(self) -> None:
        report = validate_audio(self.manifest_path, self.ledger_path)
        self.assertEqual(report, AudioReport(2, 1, 2))

    def test_manifest_and_ledger_contracts(self) -> None:
        base_manifest = json.loads(json.dumps(self.manifest))
        base_ledger = json.loads(json.dumps(self.ledger))
        changes = (
            lambda: self.manifest.update(schema_version=2),
            lambda: self.manifest.update(source_licensing="MIT"),
            lambda: self.manifest.update(artifacts={}),
            lambda: self.ledger.update(schema_version=2),
            lambda: self.ledger.update(collections=["bad"]),
            lambda: self.ledger.update(missing=None),
            lambda: self.ledger.update(invalid={}),
            lambda: self.ledger.update(missing=["lost.wav"]),
            lambda: self.ledger.update(invalid=["bad.wav"]),
        )
        for index, change in enumerate(changes):
            with self.subTest(case=index):
                self.manifest = json.loads(json.dumps(base_manifest))
                self.ledger = json.loads(json.dumps(base_ledger))
                change()
                self.rejects()

    def test_collection_license_and_provenance_validation(self) -> None:
        bad_collections = (
            {"license": "Proprietary", "source_page": "https://example.invalid"},
            {"license": "CC0", "source_page": "http://example.invalid"},
            {"license": "CC0", "source_page": "https://"},
            {"license": "CC0", "source_page": "https://user@example.invalid"},
            {"license": "CC0", "source_page": "https://example.invalid/bad path"},
            {"license": "CC0", "owner_provenance": "../provenance.json"},
            {"license": "CC0", "owner_provenance": "owner/ledger.json"},
            {"license": "CC0"},
        )
        for collection in bad_collections:
            with self.subTest(collection=collection):
                self.ledger["collections"] = [collection]
                self.rejects()

    def test_artifact_files_hashes_uniqueness_and_sources(self) -> None:
        mutations = (
            lambda a: a.update(file="missing.wav"),
            lambda a: a.update(file="cue.mp3"),
            lambda a: a.update(sha256="0" * 64),
            lambda a: a.update(sources="tap"),
            lambda a: a.update(sources=[3]),
            lambda a: a.update(sources=["../tap.wav"]),
            lambda a: a.update(sources=["tap.wav", "tap.wav"]),
        )
        for index, mutation in enumerate(mutations):
            with self.subTest(case=index):
                original = dict(self.manifest["artifacts"][0])
                mutation(self.manifest["artifacts"][0])
                self.rejects()
                self.manifest["artifacts"][0] = original

        self.manifest["artifacts"].append(dict(self.manifest["artifacts"][0]))
        self.rejects()


class CatalogTests(unittest.TestCase):
    @staticmethod
    def module(entry=None, failure: Exception | None = None) -> ModuleType:
        module = ModuleType("kilix_content")

        class Catalog:
            @classmethod
            def load(cls, path: Path):
                if failure is not None:
                    raise failure
                return cls()

            def require(self, game_id: str):
                if failure is not None:
                    raise failure
                return entry

        module.Catalog = Catalog
        return module

    def test_catalog_entry_contract(self) -> None:
        entry = SimpleNamespace(
            kind="game",
            binary="fixture-game",
            launch_mode="terminal",
            capabilities=("kitty-graphics", "audio"),
            content_id="fixture-content",
        )
        with patch.dict(sys.modules, {"kilix_content": self.module(entry)}):
            self.assertEqual(
                validate_catalog(Path("catalog.json"), "fixture-game"),
                ("fixture-content", "terminal"),
            )
            for field, value in (
                ("kind", "application"),
                ("binary", "other"),
                ("launch_mode", "window"),
                ("capabilities", ("audio",)),
            ):
                with self.subTest(field=field):
                    old = getattr(entry, field)
                    setattr(entry, field, value)
                    with self.assertRaises(ToolError):
                        validate_catalog(Path("catalog.json"), "fixture-game")
                    setattr(entry, field, old)

    def test_invalid_ids_import_and_catalog_failures_are_normalized(self) -> None:
        for game_id in ("", ".", "Upper", "a/b", "a" * 65, "-leading"):
            with self.subTest(game_id=game_id), self.assertRaises(ToolError):
                validate_catalog(Path("catalog.json"), game_id)
        with patch.dict(sys.modules, {"kilix_content": None}):
            with self.assertRaisesRegex(ToolError, "kilix_content is unavailable"):
                validate_catalog(Path("catalog.json"), "fixture")
        for failure in (OSError("read"), ValueError("schema"), KeyError("missing")):
            with self.subTest(failure=failure), patch.dict(
                sys.modules, {"kilix_content": self.module(failure=failure)}
            ), self.assertRaisesRegex(ToolError, "catalog validation failed"):
                validate_catalog(Path("catalog.json"), "fixture")


class CliTests(unittest.TestCase):
    def invoke(self, *arguments: str) -> str:
        output = io.StringIO()
        with patch.object(sys, "argv", ["kilix-game-tools", *arguments]), redirect_stdout(output):
            self.assertEqual(main(), 0)
        return output.getvalue()

    def test_audio_graphics_and_catalog_commands(self) -> None:
        with patch(
            "kilix_game_tools.__main__.validate_audio",
            return_value=AudioReport(3, 2, 1),
        ) as audio:
            self.assertEqual(
                self.invoke("validate-audio", "manifest.json", "ledger.json"),
                "PASS audio-manifest wavs=3 foley=2 collections=1\n",
            )
            audio.assert_called_once_with(Path("manifest.json"), Path("ledger.json"))
        with patch(
            "kilix_game_tools.__main__.validate_graphics",
            return_value=GraphicsReport("fixture", 4, 5),
        ) as graphics:
            self.assertEqual(
                self.invoke(
                    "validate-graphics",
                    "manifest.json",
                    "--root",
                    "project",
                    "--no-provenance",
                ),
                "PASS graphics game=fixture atlases=4 bitmaps=5 clean-room=yes\n",
            )
            graphics.assert_called_once_with(
                Path("manifest.json"), root=Path("project"), require_provenance=False
            )
        with patch(
            "kilix_game_tools.__main__.validate_catalog",
            return_value=("content", "terminal"),
        ):
            self.assertEqual(
                self.invoke("validate-catalog", "catalog.json", "fixture"),
                "PASS catalog id=content mode=terminal\n",
            )

    def test_tool_errors_and_version_are_cli_results(self) -> None:
        with patch(
            "kilix_game_tools.__main__.validate_audio",
            side_effect=ToolError("broken input"),
        ), patch.object(
            sys,
            "argv",
            ["kilix-game-tools", "validate-audio", "manifest", "ledger"],
        ), self.assertRaisesRegex(SystemExit, "kilix-game-tools: broken input"):
            main()

        output = io.StringIO()
        with patch.object(
            sys, "argv", ["kilix-game-tools", "--version"]
        ), redirect_stdout(output), self.assertRaises(SystemExit) as raised:
            main()
        self.assertEqual(raised.exception.code, 0)
        self.assertEqual(output.getvalue(), f"kilix-game-tools {__version__}\n")

        errors = io.StringIO()
        with patch.object(
            sys, "argv", ["kilix-game-tools"]
        ), redirect_stderr(errors), self.assertRaises(SystemExit) as raised:
            main()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("required", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
