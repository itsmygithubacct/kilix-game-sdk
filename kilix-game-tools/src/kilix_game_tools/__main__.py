"""Command-line interface for shared Kilix game tools."""

from __future__ import annotations

import argparse
from pathlib import Path

from .audio import validate_audio
from .catalog import validate_catalog
from .common import ToolError
from .graphics import validate_graphics


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="kilix-game-tools")
    commands = parser.add_subparsers(dest="command", required=True)

    audio = commands.add_parser("validate-audio")
    audio.add_argument("manifest", type=Path)
    audio.add_argument("source_ledger", type=Path)

    graphics = commands.add_parser("validate-graphics")
    graphics.add_argument("manifest", type=Path)
    graphics.add_argument("--root", type=Path)
    graphics.add_argument("--no-provenance", action="store_true")

    catalog = commands.add_parser("validate-catalog")
    catalog.add_argument("catalog", type=Path)
    catalog.add_argument("game_id")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "validate-audio":
            report = validate_audio(args.manifest, args.source_ledger)
            print(
                f"PASS audio-manifest wavs={report.artifacts} "
                f"foley={report.foley_artifacts} "
                f"collections={report.collections}"
            )
        elif args.command == "validate-graphics":
            report = validate_graphics(
                args.manifest,
                root=args.root,
                require_provenance=not args.no_provenance,
            )
            print(
                f"PASS graphics game={report.game} atlases={report.atlases} "
                f"bitmaps={report.bitmaps} clean-room=yes"
            )
        else:
            content_id, launch_mode = validate_catalog(args.catalog, args.game_id)
            print(f"PASS catalog id={content_id} mode={launch_mode}")
    except ToolError as exc:
        raise SystemExit(f"kilix-game-tools: {exc}") from exc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
