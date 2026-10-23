# Translation Sidecar

The chat system translates messages through this small internal HTTP service.
It is **not** part of the game server and must **never** be exposed publicly
(bind it to `127.0.0.1`; the game server is its only client).

## Why this design

- **MADLAD-400-3B** (Apache-2.0) via **CTranslate2** (MIT) is the free baseline
  engine. Both licenses permit commercial use, and MADLAD covers 400+ languages
  including Greek. (NLLB-200 and Tower are CC-BY-NC and were rejected.)
- **Lingua** performs source-language detection.
- The engine is pluggable on the game side (`chat::TranslationEngine`); the
  premium LLM path is a stubbed second engine (`chat::LlmTranslationEngine`).

## Contract

| Endpoint | Request | Response |
|---|---|---|
| `POST /detect` | `{"text": "..."}` | `{"language": "fr", "confidence": 0.93}` |
| `POST /translate` | `{"items":[{"id","text","source","target"}]}` | `{"results":[{"id","translated_text","detected_source_language","confidence"}]}` |
| `GET /health` | — | `{"ready": true, "passthrough": false}` |

If the model is not loaded, `/translate` returns HTTP `503
{"error":"model_unavailable"}`. The game server degrades gracefully: it stores
no translation and shows the original text.

## Running

```bash
python3 server/translation/sidecar.py --port 8790
```

The game reads `game/config/translation.json`:

```json
{ "sidecar_url": "http://127.0.0.1:8790", "enabled": true,
  "detection_confidence_threshold": 0.4, "request_timeout_ms": 2000 }
```

## Installing the model (deferred ops step)

1. `pip install ctranslate2 sentencepiece lingua-language-detector`
2. Download `google/madlad400-3b-mt` and convert to CTranslate2 int8:

   ```bash
   ct2-transformers-converter --model google/madlad400-3b-mt \
       --output_dir models/madlad400-3b-ct2-int8 --quantization int8
   ```

   (The SentencePiece model is copied alongside as `sentencepiece.model`.)
3. Start the sidecar (it loads `models/madlad400-3b-ct2-int8` by default).

## Test mode

`--passthrough` returns a deterministic `<target>:<text>` translation and
`"en"` detection, so the game-side translation seam can be exercised without
the multi-gigabyte model. **Never run `--passthrough` in production.** The
translation smoke test (`tools/translation_smoke.sh`) uses it.
