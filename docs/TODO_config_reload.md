# TODO: configuration reload

Status: implemented. See `docs/config_reload.md` for the snapshot/SIGHUP design
and the `tools/reload_deployment.sh` workflow.

The old `RAVENEST_CONFIG_HOT_RELOAD=1` in-place reload path has been removed.

Remaining follow-up:

- Migrate every helper that still takes `GameConfigCache&` to
  `const config_snapshot&`, so multi-file helpers share one generation. The
  current compatibility wrappers are thread-safe (each returns an immutable
  handle or a deep copy) but take a fresh snapshot per call.
