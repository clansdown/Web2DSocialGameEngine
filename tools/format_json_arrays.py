#!/usr/bin/env python3
"""Collapse small numeric arrays in a JSON config file onto a single line.

Game configs like `fiefdom_building_types.json` were formatted with every
array element on its own line, which makes small 5-element arrays (costs,
amounts, construction times) span several screens. This tool re-emits the file
with the same 4-space / `": "` style, but writes any *numeric* array of length
<= 5 on one line:

    "gold_cost": [0.5, 1, 1.5, 2, 3],

Arrays of objects (`outputs`, `prerequisites`), longer strings
(`descriptions`), and larger numeric arrays (e.g. the manor house's
10-element ledger arrays) stay expanded so long prose stays readable.

Only numeric arrays are collapsed; booleans and strings are excluded so a
sentence array is never squashed into a wall of prose. Key order and number
literals are preserved (Python's json round-trips both).

If the input is not valid JSON, a tolerant fallback reparses it by inverting
the historical "comma on the opening line" bug (an earlier version of this
tool attached separator commas to `{`/`[` openers instead of `}`/`]` closers,
which is the only JSON construct this tool ever emitted incorrectly), so it
can repair files mangled by that version in one pass.

Usage:
    python3 tools/format_json_arrays.py game/config/fiefdom_building_types.json
"""

import argparse
import json
from typing import Any


def _is_small_numeric_array(value: Any) -> bool:
    """True when value is a non-empty list of <=5 JSON numbers."""
    if not isinstance(value, list) or not value or len(value) > 5:
        return False
    return all(isinstance(e, (int, float)) and not isinstance(e, bool) for e in value)


def _emit(value: Any, level: int) -> list[str]:
    """Render value as a list of lines. Every line carries its own leading
    indentation (nested multi-line blocks included)."""
    pad = "    " * level
    if _is_small_numeric_array(value):
        return [pad + json.dumps(value, ensure_ascii=False)]
    if isinstance(value, dict):
        if not value:
            return [pad + "{}"]
        lines = [pad + "{"]
        items = list(value.items())
        for i, (key, val) in enumerate(items):
            comma = "," if i < len(items) - 1 else ""
            sub = _emit(val, level + 1)
            if len(sub) == 1:
                lines.append(pad + "    " + '"' + key + '": ' + sub[0].lstrip() + comma)
            else:
                lines.append(pad + "    " + '"' + key + '": ' + sub[0].lstrip())
                lines.extend(sub[1:-1])
                lines.append(sub[-1] + comma)
        lines.append(pad + "}")
        return lines
    if isinstance(value, list):
        if not value:
            return [pad + "[]"]
        lines = [pad + "["]
        for i, elem in enumerate(value):
            comma = "," if i < len(value) - 1 else ""
            sub = _emit(elem, level + 1)
            if len(sub) == 1:
                lines.append(sub[0] + comma)
            else:
                lines.extend(sub[:-1])
                lines.append(sub[-1] + comma)
        lines.append(pad + "]")
        return lines
    # Scalar.
    return [pad + json.dumps(value, ensure_ascii=False)]


# ---------------------------------------------------------------------------
# Tolerant fallback parser (repairs the historical comma-on-opener bug) ----

def _tokenize(text: str) -> list[str]:
    """Split JSON text into tokens: braces, brackets, colons, commas, and
    scalar tokens (quoted strings and raw number/literal words)."""
    tokens: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "{}[]:,":
            tokens.append(c)
            i += 1
        elif c == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2  # skip the escaped character
                    continue
                if text[j] == '"':
                    break
                j += 1
            if j >= n:
                raise ValueError("unterminated string literal in input")
            tokens.append(text[i : j + 1])
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "{}[]:,":
                j += 1
            tokens.append(text[i:j])
            i = j
    return tokens


def _parse_scalar(tok: str) -> Any:
    if tok == "true":
        return True
    if tok == "false":
        return False
    if tok == "null":
        return None
    return json.loads(tok)


def _parse_object(tokens: list[str], i: int) -> tuple[dict[str, Any], int]:
    """Parse an object starting at tokens[i] == '{'. Returns (obj, next_index).
    Separator commas are optional/ignored (repair mode)."""
    assert tokens[i] == "{"
    i += 1
    obj: dict[str, Any] = {}
    while True:
        while i < len(tokens) and tokens[i] == ",":
            i += 1
        if i >= len(tokens) or tokens[i] == "}":
            return obj, i + 1
        key = _parse_scalar(tokens[i])
        i += 1
        if i < len(tokens) and tokens[i] == ":":
            i += 1
        value, i = _parse_value(tokens, i)
        obj[key] = value


def _parse_array(tokens: list[str], i: int) -> tuple[list[Any], int]:
    """Parse an array starting at tokens[i] == '['. Returns (arr, next_index).
    Separator commas are optional/ignored (repair mode)."""
    assert tokens[i] == "["
    i += 1
    arr: list[Any] = []
    while True:
        while i < len(tokens) and tokens[i] == ",":
            i += 1
        if i >= len(tokens) or tokens[i] == "]":
            return arr, i + 1
        value, i = _parse_value(tokens, i)
        arr.append(value)


def _parse_value(tokens: list[str], i: int) -> tuple[Any, int]:
    tok = tokens[i]
    if tok == "{":
        return _parse_object(tokens, i)
    if tok == "[":
        return _parse_array(tokens, i)
    return _parse_scalar(tok), i + 1


def _load_tolerant(text: str) -> Any:
    """Parse JSON text while ignoring separator commas entirely. The historical
    comma-on-opener bug moved every separator onto `{`/`[` opener lines (and
    left closers comma-less); commas are *only* separators in JSON, so ignoring
    them in this pass reconstructs the exact original structure."""
    tokens = _tokenize(text)
    value, end = _parse_value(tokens, 0)
    if end != len(tokens):
        raise ValueError(f"unexpected trailing tokens after position {end}")
    return value


# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", help="JSON file to reformat in place")
    args = parser.parse_args()

    with open(args.path, "r", encoding="utf-8") as f:
        text = f.read()

    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        data = _load_tolerant(text)

    rendered = "\n".join(_emit(data, 0)) + "\n"

    with open(args.path, "w", encoding="utf-8") as f:
        f.write(rendered)


if __name__ == "__main__":
    main()