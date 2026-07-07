#!/usr/bin/env python3
"""Generate missing translations for game text using OpenRouter AI.

Scans English source texts in text/en/, compares against target language
directories, and calls the OpenRouter chat completions API to translate any
missing text IDs. Uses text/context.md for game context and
text/translation_instructions.md for style/terminology rules.

Usage:
    # Preview what would be translated for Spanish and German
    ./tools/generate_translations.py --target es de --dry-run

    # Generate translations for all target languages
    ./tools/generate_translations.py --target es de fr it ar zh-CN ko ja

    # Generate a specific text ID only
    ./tools/generate_translations.py --target es --text-id ui_login_title

    # Regenerate existing translations (overwrite)
    ./tools/generate_translations.py --target fr --force

    # Use a custom config path
    ./tools/generate_translations.py --target de --config /path/to/config.json

Exit codes:
    0: All translations generated successfully (or dry-run complete)
    1: Configuration error (missing/invalid config, API key, etc.)
    2: Some translations failed (partial success)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

PROJECT_ROOT: Final[Path] = Path(__file__).resolve().parent.parent
TEXT_DIR: Final[Path] = PROJECT_ROOT / "text"
SOURCE_LANG: Final[str] = "en"
DEFAULT_CONFIG_PATH: Final[Path] = TEXT_DIR / "config.json"
CONTEXT_FILE: Final[Path] = TEXT_DIR / "context.md"
INSTRUCTIONS_FILE: Final[Path] = TEXT_DIR / "translation_instructions.md"
MAX_RETRIES: Final[int] = 3
RETRY_BACKOFF_SECONDS: Final[list[float]] = [1.0, 2.0, 4.0]
REQUEST_TIMEOUT_SECONDS: Final[int] = 60

# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class TranslationConfig:
    """Configuration loaded from config.json.

    Attributes:
        openrouter_api_key: API key for OpenRouter authentication.
        model: Model identifier string (e.g. \"openai/gpt-4o\").
        base_url: Base URL for the OpenRouter API.
    """

    openrouter_api_key: str
    model: str
    base_url: str


@dataclass(frozen=True)
class TranslationReport:
    """Summary of a translation generation run.

    Attributes:
        generated: How many new translations were written.
        skipped: How many were skipped (already exist, not forced).
        errors: List of error messages encountered.
    """

    generated: int = 0
    skipped: int = 0
    errors: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    """Parse and validate command-line arguments.

    Returns:
        Parsed arguments namespace with fields: target, text_id, config,
        dry_run, force, verbose.

    Raises:
        SystemExit: If no target languages specified or other arg errors.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Generate missing game text translations using OpenRouter AI.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  %(prog)s --target es de --dry-run\n"
            "  %(prog)s --target fr --text-id ui_login_title\n"
            "  %(prog)s --target es de fr it ar zh-CN ko ja\n"
        ),
    )

    parser.add_argument(
        "--target", "-t",
        type=str,
        nargs="+",
        required=True,
        help="Target language code(s) to generate translations for "
             "(e.g. es de fr it ar zh-CN ko ja)",
    )

    parser.add_argument(
        "--text-id", "-i",
        type=str,
        nargs="+",
        default=None,
        help="Only translate the specified text ID(s) instead of all missing ones",
    )

    parser.add_argument(
        "--config", "-c",
        type=str,
        default=None,
        help=f"Path to config.json (default: {DEFAULT_CONFIG_PATH})",
    )

    parser.add_argument(
        "--dry-run", "-n",
        action="store_true",
        default=False,
        help="Preview what would be translated without calling the API",
    )

    parser.add_argument(
        "--force", "-f",
        action="store_true",
        default=False,
        help="Overwrite existing translations instead of skipping them",
    )

    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        default=False,
        help="Show detailed output including API responses",
    )

    return parser.parse_args()


# ---------------------------------------------------------------------------
# File I/O helpers
# ---------------------------------------------------------------------------


