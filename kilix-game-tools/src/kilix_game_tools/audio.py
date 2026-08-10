"""Validate generated WAV manifests and CC0/public-domain source ledgers."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from .common import (
    ToolError,
    load_json_object,
    require_sha256,
    resolve_regular_file,
    safe_relative_path,
    sha256_file,
)


ALLOWED_LICENSES = frozenset({"CC0", "CC0 1.0", "Public Domain"})


@dataclass(frozen=True)
class AudioReport:
    artifacts: int
    foley_artifacts: int
    collections: int


def _records(value: object, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        raise ToolError(f"{context} must be a list of objects")
    return value


def _is_https_url(value: object) -> bool:
    if (
        not isinstance(value, str)
        or not value
        or any(character.isspace() for character in value)
    ):
        return False
    try:
        parsed = urlsplit(value)
        return (
            parsed.scheme == "https"
            and parsed.hostname is not None
            and parsed.username is None
            and parsed.password is None
        )
    except ValueError:
        return False


def validate_audio(manifest_path: Path, ledger_path: Path) -> AudioReport:
    manifest = load_json_object(manifest_path)
    ledger = load_json_object(ledger_path)
    artifacts = _records(manifest.get("artifacts"), "audio artifacts")
    collections = _records(ledger.get("collections"), "source collections")
    if manifest.get("schema_version") != 1:
        raise ToolError("unsupported generated-audio manifest schema")
    if manifest.get("source_licensing") != "CC0 1.0":
        raise ToolError("generated-audio manifest is not CC0-only")
    if ledger.get("schema_version") != 1:
        raise ToolError("unsupported audio source-ledger schema")
    for field in ("missing", "invalid"):
        unresolved = ledger.get(field)
        if not isinstance(unresolved, list):
            raise ToolError(f"audio source ledger {field} must be a list")
        if unresolved:
            raise ToolError("audio source ledger reports unresolved inputs")

    for collection in collections:
        if collection.get("license") not in ALLOWED_LICENSES:
            raise ToolError("audio source ledger contains a non-CC0 source")
        source_page = collection.get("source_page")
        owner = collection.get("owner_provenance")
        owner_safe = False
        if isinstance(owner, str):
            try:
                owner_safe = safe_relative_path(owner).name == "provenance.json"
            except ToolError:
                owner_safe = False
        if not _is_https_url(source_page) and not owner_safe:
            raise ToolError("audio source ledger has no URL or owning ledger")

    foley = 0
    root = manifest_path.parent
    artifact_paths: set[str] = set()
    for artifact in artifacts:
        logical = safe_relative_path(artifact.get("file"), suffix=".wav")
        if logical.as_posix() in artifact_paths:
            raise ToolError(f"duplicate audio artifact path: {logical}")
        artifact_paths.add(logical.as_posix())
        path = resolve_regular_file(root, logical.as_posix(), suffix=".wav")
        expected = require_sha256(
            artifact.get("sha256"), context=logical.as_posix()
        )
        if sha256_file(path) != expected:
            raise ToolError(f"audio checksum mismatch: {logical}")
        sources = artifact.get("sources")
        if not isinstance(sources, list) or any(
            not isinstance(item, str) for item in sources
        ):
            raise ToolError(f"invalid source list: {logical}")
        source_paths: set[str] = set()
        for source in sources:
            source_path = safe_relative_path(source).as_posix()
            if source_path in source_paths:
                raise ToolError(f"duplicate audio source: {logical}: {source_path}")
            source_paths.add(source_path)
        if sources:
            foley += 1
    return AudioReport(len(artifacts), foley, len(collections))
