# Threading model

uWebSockets is single-threaded. Every uWS object (`HttpRequest`, `HttpResponse`, `WebSocket`, `App`) is valid only on the uWS loop under the documented lifetime rules.

## Invariants

1. Only the uWS loop touches uWS objects.
2. Workers receive owned copies (`std::string`, `json`, snapshots), never request views.
3. Worker completions return through `loop_dispatcher` (`server/runtime/LoopDispatcher.*`).
4. Blocking work (SQL, Argon2id, sidecar HTTP, push, file IO) never runs on the loop.
5. Queues are bounded; saturation returns `server_busy` (HTTP 503).
6. Shutdown drains producer pools before the loop gate and API pool are joined.

## Request path

```text
uWS loop
  onData: accumulate body only
    -> copy endpoint/headers/client/raw body
    -> http_pool.try_post(work)
         worker: parse JSON, authenticate, call handler, build ApiResponse
           -> loop_dispatcher.post(completion)
                loop: sendJsonResponse(res)
```

- `g_http_pool` is a bounded `thread_pool` (8 threads, 256 queue entries).
- `g_file_pool` is a separate bounded pool (4 threads, 128 entries) for `/images/*` and `/api/avatar/*` reads. Path validation and the MIME decision run there; the loop only writes the response.
- Body cap is 4 MiB; JSON parsing (including large bodies) happens on the worker.
- `uploadCharacterAvatar` parses/authenticates on the loop because it needs the uWS response handle for its own deferred upload completion.

## Chat translation

- `BaselineTranslationQueue` owns one long-lived worker (the DB adapter pins one connection per thread). `requestTranslations` and push dispatch enqueue cache misses; neither performs sidecar I/O inline.
- `PremiumTranslationQueue` is the Phase 2 seam and is not started in Phase 1.
- A queued job carries the message `version`; `store_translation` discards it if the message was edited or deleted in the meantime.

## Database access

`sqlite::database` (the adapter) owns one libpq connection per worker thread per logical database. A worker never shares a connection with another thread. `statement` objects are stack-scoped within one call.

## Game config

`GameConfigCache` stores immutable `config_state` snapshots. Reads go through a
`config_snapshot` (one consistent generation per request/operation) and
`config_handle` (a restricted one-file view). Reloads build a complete new state
and swap it atomically, so a reader never observes a partially replaced file and
old data stays alive until the last reader releases it.

Reload is triggered by **SIGHUP** (`ConfigReloadService`) or
`${SERVER_PATH}/tools/reload_deployment.sh` on a Docker host. The reload is
all-or-nothing: every changed config and both map caches are prepared first; a
read/parse/postprocess failure in any file keeps the entire previous
configuration active. `RAVENEST_CONFIG_HOT_RELOAD` no longer exists.

`TowerDefenseMapCache` and `CombatMapCache` track per-file mtimes, so an in-place
map edit is detected without a directory `touch`. Both participate in the same
prepare/commit reload. Reload callbacks reapply the Web Push VAPID keyring and
clear the `TextManager` cache.

A config-only deploy needs no image pull or container restart:

```bash
rsync -a --chmod=D755,F644 game/config/ user@server:/srv/ravenest/game/config/
ssh user@server /srv/ravenest/tools/reload_deployment.sh
```

## Shutdown order

1. Listener/combat sockets close; `App::run()` returns.
2. `ConfigReloadService` stops and joins its SIGHUP worker.
3. Chat translation queues stop and join.
4. `file_pool.shutdown()` drains/joins static/avatar reads.
5. `loop_dispatcher.shutdown()` closes the completion gate.
6. `http_pool.shutdown()` drains/joins API workers. Their completions are dropped.
7. Avatar and combat managers shut down.
8. The uWS `App` and loop are destroyed.

`combat_match_manager` keeps its own worker pool and posts through the same loop; it is joined before the App is destroyed.

## Adding work

- Pure CPU: `cpu` work belongs in a worker, never inline.
- DB: use the streaming adapter (thread-local connection) or `db::db_executor`.
- Network: sidecar, push, R2, verifier calls run on workers with deadlines.
- Completion: post to `loop_dispatcher`; never capture `res` in a worker that outlives the request without the abort flag.

## Tests

- `tools/agent_test_server.sh` drives request bounds.
- Shutdown/abort coverage is in `tools/combat_shutdown_test.sh` and should be extended with an HTTP abort test.
- Queue saturation should be exercised with a small test pool.
