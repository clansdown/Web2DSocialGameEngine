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

`GameConfigCache` is read-only by default. Hot reload is **off** unless `RAVENEST_CONFIG_HOT_RELOAD=1`; enabling it with multiple worker threads still permits a reader/writer race on cached JSON, so it is intended for single-threaded debugging only. All config access is serialized by `reload_mutex_` during lookup/insert. In Docker deployments, use `${SERVER_PATH}/tools/restart_deployment.sh` for config changes until the safe snapshot reload in `docs/TODO_config_reload.md` is implemented.

## Shutdown order

1. Listener/combat sockets close; `App::run()` returns.
2. Chat translation queues stop and join.
3. `file_pool.shutdown()` drains/joins static/avatar reads.
4. `loop_dispatcher.shutdown()` closes the completion gate.
5. `http_pool.shutdown()` drains/joins API workers. Their completions are dropped.
6. Avatar and combat managers shut down.
7. The uWS `App` and loop are destroyed.

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