def load_config(config_path: Path) -> TranslationConfig:
    """Load and validate the translation configuration file.

    Reads the JSON config file and validates that all required fields
    are present and non-empty.

    Args:
        config_path: Path to the config.json file.

    Returns:
        A validated TranslationConfig instance.

    Raises:
        FileNotFoundError: If config_path does not exist.
        ValueError: If the config is missing required fields or has
            an invalid format.
        json.JSONDecodeError: If config is not valid JSON.
    """
    if not config_path.exists():
        raise FileNotFoundError(
            f"Config file not found: {config_path}\n"
            f"Create it with at least:\n"
            f'  {{"openrouter_api_key": "YOUR_KEY", "model": "openai/gpt-4o", '
            f'"base_url": "https://openrouter.ai/api/v1"}}'
        )

    with open(config_path, "r", encoding="utf-8") as file_handle:
        raw: dict[str, object] = json.load(file_handle)

    api_key: object = raw.get("openrouter_api_key", "")
    model: object = raw.get("model", "")
    base_url: object = raw.get("base_url", "")

    missing_fields: list[str] = []
    if not isinstance(api_key, str) or not api_key:
        missing_fields.append("openrouter_api_key")
    if not isinstance(model, str) or not model:
        missing_fields.append("model")
    if not isinstance(base_url, str) or not base_url:
        missing_fields.append("base_url")

    if missing_fields:
        raise ValueError(
            f"Config file {config_path} is missing or has empty "
            f"required fields: {', '.join(missing_fields)}"
        )

    return TranslationConfig(
        openrouter_api_key=api_key,
        model=model,
        base_url=base_url,
    )


def load_text_file(file_path: Path) -> str:
    """Read a UTF-8 text file and return its contents with trailing whitespace stripped.

    Args:
        file_path: Path to the file to read.

    Returns:
        The file contents as a string. Returns empty string if the file
        does not exist or is empty.

    Raises:
        OSError: If the file cannot be read for reasons other than
            non-existence.
    """
    try:
        with open(file_path, "r", encoding="utf-8") as file_handle:
            content: str = file_handle.read()
            return content.rstrip("\n")
    except FileNotFoundError:
        return ""


def write_text_file(file_path: Path, content: str) -> None:
    """Write a UTF-8 text file with a single trailing newline.

    Ensures the parent directory exists, creating it if necessary.

    Args:
        file_path: Path where the file should be written.
        content: The text content to write (trailing newline added).

    Raises:
        OSError: If the file cannot be written.
    """
    file_path.parent.mkdir(parents=True, exist_ok=True)
    with open(file_path, "w", encoding="utf-8") as file_handle:
        file_handle.write(content)
        file_handle.write("\n")


# ---------------------------------------------------------------------------
# Context loading
# ---------------------------------------------------------------------------


def load_context() -> str:
    """Load the game context from context.md for the AI system prompt.

    Returns the file contents, or a default context string if the file
    does not exist.

    Returns:
        The game context as a string.
    """
    if CONTEXT_FILE.exists():
        return load_text_file(CONTEXT_FILE)
    return "Medieval fantasy strategy game called Ravenest: Build and Battle."


def load_translation_instructions() -> str:
    """Load translation style instructions from translation_instructions.md.

    Returns the file contents, or a default set of instructions if the
    file does not exist.

    Returns:
        The translation instructions as a string.
    """
    if INSTRUCTIONS_FILE.exists():
        return load_text_file(INSTRUCTIONS_FILE)
    return (
        "Translate the following English game text into the target language. "
        "Preserve any {placeholder} variables exactly. "
        "Keep proper names untranslated. "
        "Return ONLY the translation, no explanations."
    )


# ---------------------------------------------------------------------------
# Source text scanning
# ---------------------------------------------------------------------------


def get_text_id(file_path: Path) -> str:
    """Extract the text ID from a .txt file path.

    The text ID is the filename without the .txt extension.

    Args:
        file_path: Path to a .txt file (e.g. \".../en/ui_login_title.txt\").

    Returns:
        The text ID string (e.g. \"ui_login_title\").
    """
    return file_path.stem


def load_source_texts(source_dir: Path) -> dict[str, str]:
    """Load all English source texts from the source language directory.

    Scans for all *.txt files and returns a mapping of text ID to content.

    Args:
        source_dir: Path to the source language directory (e.g. text/en).

    Returns:
        Dictionary mapping text IDs to their content strings.
        Only includes non-empty files.
    """
    source_texts: dict[str, str] = {}

    if not source_dir.exists():
        print(f"Warning: Source directory not found: {source_dir}", file=sys.stderr)
        return source_texts

    for file_path in sorted(source_dir.iterdir()):
        if not file_path.is_file() or file_path.suffix != ".txt":
            continue

        text_id: str = get_text_id(file_path)
        content: str = load_text_file(file_path)

        if not content:
            print(f"Warning: Empty source text: {text_id} — skipping", file=sys.stderr)
            continue

        source_texts[text_id] = content

    return source_texts


