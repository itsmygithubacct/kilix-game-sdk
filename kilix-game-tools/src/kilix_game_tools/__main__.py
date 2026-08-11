"""Command-line interface for shared Kilix game tools."""

from __future__ import annotations

import argparse
from pathlib import Path

from . import __version__
from .audio import validate_audio
from .catalog import validate_catalog
from .common import ToolError
from .contact import build_contact_sheet
from .graphics import validate_graphics


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="kilix-game-tools")
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    commands = parser.add_subparsers(dest="command", required=True)

    audio = commands.add_parser("validate-audio")
    audio.add_argument("manifest", type=Path)
    audio.add_argument("source_ledger", type=Path)

    graphics = commands.add_parser("validate-graphics")
    graphics.add_argument("manifest", type=Path)
    graphics.add_argument("--root", type=Path)
    graphics.add_argument("--no-provenance", action="store_true")

    sheet = commands.add_parser(
        "contact-sheet",
        help="composite generated art into one grid image for human review")
    sheet.add_argument("output", type=Path)
    sheet.add_argument("images", type=Path, nargs="+")
    sheet.add_argument("--columns", type=int, default=4)
    sheet.add_argument("--cell", default="320x240",
                       help="cell size as WxH, default 320x240")

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
        elif args.command == "contact-sheet":
            try:
                cell_width, _, cell_height = args.cell.partition("x")
                cell = (int(cell_width), int(cell_height))
            except ValueError as exc:
                raise ToolError(f"--cell must be WxH, got {args.cell}") from exc
            report = build_contact_sheet(
                args.output, list(args.images),
                columns=args.columns, cell=cell)
            print(
                f"PASS contact-sheet images={report.images} "
                f"grid={report.columns}x{report.rows} "
                f"size={report.width}x{report.height} out={report.output}"
            )
        else:
            content_id, launch_mode = validate_catalog(args.catalog, args.game_id)
            print(f"PASS catalog id={content_id} mode={launch_mode}")
    except ToolError as exc:
        raise SystemExit(f"kilix-game-tools: {exc}") from exc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
