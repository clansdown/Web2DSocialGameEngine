# TODO: safe configuration reload (Option B)

Status: deferred. The current restart helper is intentional. This file records
the larger design for reloading configuration without restarting the server.

## Problem

`game/config/*` is loaded by `GameConfigCache` at startup. The cache has an
opt-in mtime reload:

```text
RAVENEST_CONFIG_HOT_RELOAD=1
```

`GameConfigCache::getConfig()` returns `nlohmann::json&`. Readers hold that
reference after the cache mutex is released. `try_reload()` currently replaces
the referenced JSON in place, so a concurrent reader can observe freed or
partially replaced data. `docs/threading.md` correctly documents this as
single-threaded/debug-only.

A restart is therefore currently required for config changes in production.

## Goal

Reload `game/config/*` safely while HTTP workers, combat workers, chat workers,
and other consumers are reading configuration. A failed parse must keep the
previous configuration rather than taking the server down.

## Proposed design

Use immutable per-file snapshots and atomically swap the snapshot container.

```cpp
using config_ptr = std::shared_ptr<const nlohmann::json>;

struct config_state {
    std::unordered_map<std::string, config_ptr> files;
    std::unordered_map<std::string, std::time_t> mtimes;
};
```

`GameConfigCache` owns:

```cpp
std::mutex state_mutex_;
std::shared_ptr<const config_state> state_;
```

Accessors:

```cpp
config_ptr get_config(const std::string& name);
config_ptr get_fiefdom_building_types();
// ... one accessor per existing getter
```

Reload path:

```cpp
bool reload_configs(std::string& error);
bool reload_changed_configs(std::string& error);
```

`reload_configs()`:

1. Builds a new `config_state` outside the state mutex.
2. Reuses unchanged `config_ptr` values; parses changed files into new ones.
3. Validates and runs each file's postprocess step.
4. Rebuilds derived maps (arable/forest building maps) from the new state.
5. Swaps `state_` under `state_mutex_` in one short critical section.
6. Leaves the old state alive until its last reader releases it.

Readers take a `config_ptr` (or a state snapshot) and use that object for the
whole operation. No reference to a mutable cache entry escapes.

## Call-site migration

The bulk of the work is replacing `nlohmann::json&` access with `config_ptr`
handles. Existing patterns that need review:

- `auto cfg = cache.getX();` — usually fine, but becomes a `shared_ptr`.
- `const nlohmann::json& cfg = cache.getX();` — must become a handle or a
  dereference of a local handle.
- `nlohmann::json& cfg = cache.getX();` — same.
- `config_cache.getAllConfigs()` — return a copy from the current snapshot.
- Internal helpers such as `getBuildingClass()` and
  `getBuildingExclusiveGroup()`.
- Long-lived objects that store `GameConfigCache*` are fine; they keep the
  cache pointer and take a snapshot per operation.
- Combat should continue using its own per-match config snapshot; it must not
  start reading mutable global config mid-battle.

A temporary compatibility layer may be useful:

```cpp
class config_ref {
    config_ptr ptr_;
public:
    const nlohmann::json& operator*() const;
    const nlohmann::json* operator->() const;
};
```

but a `shared_ptr` is preferred if the call-site churn is acceptable.

## Reload trigger options

Pick one primary mechanism:

1. **SIGHUP plus script**
   - `reload_deployment.sh` sends `docker kill --signal=HUP ravenest`.
   - Signal handler sets an atomic flag; a worker/loop performs reload.
   - Simple; no credentials. Parse errors only reach logs.

2. **Staff-only HTTP endpoint**
   - `POST /api/reloadConfig` under a staff capability.
   - Returns parse result and which files changed.
   - Better feedback; requires auth and a capability decision.

3. **Safe mtime polling**
   - Keep `RAVENEST_CONFIG_HOT_RELOAD=1`, but now backed by snapshots.
   - Poll at most every N seconds, parse changed files off the request path.
   - No script or signal. Best convenience; needs a background reload worker to
     avoid request-thread parsing latency.

The likely end state is (1) or (2) for explicit deploys, with (3) optional for
development.

## Tests

- Concurrent readers plus repeated reload under TSan.
- Reload one file while another thread holds an old `config_ptr`; old snapshot
  remains valid and unchanged.
- Invalid JSON: reload fails, previous snapshot remains active.
- `registration.json` mode/global-code swap applies to subsequent requests.
- Derived maps (arable/forest) update with `fiefdom_building_types.json`.
- No server restart, no dropped WebSocket sessions, no DB disruption.

## Out of scope

- Restarting after image, env, or compose changes. Use
  `${SERVER_PATH}/tools/restart_deployment.sh`.
- Live reload of client assets; those are served from the content mount and
  read per request.
- Changing config semantics or validation rules.

## Related files

- `server/GameConfigCache.hpp`
- `server/GameConfigCache.cpp`
- `server/db/Schema.cpp` (pattern for connection/cleanup)
- `docs/threading.md`
- `server/registration/RegistrationSettings.cpp`
- `server/registration/InvitationHandlers.cpp`
- `server/main.cpp`
