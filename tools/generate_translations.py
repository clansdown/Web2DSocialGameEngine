#!/usr/bin/env python3
"""Generate missing translations for game text using OpenRouter AI.

Scans English source texts in game/text/en/, compares against target language
directories, and calls the OpenRouter chat completions API to translate any
missing or stale text IDs. Uses game/text/context.md for game context,
game/text/translation_instructions.md for style/terminology rules, and the
per-text / per-class .meta sidecars for English authoring context.

Usage:
    # Preview what would be translated for Spanish and German
    ./tools/generate_translations.py --target es de --dry-run

    # Generate translations for all target languages
    ./tools/generate_translations.py --target es de fr it ar zh-CN ko ja

    # Generate a specific text ID only
    ./tools/generate_translations.py --target es --text-id ui_login_title

    # Regenerate existing translations (overwrite)
    ./tools/generate_translations.py --target fr --force

    # Fail on any text with no class/per-file .meta context
    ./tools/generate_translations.py --target es --require-meta

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
TEXT_DIR: Final[Path] = PROJECT_ROOT / "game" / "text"
SOURCE_LANG: Final[str] = "en"
DEFAULT_CONFIG_PATH: Final[Path] = TEXT_DIR / "config.json"
CONTEXT_FILE: Final[Path] = TEXT_DIR / "context.md"
INSTRUCTIONS_FILE: Final[Path] = TEXT_DIR / "translation_instructions.md"
CLASS_CONTEXT_DIR_NAME: Final[str] = "classes"
MAX_RETRIES: Final[int] = 3
NAME_VALIDATION_ATTEMPTS: Final[int] = 2
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


@dataclass(frozen=True)
class ResolvedTextContext:
    """English authoring context for one text ID.

    The translator passes this to the model inside a <context> block. Context
    is inherited from every matching class prefix (broadest first), then the
    per-file sidecar adds specifics.

    Attributes:
        class_contexts: (prefix, content, path) tuples, broadest prefix first.
        file_context: Per-file .meta content, or None.
        meta_paths: Every context file that exists for this text.
        newest_mtime: Newest mtime among the context files (0 when none).
    """

    class_contexts: tuple[tuple[str, str, Path], ...] = ()
    file_context: str | None = None
    meta_paths: tuple[Path, ...] = ()
    newest_mtime: float = 0.0

    @property
    def has_context(self) -> bool:
        """True when at least one class or per-file context exists."""
        return bool(self.class_contexts or self.file_context)


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

    parser.add_argument(
        "--require-meta",
        action="store_true",
        default=False,
        help="Fail translation for any text with no class or per-file .meta context",
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
# Per-text / per-class context
# ---------------------------------------------------------------------------


def _safe_mtime(path: Path) -> float:
    """Return a file's mtime, or 0.0 when it does not exist.

    Args:
        path: File to inspect.

    Returns:
        The POSIX mtime as a float, or 0.0 on any stat failure.
    """
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


def load_text_meta(meta_path: Path) -> str:
    """Load one free-form English context sidecar.

    Args:
        meta_path: Path to a .meta file.

    Returns:
        The trimmed context text, or an empty string when missing/empty.
    """
    return load_text_file(meta_path).strip()


def class_meta_files(source_dir: Path) -> dict[str, Path]:
    """Find every prefix-based class context file.

    Class contexts live in `<source_dir>/classes/<prefix>.meta`. A prefix
    applies to `text_id == prefix` or `text_id.startswith(prefix + "_")`.

    Args:
        source_dir: English source directory (e.g. game/text/en).

    Returns:
        Mapping of prefix to .meta path, sorted by prefix.
    """
    directory: Path = source_dir / CLASS_CONTEXT_DIR_NAME
    if not directory.exists():
        return {}

    result: dict[str, Path] = {}
    for meta_path in sorted(directory.iterdir()):
        if meta_path.is_file() and meta_path.suffix == ".meta":
            result[meta_path.stem] = meta_path
    return result


def resolve_text_context(
    source_dir: Path,
    text_id: str,
    class_meta_paths: dict[str, Path] | None = None,
) -> ResolvedTextContext:
    """Resolve all class + per-file context for one text ID.

    Class contexts are accumulated broadest-prefix first, so a name pool can
    inherit `names` then `names_knight`, and a per-file sidecar adds specifics.

    Args:
        source_dir: English source directory.
        text_id: Text ID (filename stem).
        class_meta_paths: Optional pre-scanned class map.

    Returns:
        ResolvedTextContext containing content, paths, and newest mtime.
    """
    if class_meta_paths is None:
        class_meta_paths = class_meta_files(source_dir)

    class_contexts: list[tuple[str, str, Path]] = []
    for prefix, meta_path in class_meta_paths.items():
        if text_id != prefix and not text_id.startswith(prefix + "_"):
            continue
        content: str = load_text_meta(meta_path)
        if content:
            class_contexts.append((prefix, content, meta_path))
    class_contexts.sort(key=lambda item: len(item[0]))

    file_path: Path = source_dir / f"{text_id}.meta"
    file_context: str | None = None
    if file_path.exists():
        file_context = load_text_meta(file_path) or None

    meta_paths: list[Path] = [item[2] for item in class_contexts]
    if file_context is not None:
        meta_paths.append(file_path)

    newest_mtime: float = max((_safe_mtime(path) for path in meta_paths), default=0.0)
    return ResolvedTextContext(
        class_contexts=tuple(class_contexts),
        file_context=file_context,
        meta_paths=tuple(meta_paths),
        newest_mtime=newest_mtime,
    )


def render_context_block(context: ResolvedTextContext) -> str:
    """Render resolved context as a single <context> block.

    Args:
        context: Resolved class + per-file context.

    Returns:
        The wrapped context block, or an empty string when there is none.
    """
    if not context.has_context:
        return ""

    sections: list[str] = []
    for prefix, content, _ in context.class_contexts:
        sections.append(
            f'Class context for text IDs prefixed "{prefix}":\n{content}'
        )
    if context.file_context:
        sections.append(f"Text-specific context:\n{context.file_context}")

    return "<context>\n" + "\n\n".join(sections) + "\n</context>"


def is_translation_stale(
    source_path: Path,
    target_path: Path,
    context: ResolvedTextContext,
    global_paths: tuple[Path, ...],
) -> bool:
    """Check whether a target translation is missing or older than its inputs.

    Args:
        source_path: English source .txt.
        target_path: Existing target translation .txt.
        context: Resolved context (its newest mtime is included).
        global_paths: Global context/instruction files.

    Returns:
        True when the target is missing, empty, or older than any input.
    """
    try:
        target_mtime: float = target_path.stat().st_mtime
        if target_path.stat().st_size == 0:
            return True
    except OSError:
        return True

    newest_input: float = max(
        [_safe_mtime(source_path), context.newest_mtime]
        + [_safe_mtime(path) for path in global_paths]
    )
    return newest_input > target_mtime


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


def get_ids_to_translate(
    source_texts: dict[str, str],
    source_dir: Path,
    target_dir: Path,
    force: bool,
    global_paths: tuple[Path, ...],
    class_meta_paths: dict[str, Path] | None = None,
) -> tuple[list[str], int, int]:
    """Determine which text IDs are missing or stale.

    Args:
        source_texts: All available source texts (id → content).
        source_dir: English source directory.
        target_dir: Target language directory.
        force: If True, include every source ID.
        global_paths: Global context/instruction files.
        class_meta_paths: Optional pre-scanned class context map.

    Returns:
        A tuple of (IDs to translate, missing count, stale count).
    """
    existing_ids: set[str] = get_existing_ids(target_dir)
    if class_meta_paths is None:
        class_meta_paths = class_meta_files(source_dir)

    ids_to_translate: list[str] = []
    missing_count: int = 0
    stale_count: int = 0

    for text_id in sorted(source_texts.keys()):
        if force or text_id not in existing_ids:
            if text_id not in existing_ids:
                missing_count += 1
            ids_to_translate.append(text_id)
            continue

        context: ResolvedTextContext = resolve_text_context(
            source_dir, text_id, class_meta_paths
        )
        if is_translation_stale(
            source_dir / f"{text_id}.txt",
            target_dir / f"{text_id}.txt",
            context,
            global_paths,
        ):
            stale_count += 1
            ids_to_translate.append(text_id)

    return ids_to_translate, missing_count, stale_count


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
        f"{instructions}\n\n"
        "## Authoring Context\n"
        "Some user messages include a <context> block with English authoring "
        "context (speaker, addressee, intent, tone, setting, markup, or "
        "placeholder notes). Use it only to disambiguate the translation. "
        "Never translate, quote, or include the context block in your output."
    )


def build_user_prompt(
    text_id: str,
    source_text: str,
    target_language: str,
    context: ResolvedTextContext,
) -> str:
    """Build the user prompt for translating a single text.

    Args:
        text_id: The text identifier (for context/debugging).
        source_text: The English source text to translate.
        target_language: Target language name or code.
        context: Resolved class + per-file English authoring context.

    Returns:
        The user prompt string for the API call.
    """
    context_block: str = render_context_block(context)
    name_instruction: str = ""
    if text_id.startswith("names_"):
        line_count: int = len(
            [line for line in source_text.splitlines() if line.strip()]
        )
        name_instruction = (
            f"\nThis is a proper-name pool. Return exactly {line_count} lines, "
            "one localized full name per line, in the same order. Do not "
            "number the lines, add commentary, or use code fences.\n"
        )

    return (
        f"Translate the following English game text to {target_language}.\n"
        f"Text ID: {text_id}\n"
        f"{name_instruction}\n"
        f"{context_block}\n\n"
        f"Source text:\n{source_text}"
    )


def validate_name_pool(source_text: str, translated_text: str) -> str | None:
    """Validate a translated proper-name pool.

    Args:
        source_text: English source name pool.
        translated_text: Model output.

    Returns:
        None when valid, otherwise a human-readable validation error.
    """
    source_lines: list[str] = [
        line.strip() for line in source_text.splitlines() if line.strip()
    ]
    translated_lines: list[str] = [
        line.strip() for line in translated_text.splitlines() if line.strip()
    ]

    if len(translated_lines) != len(source_lines):
        return (
            f"expected {len(source_lines)} name lines, "
            f"got {len(translated_lines)}"
        )
    if any(line[:1].isdigit() for line in translated_lines):
        return "name lines must not be numbered"
    folded: list[str] = [line.casefold() for line in translated_lines]
    if len(set(folded)) != len(folded):
        return "translated names contain duplicates"
    return None


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
    require_meta: bool,
    report: TranslationReport,
) -> None:
    """Generate translations for a single target language.

    Loads source texts, resolves per-class/per-file context, diffs against
    existing translations, and calls the OpenRouter API for each missing or
    stale text.

    Args:
        config: API configuration.
        source_lang: Source language code (usually \"en\").
        target_lang: Target language code to generate for.
        text_ids_to_generate: If set, only generate these specific IDs.
            If None, generate all missing/stale IDs.
        force: If True, regenerate existing translations.
        dry_run: If True, only print what would be done, no API calls.
        verbose: If True, print detailed output.
        require_meta: If True, fail texts with no class/per-file context.
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

    class_meta_paths: dict[str, Path] = class_meta_files(source_dir)
    global_paths: tuple[Path, ...] = (CONTEXT_FILE, INSTRUCTIONS_FILE)
    ids_to_translate, missing_count, stale_count = get_ids_to_translate(
        source_texts, source_dir, target_dir, force, global_paths, class_meta_paths
    )

    if text_ids_to_generate is not None:
        requested: set[str] = set(text_ids_to_generate)
        ids_to_translate = [
            text_id for text_id in ids_to_translate if text_id in requested
        ]
        skipped_ids: list[str] = [
            text_id for text_id in text_ids_to_generate
            if text_id not in source_texts
        ]
        if skipped_ids:
            print(f"  Warning: Requested IDs not found in source: {skipped_ids}")

    existing_ids: set[str] = get_existing_ids(target_dir)
    missing_count = sum(
        1 for text_id in ids_to_translate if text_id not in existing_ids
    )
    stale_count = 0 if force else len(ids_to_translate) - missing_count

    if not ids_to_translate:
        if text_ids_to_generate is not None:
            print(
                f"  → Requested text(s) are already up to date in "
                f"{target_lang}/"
                f"{' (use --force to regenerate)' if not force else ''}."
            )
            report.skipped += len(text_ids_to_generate)
        else:
            print(
                f"  → No missing or stale translations. All {len(source_texts)} "
                f"texts are up to date in {target_lang}/"
                f"{' (use --force to regenerate)' if not force else ''}."
            )
            report.skipped += len(source_texts)
        return

    total: int = len(ids_to_translate)
    print(f"  → {total} text(s) to translate "
          f"({missing_count} missing, {stale_count} stale)")

    if dry_run:
        print(f"\n  Would translate ({target_lang}):")
        for text_id in ids_to_translate:
            source_text: str = source_texts[text_id]
            preview: str = source_text[:60].replace("\n", "\\n")
            context: ResolvedTextContext = resolve_text_context(
                source_dir, text_id, class_meta_paths
            )
            context_label: str = ", ".join(
                path.relative_to(TEXT_DIR).as_posix()
                for path in context.meta_paths
            ) or "no .meta"
            print(f"    - {text_id}: \"{preview}\" [context: {context_label}]")
        return

    system_context: str = load_context()
    instructions: str = load_translation_instructions()
    system_prompt: str = build_system_prompt(system_context, instructions)

    successful_count: int = 0
    failed_count: int = 0

    for index, text_id in enumerate(ids_to_translate, start=1):
        source_text: str = source_texts[text_id]
        context: ResolvedTextContext = resolve_text_context(
            source_dir, text_id, class_meta_paths
        )
        print(f"\n  [{index}/{total}] Translating '{text_id}'...", end=" ")

        if require_meta and not context.has_context:
            print("MISSING CONTEXT")
            report.errors.append(f"{target_lang}/{text_id}: missing .meta context")
            failed_count += 1
            continue
        if not context.has_context:
            print(f"\n    Warning: no class or per-file .meta context for '{text_id}'",
                  file=sys.stderr)

        user_prompt: str = build_user_prompt(
            text_id, source_text, language_name, context
        )

        translation: str | None = None
        validation_error: str | None = None
        max_attempts: int = (
            NAME_VALIDATION_ATTEMPTS if text_id.startswith("names_") else 1
        )
        for _attempt in range(1, max_attempts + 1):
            translation = call_openrouter(
                config=config,
                system_prompt=system_prompt,
                user_prompt=user_prompt,
                verbose=verbose,
            )
            if translation is None:
                break
            validation_error = (
                validate_name_pool(source_text, translation)
                if text_id.startswith("names_") else None
            )
            if validation_error is None:
                break

        if translation is None:
            print("FAILED")
            report.errors.append(
                f"{target_lang}/{text_id}: API error after {MAX_RETRIES} retries"
            )
            failed_count += 1
            continue

        if validation_error is not None:
            print(f"INVALID ({validation_error})")
            report.errors.append(f"{target_lang}/{text_id}: {validation_error}")
            failed_count += 1
            continue

        if text_id.startswith("names_"):
            translation = "\n".join(
                line.strip() for line in translation.splitlines() if line.strip()
            )

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

    # Load and validate configuration. A dry run never calls the API, so it can
    # work without a key (useful for checking context/staleness in CI).
    try:
        config: TranslationConfig = load_config(config_path)
    except (FileNotFoundError, json.JSONDecodeError, ValueError) as config_error:
        if not args.dry_run:
            print(f"Configuration error: {config_error}", file=sys.stderr)
            return 1
        print(
            f"Dry-run: ignoring configuration error: {config_error}",
            file=sys.stderr,
        )
        config = TranslationConfig(
            openrouter_api_key="dry-run",
            model="dry-run",
            base_url="https://example.invalid",
        )

    if not config.openrouter_api_key and not args.dry_run:
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
            require_meta=args.require_meta,
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
