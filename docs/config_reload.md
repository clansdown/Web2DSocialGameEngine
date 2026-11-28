# Configuration reload

Status: implemented.

Production-safe config reload uses immutable snapshots and SIGHUP. The
implementation replaces the old `RAVENEST_CONFIG_HOT_RELOAD=1` in-place path
(removed) and the restart-only workflow.

## Design

- `config_ptr` = `std::shared_ptr<const nlohmann::json>`.
- `config_state` holds immutable per-file JSON, per-file mtimes, per-file
  generations, and derived building land/class/exclusive-group maps.
- `config_handle` is a restricted one-file view. It forwards read-only JSON
  accessors and refuses `*` on a temporary handle. Converting a handle to
  `nlohmann::json` produces a deep copy (never a reference into a superseded
  snapshot).
- `config_snapshot` is one consistent generation per request/operation. New code
  should take one snapshot and pass it by `const config_snapshot&`.
- `GameConfigCache` is lifecycle/reload only: `initialize`, `snapshot`,
  `prepareReload`, `commitReload`, `reloadConfigs`, `reloadChangedConfigs`,
  `isLoaded`, `addReloadCallback`.

## Reload flow

`ConfigReloadService` runs a 250 ms worker. SIGHUP sets a `sig_atomic_t`; the
worker prepares every cache before committing any:

1. `GameConfigCache::prepareReload(false, result)` builds a complete new
   `config_state` (changed files re-read, unchanged files shared).
2. `TowerDefenseMapCache::prepareReload(result)` and
   `CombatMapCache::prepareReload(result)` do the same for map JSONs.
3. If any prepare fails, nothing is committed; the previous state stays active.
4. Otherwise map caches commit, then the config cache commits, then reload
   callbacks run.

All files in a prepare are read/parsed/postprocessed before the commit. A
malformed file aborts the whole reload.

Reload callbacks run after the commit. The server currently reapplies the Web
Push VAPID keyring (`PushSender::configure`); `PushSender` is internally
synchronized so a reload cannot race an in-flight push.

## Dynamic configs

`collectConfigNames()` scans `tower_defense/spawn_schedules/*.json`,
`weeding/maps/*.json`, and `weeding/*.json` in addition to the tracked list.
The first access to an unknown dynamic config still lazy-loads through
`loadDynamicConfig` (bootstrap fallback).

## Map caches

`TowerDefenseMapCache` and `CombatMapCache` store immutable `*_map_state`
objects with per-file mtimes. In-place edits are detected (no directory `touch`
needed). Ordinary reads auto-rescan changed files; SIGHUP uses the
all-or-nothing prepare/commit path.

## Triggering a reload

For config changes, or edits/deletions to existing text, use the wrappers:

```bash
tools/push_config_files.sh   # game/config/ + reload
tools/push_text.sh           # game/text/ + reload (clears the text cache)
```

Both mirror with `--chmod=D755,F644` and invoke
`${SERVER_PATH}/tools/reload_deployment.sh` over SSH. The manual equivalent:

```bash
rsync -a --chmod=D755,F644 game/config/ user@server:/srv/ravenest/game/config/
ssh user@server /srv/ravenest/tools/reload_deployment.sh
```

The helper sends SIGHUP through compose (tini forwards it), waits for
`[GameConfigCache] Reload complete:`, and exits non-zero on
`[GameConfigCache] Reload aborted:` or timeout. It falls back to
`docker compose exec -T ravenest sh -c 'kill -HUP 1'`.

The reload callback reapplies the Web Push VAPID keyring and clears the
`TextManager` cache, so `tools/push_text.sh` applies without a restart.

## Text lookups

`TextManager` caches only non-empty results. A newly created text file is read
on the next lookup without SIGHUP or a restart. Existing text edits, deletions,
and a new translation for a language that previously fell back to English still
require SIGHUP (or `tools/push_text.sh`) because their non-empty cached entries
remain valid until the cache is cleared.

The client text cache likewise refuses to store a response when any requested
ID is empty, so closing and reopening an overlay can pick up newly available
text without a full page reload. Text that was already cached before an edit
still requires a page reload unless the server cache is cleared and the client
fetches again.

`tools/install.sh` mirrors content with `--chmod=D755,F644`, so a mode-600
source checkout can no longer make map/config JSONs unreadable to the container
user (`uid=10001`).

## Future work

- Migrate every remaining helper signature from `GameConfigCache&` to
  `const config_snapshot&`. The compatibility wrappers are thread-safe, but
  helpers that read several files should share one snapshot.
