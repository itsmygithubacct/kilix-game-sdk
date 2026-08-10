"""Deterministic tar/zip construction and safe release-entry collection."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import gzip
import os
from pathlib import Path, PurePosixPath
import stat
import tarfile
import tempfile
from typing import BinaryIO, Iterator
import zipfile

from .common import ToolError, safe_relative_path


ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)
COPY_BLOCK_BYTES = 1024 * 1024


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
    try:
        resolved_root = root.resolve(strict=True)
        if not resolved_root.is_dir():
            raise ToolError(f"release root is not a directory: {root}")
        path = resolved_root.joinpath(*source_name.parts)
        if path.is_symlink() or not path.is_file():
            raise ToolError(f"release input is not a regular file: {source_name}")
        resolved = path.resolve(strict=True)
        resolved.relative_to(resolved_root)
    except ToolError:
        raise
    except (OSError, ValueError) as exc:
        raise ToolError(f"release input escapes source root: {source_name}") from exc
    if mode not in (0o644, 0o755):
        raise ToolError(f"unsupported release mode: {mode:o}")
    return ArchiveEntry(resolved, destination_name, mode)


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


@contextmanager
def _open_regular(entry: ArchiveEntry) -> Iterator[tuple[BinaryIO, os.stat_result]]:
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor: int | None = None
    try:
        descriptor = os.open(entry.source, flags)
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode):
            raise ToolError(f"release input is not regular: {entry.source}")
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = None
            yield stream, before
            after = os.fstat(stream.fileno())
            identity_before = (
                before.st_dev,
                before.st_ino,
                before.st_size,
                before.st_mtime_ns,
                before.st_ctime_ns,
            )
            identity_after = (
                after.st_dev,
                after.st_ino,
                after.st_size,
                after.st_mtime_ns,
                after.st_ctime_ns,
            )
            if identity_after != identity_before:
                raise ToolError(f"release input changed while reading: {entry.source}")
    except ToolError:
        raise
    except OSError as exc:
        raise ToolError(f"cannot read release input {entry.source}: {exc}") from exc
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _copy_stream(source: BinaryIO, destination: BinaryIO) -> None:
    for block in iter(lambda: source.read(COPY_BLOCK_BYTES), b""):
        destination.write(block)


def _temporary_file(output: Path):
    return tempfile.NamedTemporaryFile(
        mode="w+b",
        dir=output.parent,
        prefix=f".{output.name}.",
        suffix=".tmp",
        delete=False,
    )


def write_tar_gz(
    output: Path, prefix: str, entries: list[ArchiveEntry]
) -> None:
    prefix_path = safe_relative_path(prefix)
    selected = validate_entries(entries)
    temporary_path: Path | None = None
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with _temporary_file(output) as raw:
            temporary_path = Path(raw.name)
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
                with tarfile.open(
                    fileobj=gz, mode="w", format=tarfile.PAX_FORMAT
                ) as archive:
                    for entry in selected:
                        with _open_regular(entry) as (source, source_stat):
                            info = tarfile.TarInfo(
                                (prefix_path / entry.destination).as_posix()
                            )
                            info.size = source_stat.st_size
                            info.mode = entry.mode
                            info.uid = 0
                            info.gid = 0
                            info.uname = "root"
                            info.gname = "root"
                            info.mtime = 0
                            archive.addfile(info, source)
            raw.flush()
            os.fsync(raw.fileno())
        os.replace(temporary_path, output)
        temporary_path = None
    except ToolError:
        raise
    except (OSError, tarfile.TarError) as exc:
        raise ToolError(f"cannot write tar archive {output}: {exc}") from exc
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def write_zip(output: Path, entries: list[ArchiveEntry]) -> None:
    selected = validate_entries(entries)
    temporary_path: Path | None = None
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with _temporary_file(output) as temporary:
            temporary_path = Path(temporary.name)
            with zipfile.ZipFile(temporary, "w") as archive:
                for entry in selected:
                    with _open_regular(entry) as (source, source_stat):
                        info = zipfile.ZipInfo(entry.destination.as_posix(), ZIP_EPOCH)
                        info.compress_type = zipfile.ZIP_DEFLATED
                        info.create_system = 3
                        info.external_attr = (
                            (stat.S_IFREG | entry.mode) & 0xFFFF
                        ) << 16
                        info.file_size = source_stat.st_size
                        with archive.open(
                            info,
                            "w",
                            force_zip64=source_stat.st_size >= zipfile.ZIP64_LIMIT,
                        ) as destination:
                            _copy_stream(source, destination)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_path, output)
        temporary_path = None
    except ToolError:
        raise
    except (OSError, RuntimeError, zipfile.LargeZipFile) as exc:
        raise ToolError(f"cannot write ZIP archive {output}: {exc}") from exc
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
