"""Deterministic tar/zip construction and safe release-entry collection."""

from __future__ import annotations

from dataclasses import dataclass
import gzip
import io
import os
from pathlib import Path, PurePosixPath
import tarfile
import tempfile
import zipfile

from .common import ToolError, safe_relative_path


ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)


@dataclass(frozen=True)
class ArchiveEntry:
    source: Path
    destination: PurePosixPath
    mode: int = 0o644


def collect_entry(
    root: Path, source: str, destination: str | None = None, *, mode: int = 0o644
) -> ArchiveEntry:
    source_name = safe_relative_path(source)
    destination_name = safe_relative_path(destination or source)
    path = root.joinpath(*source_name.parts)
    if path.is_symlink() or not path.is_file():
        raise ToolError(f"release input is not a regular file: {source_name}")
    try:
        path.resolve(strict=True).relative_to(root.resolve(strict=True))
    except (OSError, ValueError) as exc:
        raise ToolError(f"release input escapes source root: {source_name}") from exc
    if mode not in (0o644, 0o755):
        raise ToolError(f"unsupported release mode: {mode:o}")
    return ArchiveEntry(path, destination_name, mode)


def validate_entries(entries: list[ArchiveEntry]) -> list[ArchiveEntry]:
    seen: set[str] = set()
    validated: list[ArchiveEntry] = []
    for entry in entries:
        destination = safe_relative_path(entry.destination.as_posix())
        if destination.as_posix() in seen:
            raise ToolError(f"duplicate release destination: {destination}")
        seen.add(destination.as_posix())
        if entry.source.is_symlink() or not entry.source.is_file():
            raise ToolError(f"release input is not regular: {entry.source}")
        if entry.mode not in (0o644, 0o755):
            raise ToolError(f"unsupported release mode: {entry.mode:o}")
        validated.append(entry)
    return sorted(validated, key=lambda item: item.destination.as_posix())


def write_tar_gz(
    output: Path, prefix: str, entries: list[ArchiveEntry]
) -> None:
    prefix_path = safe_relative_path(prefix)
    selected = validate_entries(entries)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with temporary.open("wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
                with tarfile.open(
                    fileobj=gz, mode="w", format=tarfile.PAX_FORMAT
                ) as archive:
                    for entry in selected:
                        data = entry.source.read_bytes()
                        info = tarfile.TarInfo(
                            (prefix_path / entry.destination).as_posix()
                        )
                        info.size = len(data)
                        info.mode = entry.mode
                        info.uid = 0
                        info.gid = 0
                        info.uname = "root"
                        info.gname = "root"
                        info.mtime = 0
                        archive.addfile(info, io.BytesIO(data))
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def write_zip(output: Path, entries: list[ArchiveEntry]) -> None:
    selected = validate_entries(entries)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent,
            prefix=f".{output.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
        with zipfile.ZipFile(temporary_path, "w") as archive:
            for entry in selected:
                info = zipfile.ZipInfo(entry.destination.as_posix(), ZIP_EPOCH)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = (entry.mode & 0xFFFF) << 16
                archive.writestr(info, entry.source.read_bytes())
        with temporary_path.open("rb+") as stream:
            os.fsync(stream.fileno())
        os.replace(temporary_path, output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