def get_existing_ids(lang_dir: Path) -> set[str]:
    """Get the set of text IDs that already exist in a language directory.

    Args:
        lang_dir: Path to the target language directory.

    Returns:
        Set of text IDs that have existing translation files.
    """
    if not lang_dir.exists():
        return set()

    existing_ids: set[str] = set()
    for file_path in lang_dir.iterdir():
        if file_path.is_file() and file_path.suffix == ".txt":
            existing_ids.add(get_text_id(file_path))

    return existing_ids


def get_missing_ids(
    source_texts: dict[str, str],
    existing_ids: set[str],
    force: bool,
) -> list[str]:
    """Determine which text IDs need translation.

    Args:
        source_texts: All available source texts (id → content).
        existing_ids: IDs that already have translations.
        force: If True, include existing IDs for regeneration.

    Returns:
        Sorted list of text IDs that need translation.
    """
    if force:
        return sorted(source_texts.keys())

    return sorted(set(source_texts.keys()) - existing_ids)


# ---------------------------------------------------------------------------
# OpenRouter API
# ---------------------------------------------------------------------------


def build_system_prompt(context: str, instructions: str) -> str:
    """Build the system prompt for the AI translation model.

    Combines game context and translation instructions into a single
    system-level prompt.

    Args:
        context: Game/world context from context.md.
        instructions: Translation style instructions.

    Returns:
        The assembled system prompt string.
    """
    return (
        "You are a professional game translator specializing in "
        "medieval fantasy strategy games.\n\n"
        "## Game Context\n"
        f"{context}\n\n"
        "## Translation Instructions\n"
        f"{instructions}"
    )


def build_user_prompt(text_id: str, source_text: str, target_language: str) -> str:
    """Build the user prompt for translating a single text.

    Args:
        text_id: The text identifier (for context/debugging).
        source_text: The English source text to translate.
        target_language: Target language name or code.

    Returns:
        The user prompt string for the API call.
    """
    return (
        f"Translate the following English game text to {target_language}.\n"
        f"Text ID: {text_id}\n\n"
        f"{source_text}"
    )


def call_openrouter(
    config: TranslationConfig,
    system_prompt: str,
    user_prompt: str,
    verbose: bool,
) -> str | None:
    """Call the OpenRouter chat completions API to get a translation.

    Makes a POST request to the OpenRouter API with retry logic and
    exponential backoff.

    Args:
        config: TranslationConfig with API key, model, and base URL.
        system_prompt: The system-level prompt with context and instructions.
        user_prompt: The user prompt with the text to translate.
        verbose: If True, print API request/response details.

    Returns:
        The translated text string on success, or None if all retries fail.

    Raises:
        SystemExit: If the API key is empty (configuration error).
    """
    api_url: str = f"{config.base_url.rstrip('/')}/chat/completions"
    model: str = config.model

    request_body: dict[str, object] = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "temperature": 0.3,
        "max_tokens": 1024,
    }

    encoded_body: bytes = json.dumps(request_body).encode("utf-8")

    headers: dict[str, str] = {
        "Authorization": f"Bearer {config.openrouter_api_key}",
        "Content-Type": "application/json",
        "HTTP-Referer": "https://github.com/anomalyco/Ravenest_Build_and_Battle",
        "X-Title": "Ravenest Build and Battle Translation Tool",
    }

    last_error: str | None = None

    for attempt in range(1, MAX_RETRIES + 1):
        if verbose:
            print(f"  [API] Request to {model} (attempt {attempt}/{MAX_RETRIES})")

        try:
            request_obj: urllib.request.Request = urllib.request.Request(
                url=api_url,
                data=encoded_body,
                headers=headers,
                method="POST",
            )

            with urllib.request.urlopen(
                request_obj, timeout=REQUEST_TIMEOUT_SECONDS
            ) as response:
                response_raw: bytes = response.read()
                response_data: dict[str, object] = json.loads(
                    response_raw.decode("utf-8")
                )

            if verbose:
                print(f"  [API] Response received: {json.dumps(response_data, indent=2)[:500]}")

            # Extract the translated text from the response
            choices: object = response_data.get("choices")
            if not isinstance(choices, list) or not choices:
                last_error = f"API returned empty choices: {response_data}"
                if verbose:
                    print(f"  [API] {last_error}")
                continue

            first_choice: object = choices[0]
            if not isinstance(first_choice, dict):
                last_error = f"API choice is not an object: {first_choice}"
                if verbose:
                    print(f"  [API] {last_error}")
                continue

            message: object = first_choice.get("message")
            if not isinstance(message, dict):
                last_error = f"API message is not an object: {first_choice}"
                if verbose:
                    print(f"  [API] {last_error}")
                continue

            content: object = message.get("content")
            if not isinstance(content, str):
                last_error = f"API content is not a string: {message}"
                if verbose:
                    print(f"  [API] {last_error}")
                continue

            translated: str = content.strip()
            if not translated:
                last_error = "API returned empty translation"
                if verbose:
                    print(f"  [API] {last_error}")
                continue

            return translated

        except urllib.error.HTTPError as http_error:
            status_code: int = http_error.code
            error_body: str = http_error.read().decode("utf-8", errors="replace")
            last_error = f"HTTP {status_code}: {error_body[:300]}"

            if verbose:
                print(f"  [API] {last_error}")

            # Do not retry auth errors
            if status_code in (401, 403):
                print(
                    f"Error: Authentication failed (HTTP {status_code}). "
                    f"Check your openrouter_api_key in the config file.",
                    file=sys.stderr,
                )
                return None

        except urllib.error.URLError as url_error:
            last_error = f"Network error: {url_error.reason}"
            if verbose:
                print(f"  [API] {last_error}")

        except (json.JSONDecodeError, OSError, ValueError) as other_error:
            last_error = f"{type(other_error).__name__}: {other_error}"
            if verbose:
                print(f"  [API] {last_error}")

        # Exponential backoff before retry
        if attempt < MAX_RETRIES:
            backoff: float = RETRY_BACKOFF_SECONDS[attempt - 1]
            if verbose:
                print(f"  [API] Retrying in {backoff}s...")
            time.sleep(backoff)

    if verbose:
        print(f"  [API] All {MAX_RETRIES} attempts failed")
    return None


