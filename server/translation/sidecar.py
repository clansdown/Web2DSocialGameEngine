#!/usr/bin/env python3
"""Internal translation sidecar for the Ravenest chat system.

Runs MADLAD-400-3B (Apache-2.0) through CTranslate2 (MIT) and detects the
source language with Lingua. This process is bound to localhost and must NEVER
be exposed through nginx — the game server is the only client.

Contract (see server/docs/chat.md):
    POST /detect    {"text": "..."}
        -> {"language": "fr", "confidence": 0.93}
    POST /translate {"items": [{"id": "m_1", "text": "...",
                                "source": "fr", "target": "en"}]}
        -> {"results": [{"id": "m_1", "translated_text": "...",
                         "detected_source_language": "fr", "confidence": 0.9}]}

Degradation: if CTranslate2 or the converted model directory is missing, the
process still starts and /detect still works (via Lingua or a heuristic), but
/translate returns HTTP 503 {"error": "model_unavailable"}. The game server
treats that as "no translation" and shows the original text.

Test mode: `--passthrough` returns a deterministic `<target>:<text>` translation
(and "en"/0.9 detection) so the game-side seam can be verified without the
multi-gigabyte model. Never run `--passthrough` in production.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

MODEL_DIR_DEFAULT = "models/madlad400-3b-ct2-int8"

_translator: Any = None
_tokenizer: Any = None
_detector: Any = None
_model_ready = False

_GREEK = re.compile(r"[\u0370-\u03ff\u1f00-\u1fff]")
_CYRILLIC = re.compile(r"[\u0400-\u04ff]")
_CJK = re.compile(r"[\u3040-\u30ff\u4e00-\u9fff]")
_ARABIC = re.compile(r"[\u0600-\u06ff]")
_HEBREW = re.compile(r"[\u0590-\u05ff]")


def heuristic_detect(text: str) -> tuple[str, float]:
    if _GREEK.search(text):
        return "el", 0.8
    if _CYRILLIC.search(text):
        return "ru", 0.75
    if _CJK.search(text):
        return "zh", 0.7
    if _ARABIC.search(text):
        return "ar", 0.75
    if _HEBREW.search(text):
        return "he", 0.75
    return "en", 0.5


def detect(text: str) -> tuple[str, float]:
    if _detector is not None:
        try:
            language, confidence = _detector.detect_language_of(text), None
            if language is not None:
                confidences = _detector.compute_language_confidence_values(text)
                confidence = float(confidences[0].value) if confidences else 0.0
                return str(language.iso_code_639_1.name).lower(), confidence
        except Exception:  # noqa: BLE001 - detection must never crash the request
            pass
    return heuristic_detect(text)


def load_model(model_dir: str) -> None:
    global _translator, _tokenizer, _model_ready
    try:
        import ctranslate2  # type: ignore
        import sentencepiece as spm  # type: ignore

        _translator = ctranslate2.Translator(model_dir, device="cpu")
        _tokenizer = spm.SentencePieceProcessor(model_file=f"{model_dir}/sentencepiece.model")
        _model_ready = True
        print(f"[sidecar] MADLAD model loaded from {model_dir}", flush=True)
    except Exception as exc:  # noqa: BLE001
        _model_ready = False
        print(f"[sidecar] model unavailable ({exc}); /translate returns 503", flush=True)


def load_detector() -> None:
    global _detector
    try:
        from lingua import LanguageDetectorBuilder  # type: ignore

        _detector = LanguageDetectorBuilder.from_all_languages().build()
        print("[sidecar] Lingua language detector loaded", flush=True)
    except Exception as exc:  # noqa: BLE001
        _detector = None
        print(f"[sidecar] Lingua unavailable ({exc}); using heuristic detection", flush=True)


def translate_one(text: str, source: str, target: str, passthrough: bool) -> dict[str, Any]:
    if passthrough:
        return {
            "translated_text": f"{target}:{text}",
            "detected_source_language": source or "en",
            "confidence": 0.9,
        }
    if not _model_ready:
        raise RuntimeError("model_unavailable")
    detected, confidence = detect(text) if not source else (source, 1.0)
    # MADLAD uses a target-language token prefix.
    tokens = _tokenizer.encode(f"<2{target}> {text}", out_type=str)
    result = _translator.translate_batch([tokens], beam_size=1)
    translated = _tokenizer.decode(result[0].hypotheses[0])
    return {
        "translated_text": translated,
        "detected_source_language": detected,
        "confidence": confidence,
    }


class Handler(BaseHTTPRequestHandler):
    passthrough = False

    def log_message(self, fmt: str, *args: Any) -> None:  # keep stderr quiet
        return

    def _send(self, code: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:  # noqa: N802 - stdlib naming
        length = int(self.headers.get("Content-Length", "0"))
        try:
            request = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self._send(400, {"error": "invalid_json"})
            return

        if self.path == "/detect":
            text = str(request.get("text", ""))
            if self.passthrough:
                self._send(200, {"language": "en", "confidence": 0.9})
                return
            language, confidence = detect(text)
            self._send(200, {"language": language, "confidence": confidence})
        elif self.path == "/translate":
            items = request.get("items", [])
            results = []
            for item in items:
                try:
                    out = translate_one(str(item.get("text", "")), str(item.get("source", "")),
                                        str(item.get("target", "en")), self.passthrough)
                except RuntimeError:
                    self._send(503, {"error": "model_unavailable"})
                    return
                results.append({
                    "id": item.get("id", ""),
                    "translated_text": out["translated_text"],
                    "detected_source_language": out["detected_source_language"],
                    "confidence": out["confidence"],
                })
            self._send(200, {"results": results})
        elif self.path == "/health":
            self._send(200, {"ready": _model_ready or self.passthrough,
                             "passthrough": self.passthrough})
        else:
            self._send(404, {"error": "not_found"})


def main() -> int:
    parser = argparse.ArgumentParser(description="Ravenest translation sidecar")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--model", default=MODEL_DIR_DEFAULT)
    parser.add_argument("--passthrough", action="store_true",
                        help="deterministic test mode; never use in production")
    args = parser.parse_args()

    Handler.passthrough = args.passthrough
    if not args.passthrough:
        load_detector()
        load_model(args.model)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"[sidecar] listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
