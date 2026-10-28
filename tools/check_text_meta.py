#!/usr/bin/env python3
"""Validate static game-text authoring context.

Checks:

- every ``game/text/en/*.txt`` resolves to a per-file ``<id>.meta`` or a
  prefix-based ``game/text/en/classes/<prefix>.meta``;
- every per-file/class ``.meta`` has a matching text and is non-empty;
- class prefixes are valid lowercase snake_case;
- translated retinue name pools have the same line count as English and no
  duplicate lines.

This validates structure, not the quality of the English context. Missing
context is a warning by default; ``--strict`` makes it an error.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Final

PROJECT_ROOT: Final[Path] = Path(__file__).resolve().parent.parent
TEXT_DIR: Final[Path] = PROJECT_ROOT / "game" / "text"
SOURCE_DIR: Final[Path] = TEXT_DIR / "en"
CLASS_DIR: Final[Path] = SOURCE_DIR / "classes"
PREFIX_PATTERN: Final[re.Pattern[str]] = re.compile(r"^[a-z0-9_]+$")


def read_text(path: Path) -> str:
    """Read a UTF-8 file.

    Args:
        path: File to read.

    Returns:
        The file contents, or an empty string when missing.
    """
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def class_meta_files() -> dict[str, Path]:
    """Find every class-context file.

    Returns:
        Mapping of prefix to .meta path.
    """
    if not CLASS_DIR.exists():
        return {}
    return {
        path.stem: path
        for path in sorted(CLASS_DIR.iterdir())
        if path.is_file() and path.suffix == ".meta"
    }


def matching_prefixes(text_id: str, prefixes: list[str]) -> list[str]:
    """Return matching class prefixes, broadest first.

    Args:
        text_id: Text ID.
        prefixes: Known class prefixes.

    Returns:
        Matching prefixes sorted by length.
    """
    matches: list[str] = [
        prefix for prefix in prefixes
        if text_id == prefix or text_id.startswith(prefix + "_")
    ]
    matches.sort(key=len)
    return matches


def name_pool_lines(path: Path) -> list[str]:
    """Return non-empty name-pool lines.

    Args:
        path: Name-pool text file.

    Returns:
        Stripped non-empty lines.
    """
    return [
        line.strip()
        for line in read_text(path).splitlines()
        if line.strip()
    ]


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments.

    Returns:
        Parsed namespace with strict field.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Validate game/text/en authoring context sidecars."
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        default=False,
        help="Treat missing context and orphan class context as errors",
    )
    return parser.parse_args()


def main() -> int:
    """Run the context validation.

    Returns:
        0 when valid, 1 when errors were found.
    """
    args: argparse.Namespace = parse_args()
    if not SOURCE_DIR.exists():
        print(f"Error: source text directory not found: {SOURCE_DIR}")
        return 1

    texts: dict[str, Path] = {
        path.stem: path
        for path in sorted(SOURCE_DIR.glob("*.txt"))
        if path.is_file()
    }
    per_file_metas: dict[str, Path] = {
        path.stem: path
        for path in sorted(SOURCE_DIR.glob("*.meta"))
        if path.is_file()
    }
    classes: dict[str, Path] = class_meta_files()
    prefixes: list[str] = sorted(classes.keys(), key=len, reverse=True)

    errors: list[str] = []
    warnings: list[str] = []

    for prefix, path in classes.items():
        if not PREFIX_PATTERN.fullmatch(prefix):
            errors.append(
                f"{path.relative_to(PROJECT_ROOT)}: class prefix must be "
                "lowercase snake_case"
            )
        if not read_text(path).strip():
            errors.append(f"{path.relative_to(PROJECT_ROOT)}: empty context")
        if not any(matching_prefixes(text_id, [prefix]) for text_id in texts):
            message = (
                f"{path.relative_to(PROJECT_ROOT)}: class context matches no "
                "current text"
            )
            if args.strict:
                errors.append(message)
            else:
                warnings.append(message)

    for text_id, path in per_file_metas.items():
        if text_id not in texts:
            errors.append(
                f"{path.relative_to(PROJECT_ROOT)}: per-file context has no "
                f"matching {text_id}.txt"
            )
        if not read_text(path).strip():
            errors.append(f"{path.relative_to(PROJECT_ROOT)}: empty context")

    missing_context: list[str] = []
    for text_id in texts:
        class_matches: list[str] = matching_prefixes(text_id, prefixes)
        if not class_matches and text_id not in per_file_metas:
            missing_context.append(text_id)
    for text_id in missing_context:
        message = f"{text_id}.txt: no class or per-file context"
        if args.strict:
            errors.append(message)
        else:
            warnings.append(message)

    name_pool_errors: list[str] = []
    source_names: dict[str, list[str]] = {}
    for path in sorted(SOURCE_DIR.glob("names_*.txt")):
        lines: list[str] = name_pool_lines(path)
        source_names[path.stem] = lines
        if not lines:
            name_pool_errors.append(f"{path.relative_to(PROJECT_ROOT)}: no names")
            continue
        folded: list[str] = [line.casefold() for line in lines]
        if len(set(folded)) != len(folded):
            name_pool_errors.append(
                f"{path.relative_to(PROJECT_ROOT)}: duplicate names"
            )

    for language_dir in sorted(TEXT_DIR.iterdir()):
        if not language_dir.is_dir() or language_dir.name == "en":
            continue
        for target_path in sorted(language_dir.glob("names_*.txt")):
            source_lines: list[str] | None = source_names.get(target_path.stem)
            if source_lines is None:
                name_pool_errors.append(
                    f"{target_path.relative_to(PROJECT_ROOT)}: no English source"
                )
                continue
            target_lines: list[str] = name_pool_lines(target_path)
            if len(target_lines) != len(source_lines):
                name_pool_errors.append(
                    f"{target_path.relative_to(PROJECT_ROOT)}: expected "
                    f"{len(source_lines)} names, got {len(target_lines)}"
                )
            if any(line[:1].isdigit() for line in target_lines):
                name_pool_errors.append(
                    f"{target_path.relative_to(PROJECT_ROOT)}: name lines "
                    "must not be numbered"
                )
            folded_target: list[str] = [
                line.casefold() for line in target_lines
            ]
            if len(set(folded_target)) != len(folded_target):
                name_pool_errors.append(
                    f"{target_path.relative_to(PROJECT_ROOT)}: duplicate names"
                )

    errors.extend(name_pool_errors)

    print(
        f"Checked {len(texts)} text(s), {len(per_file_metas)} per-file "
        f"context(s), {len(classes)} class context(s)."
    )
    if warnings:
        print(f"\nWarnings ({len(warnings)}):")
        for message in warnings:
            print(f"  - {message}")
    if errors:
        print(f"\nErrors ({len(errors)}):")
        for message in errors:
            print(f"  - {message}")
        return 1

    print("\nAll text context valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
