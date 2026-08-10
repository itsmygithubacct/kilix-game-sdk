from __future__ import annotations

from hashlib import sha256
import json
import os
from pathlib import Path, PurePosixPath
import stat
import tarfile
import tempfile
import unittest
import zipfile

from kilix_game_tools.archive import (
    ArchiveEntry,
    collect_entry,
    validate_entries,
    write_tar_gz,
    write_zip,
)
from kilix_game_tools.common import (
    ToolError,
    load_json_object,
    require_sha256,
    resolve_regular_file,
    safe_relative_path,
    sha256_file,
)


class CommonTests(unittest.TestCase):
    def test_json_object_is_bounded_strict_and_unique(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = root / "document.json"
            document.write_text('{"nested": {"value": 2}}', encoding="utf-8")
            self.assertEqual(load_json_object(document)["nested"]["value"], 2)

            cases = (
                ("[]", "expected a JSON object"),
                ('{"x": 1, "x": 2}', "duplicate JSON member"),
                ('{"x": NaN}', "non-finite JSON number"),
                ('{"x":', "cannot read JSON object"),
            )
            for encoded, message in cases:
                with self.subTest(encoded=encoded):
                    document.write_text(encoded, encoding="utf-8")
                    with self.assertRaisesRegex(ToolError, message):
                        load_json_object(document)

            document.write_bytes(b"\xff")
            with self.assertRaisesRegex(ToolError, "cannot read JSON object"):
                load_json_object(document)
            document.write_bytes(b"{} ")
            with self.assertRaisesRegex(ToolError, "exceeds 2 bytes"):
                load_json_object(document, max_bytes=2)
            with self.assertRaisesRegex(ToolError, "limit must be positive"):
                load_json_object(document, max_bytes=0)
            with self.assertRaisesRegex(ToolError, "cannot read JSON object"):
                load_json_object(root / "missing.json")

    def test_safe_relative_paths_are_canonical_and_portable(self) -> None:
        self.assertEqual(
            safe_relative_path("assets/a.PNG", suffix=".png"),
            PurePosixPath("assets/a.PNG"),
        )
        invalid = (
            "",
            ".",
            "..",
            "/absolute",
            "a//b",
            "a/./b",
            "a/../b",
            "a/",
            "a\\b",
            "C:/file",
            "line\nname",
            "bad\ud800name",
        )
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(ToolError):
                safe_relative_path(value)
        with self.assertRaises(ToolError):
            safe_relative_path(3)
        with self.assertRaisesRegex(ToolError, "must end in .png"):
            safe_relative_path("asset.jpg", suffix=".png")

    def test_regular_file_resolution_hashing_and_digest_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nested = root / "assets/file.bin"
            nested.parent.mkdir()
            nested.write_bytes(b"payload")
            self.assertEqual(resolve_regular_file(root, "assets/file.bin"), nested.resolve())
            self.assertEqual(sha256_file(nested), sha256(b"payload").hexdigest())
            self.assertEqual(require_sha256("a" * 64, context="fixture"), "a" * 64)
            for digest in (None, "A" * 64, "a" * 63, "z" * 64):
                with self.subTest(digest=digest), self.assertRaises(ToolError):
                    require_sha256(digest, context="fixture")

            (root / "link.bin").symlink_to(nested)
            with self.assertRaises(ToolError):
                resolve_regular_file(root, "link.bin")
            with self.assertRaises(ToolError):
                resolve_regular_file(root, "missing.bin")
            with self.assertRaises(ToolError):
                resolve_regular_file(root / "missing", "file.bin")
            with self.assertRaises(ToolError):
                resolve_regular_file(nested, "file.bin")
            with self.assertRaises(ToolError):
                sha256_file(root / "missing.bin")


class ArchiveTests(unittest.TestCase):
    def test_archives_stream_sorted_reproducible_regular_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "b.txt").write_bytes(b"bravo\n")
            (root / "a.sh").write_bytes(b"alpha\n")
            entries = [
                collect_entry(root, "b.txt"),
                collect_entry(root, "a.sh", "bin/a.sh", mode=0o755),
            ]
            first_tar, second_tar = root / "one.tar.gz", root / "two.tar.gz"
            first_zip, second_zip = root / "one.zip", root / "two.zip"
            write_tar_gz(first_tar, "fixture-1", entries)
            write_tar_gz(second_tar, "fixture-1", list(reversed(entries)))
            write_zip(first_zip, entries)
            write_zip(second_zip, list(reversed(entries)))
            self.assertEqual(first_tar.read_bytes(), second_tar.read_bytes())
            self.assertEqual(first_zip.read_bytes(), second_zip.read_bytes())

            with tarfile.open(first_tar, "r:gz") as archive:
                self.assertEqual(
                    archive.getnames(), ["fixture-1/b.txt", "fixture-1/bin/a.sh"]
                )
                members = {item.name: item for item in archive.getmembers()}
                self.assertEqual(members["fixture-1/bin/a.sh"].mode, 0o755)
                self.assertEqual(members["fixture-1/b.txt"].mtime, 0)
            with zipfile.ZipFile(first_zip) as archive:
                self.assertEqual(archive.namelist(), ["b.txt", "bin/a.sh"])
                mode = archive.getinfo("bin/a.sh").external_attr >> 16
                self.assertTrue(stat.S_ISREG(mode))
                self.assertEqual(mode & 0o777, 0o755)

    def test_unique_temp_file_does_not_follow_predictable_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.txt"
            source.write_text("source\n", encoding="utf-8")
            victim = root / "victim.txt"
            victim.write_text("preserve me\n", encoding="utf-8")
            output = root / "release.tar.gz"
            bait = output.with_suffix(output.suffix + ".tmp")
            bait.symlink_to(victim)
            write_tar_gz(output, "fixture", [collect_entry(root, source.name)])
            self.assertEqual(victim.read_text(encoding="utf-8"), "preserve me\n")
            self.assertTrue(bait.is_symlink())
            self.assertFalse(output.is_symlink())

            zip_output = root / "release.zip"
            zip_output.symlink_to(victim)
            write_zip(zip_output, [collect_entry(root, source.name)])
            self.assertFalse(zip_output.is_symlink())
            self.assertTrue(zipfile.is_zipfile(zip_output))

    def test_entry_validation_rejects_unsafe_or_ambiguous_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.txt"
            source.write_text("source", encoding="utf-8")
            link = root / "link.txt"
            link.symlink_to(source)
            for name in ("missing", "link.txt", "../escape"):
                with self.subTest(name=name), self.assertRaises(ToolError):
                    collect_entry(root, name)
            with self.assertRaises(ToolError):
                collect_entry(root, "source.txt", mode=0o600)
            with self.assertRaises(ToolError):
                collect_entry(source, "child")

            good = collect_entry(root, "source.txt")
            with self.assertRaisesRegex(ToolError, "duplicate release destination"):
                validate_entries([good, good])
            with self.assertRaises(ToolError):
                validate_entries([ArchiveEntry(source, PurePosixPath("bad/../name"))])
            with self.assertRaises(ToolError):
                validate_entries([ArchiveEntry(root, PurePosixPath("root"))])
            with self.assertRaises(ToolError):
                validate_entries([ArchiveEntry(source, PurePosixPath("source"), 0o600)])
            with self.assertRaises(ToolError):
                write_tar_gz(root / "bad.tar.gz", "../prefix", [good])


if __name__ == "__main__":
    unittest.main()
