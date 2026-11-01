# Translation sidecar (Phase 1 baseline)

Chat translation uses a small internal HTTP service running **MADLAD-400-3B** through **CTranslate2** with **Lingua** language detection. It is CPU-only and bound to `127.0.0.1:8790` (or a private Compose network). It must never be exposed publicly.

Full setup and contract: `server/translation/README.md`.

## Runtime config

`game/config/translation.json`:

```json
{
  "sidecar_url": "http://127.0.0.1:8790",
  "enabled": true,
  "detection_confidence_threshold": 0.4,
  "request_timeout_ms": 2000
}
```

## Phase 1 behavior

- `sendChatMessage` inserts the message and responds; translation runs on a worker.
- `requestTranslations` returns cached translations immediately and enqueues missing ones.
- `pollChats` delivers new translations through the chat `change_id` cursor.
- A full/disabled sidecar degrades to the original text; it never fails the send.
- Web Push uses stored translations only.
- `messages.llm_translation_eligible` and `message_translations.tier` exist now but the LLM engine is inert. Monthly-pass premium translation is **Phase 2**.

## Setup

```bash
./server/translation/setup_sidecar.sh
./server/translation/run_sidecar.sh
```

Or with Docker Compose:

```bash
docker compose --profile translation up -d translation
```

The model is a multi-gigabyte mount at `server/translation/models/` and is gitignored.

## Test mode

`--passthrough` returns `<target>:<text>` and deterministic detection without the model. `tools/translation_smoke.sh` uses it. Never enable passthrough in production.

## Phase 2 seam

`chat::TranslationEngine` already separates detection/translation. Phase 2 adds an LLM engine with provider routing, quotas, and premium cache spill; the sidecar remains the baseline path.
