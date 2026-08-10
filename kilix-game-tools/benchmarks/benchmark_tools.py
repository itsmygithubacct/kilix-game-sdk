#!/usr/bin/env python3
"""Stable archive and PPM workloads for release-gate comparisons."""

from __future__ import annotations

import argparse
from hashlib import sha256
from pathlib import Path
import tempfile
from time import perf_counter_ns

from kilix_game_tools.archive import collect_entry, write_tar_gz, write_zip
from kilix_game_tools.graphics import ppm_info


def sparse_file(path: Path, size: int, prefix: bytes = b"") -> None:
    with path.open("wb") as stream:
        stream.write(prefix)
        stream.truncate(len(prefix) + size)


def benchmark_archive(kind: str, size_mib: int) -> None:
    with tempfile.TemporaryDirectory(prefix="kilix-tools-archive-") as directory:
        root = Path(directory)
        source = root / "payload.bin"
        sparse_file(source, size_mib * 1024 * 1024)
        entry = collect_entry(root, source.name)
        output = root / ("release.tar.gz" if kind == "tar" else "release.zip")
        started = perf_counter_ns()
        if kind == "tar":
            write_tar_gz(output, "fixture-1", [entry])
        else:
            write_zip(output, [entry])
        elapsed = perf_counter_ns() - started
        digest = sha256(output.read_bytes()).hexdigest()
        print(
            f"kind={kind} input_mib={size_mib} elapsed_ms={elapsed / 1e6:.3f} "
            f"output_bytes={output.stat().st_size} sha256={digest}"
        )


def benchmark_ppm(width: int, height: int, rounds: int) -> None:
    with tempfile.TemporaryDirectory(prefix="kilix-tools-ppm-") as directory:
        path = Path(directory) / "frame.ppm"
        header = f"P6\n{width} {height}\n255\n".encode("ascii")
        sparse_file(path, width * height * 3, header)
        started = perf_counter_ns()
        result = None
        for _ in range(rounds):
            result = ppm_info(path)
        elapsed = perf_counter_ns() - started
        print(
            f"kind=ppm bytes={path.stat().st_size} rounds={rounds} "
            f"elapsed_ms={elapsed / 1e6:.3f} result={result}"
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("tar", "zip", "ppm"))
    parser.add_argument("--size-mib", type=int, default=32)
    parser.add_argument("--width", type=int, default=4096)
    parser.add_argument("--height", type=int, default=2048)
    parser.add_argument("--rounds", type=int, default=10)
    args = parser.parse_args()
    if args.kind == "ppm":
        benchmark_ppm(args.width, args.height, args.rounds)
    else:
        benchmark_archive(args.kind, args.size_mib)


if __name__ == "__main__":
    main()