# ---------------------------------------------------------------------------
# Language name mapping
# ---------------------------------------------------------------------------


def get_language_display_name(lang_code: str) -> str:
    """Get the human-readable language name for a language code.

    Used in prompts sent to the AI model so it knows what language
    to translate into.

    Args:
        lang_code: Language code (e.g. \"es\", \"zh-CN\").

    Returns:
        Human-readable language name (e.g. \"Spanish\", \"Simplified Chinese\").
    """
    language_names: dict[str, str] = {
        "en": "English",
        "es": "Spanish",
        "de": "German",
        "fr": "French",
        "it": "Italian",
        "ar": "Arabic",
        "zh-CN": "Simplified Chinese",
        "ko": "Korean",
        "ja": "Japanese",
    }

    return language_names.get(lang_code, lang_code)


# ---------------------------------------------------------------------------
# Main translation generation
# ---------------------------------------------------------------------------


def translate_language(
    config: TranslationConfig,
    source_lang: str,
    target_lang: str,
    text_ids_to_generate: list[str] | None,
    force: bool,
    dry_run: bool,
    verbose: bool,
    report: TranslationReport,
) -> None:
    """Generate translations for a single target language.

    Loads source texts, diffs against existing translations, and calls
    the OpenRouter API for each missing text.

    Args:
        config: API configuration.
        source_lang: Source language code (usually \"en\").
        target_lang: Target language code to generate for.
        text_ids_to_generate: If set, only generate these specific IDs.
            If None, generate all missing IDs.
        force: If True, regenerate existing translations.
        dry_run: If True, only print what would be done, no API calls.
        verbose: If True, print detailed output.
        report: TranslationReport to update with results.
    """
    source_dir: Path = TEXT_DIR / source_lang
    target_dir: Path = TEXT_DIR / target_lang
    language_name: str = get_language_display_name(target_lang)

    print(f"\n{'='*60}")
    print(f"Target: {language_name} ({target_lang})")
    print(f"{'='*60}")

    source_texts: dict[str, str] = load_source_texts(source_dir)
    if not source_texts:
        print(f"  → No source texts found in {source_lang}/. Skipping.")
        return

    existing_ids: set[str] = get_existing_ids(target_dir)

    if text_ids_to_generate is not None:
        # Filter to only requested IDs
        missing_ids: list[str] = sorted(
            tid for tid in text_ids_to_generate
            if tid in source_texts
            and (force or tid not in existing_ids)
        )
        skipped_ids: list[str] = [
            tid for tid in text_ids_to_generate
            if tid not in source_texts
        ]
        if skipped_ids:
            print(f"  Warning: Requested IDs not found in source: {skipped_ids}")
    else:
        missing_ids = get_missing_ids(source_texts, existing_ids, force)

    if not missing_ids:
        print(f"  → No missing translations. All {len(source_texts)} texts "
              f"are already present in {target_lang}/"
              f"{' (use --force to regenerate)' if not force else ''}.")
        report.skipped += len(source_texts)
        return

    total: int = len(missing_ids)
    print(f"  → {total} text(s) to translate")

    if dry_run:
        print(f"\n  Would translate ({target_lang}):")
        for text_id in missing_ids:
            source_text: str = source_texts[text_id]
            preview: str = source_text[:60].replace("\n", "\\n")
            print(f"    - {text_id}: \"{preview}\"")
        return

    # Load context and instructions for the system prompt
    context: str = load_context()
    instructions: str = load_translation_instructions()
    system_prompt: str = build_system_prompt(context, instructions)

    # Also load them once as a combined cache
    successful_count: int = 0
    failed_count: int = 0

    for index, text_id in enumerate(missing_ids, start=1):
        source_text = source_texts[text_id]
        user_prompt: str = build_user_prompt(text_id, source_text, language_name)

        print(f"\n  [{index}/{total}] Translating '{text_id}'...", end=" ")

        translation: str | None = call_openrouter(
            config=config,
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            verbose=verbose,
        )

        if translation is None:
            print("FAILED")
            report.errors.append(f"{target_lang}/{text_id}: API error after {MAX_RETRIES} retries")
            failed_count += 1
            continue

        target_path: Path = target_dir / f"{text_id}.txt"
        write_text_file(target_path, translation)
        print("OK")
        successful_count += 1

        # Brief pause between requests to avoid rate limiting
        if index < total:
            time.sleep(0.5)

    report.generated += successful_count
    print(f"\n  Result: {successful_count} generated, {failed_count} failed")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    """Main entry point for the translation generation script.

    Parses arguments, loads config, and runs translation for each
    target language. Returns an exit code.

    Returns:
        0 on success (all languages processed without config errors).
        1 on configuration error.
        2 if some translations failed (partial success).
    """
    args: argparse.Namespace = parse_args()

    # Resolve config path
    if args.config is not None:
        config_path: Path = Path(args.config).resolve()
    else:
        config_path = DEFAULT_CONFIG_PATH

    # Load and validate configuration
    try:
        config: TranslationConfig = load_config(config_path)
    except (FileNotFoundError, json.JSONDecodeError, ValueError) as config_error:
        print(f"Configuration error: {config_error}", file=sys.stderr)
        return 1

    if not config.openrouter_api_key:
        print(
            "Error: openrouter_api_key is empty. Set it in config.json:\n"
            f"  {config_path}\n\n"
            "Get a key at https://openrouter.ai/keys",
            file=sys.stderr,
        )
        return 1

    # Validate text directory exists
    if not TEXT_DIR.exists():
        print(f"Error: Text directory not found: {TEXT_DIR}", file=sys.stderr)
        return 1

    # Validate source language directory
    source_dir: Path = TEXT_DIR / SOURCE_LANG
    if not source_dir.exists():
        print(
            f"Error: Source language directory not found: {source_dir}/",
            file=sys.stderr,
        )
        return 1

    # Optionally validate target codes against known list
    known_languages: set[str] = {
        "en", "es", "de", "fr", "it", "ar", "zh-CN", "ko", "ja",
    }
    unknown_targets: list[str] = [
        lang for lang in args.target if lang not in known_languages
    ]
    if unknown_targets:
        print(
            f"Warning: Unknown language code(s): {unknown_targets}. "
            f"Proceeding anyway.",
            file=sys.stderr,
        )

    # Run translation for each target language
    overall_report: TranslationReport = TranslationReport()

    for target_lang in args.target:
        translate_language(
            config=config,
            source_lang=SOURCE_LANG,
            target_lang=target_lang,
            text_ids_to_generate=args.text_id,
            force=args.force,
            dry_run=args.dry_run,
            verbose=args.verbose,
            report=overall_report,
        )

    # Print summary
    print(f"\n{'='*60}")
    print("Summary")
    print(f"{'='*60}")
    print(f"  Generated:  {overall_report.generated}")
    print(f"  Skipped:    {overall_report.skipped}")
    if overall_report.errors:
        print(f"  Errors:     {len(overall_report.errors)}")
        if args.verbose:
            for error in overall_report.errors:
                print(f"    - {error}")

    if overall_report.errors:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
