# Publishing

This document describes how the game is published:

- the Svelte client is built into `client/dist` and uploaded to **Cloudflare Pages**;
- public art (and later audio) is uploaded to a private **R2** bucket and served
  through an R2 custom domain;
- `/api/*` and `/ws/*` are routed to the C++ API server through a **Cloudflare
  Tunnel**;
- `game/` is mirrored to the API server for `--content` (config, text, avatar
  pools, and the server's `/images` fallback).

One command, `./install.sh`, performs the build and all configured publishing
stages. The engine stays generic: deployment values live in a gitignored
`.deploy` file. The site repo, ops overlay, and `.deploy` are **private** and
are not part of this repository.

> The hostnames below (`app.example.com`, `api.app.example.com`,
> `assets.app.example.com`) are placeholders. Replace `example.com` with your
> Cloudflare zone.

> **Quick start**
>
> ```bash
> cp .deploy.example .deploy
> $EDITOR .deploy          # fill in the destinations you use
> chmod 600 .deploy
> chmod +x install.sh      # once, if your checkout lacks the executable bit
> DRY_RUN=1 ./install.sh   # prints the publish plan, writes nothing remote
> ./install.sh             # build -> R2 -> Pages -> API server
> ```
>
> `bash install.sh` works too if the executable bit is not set.

## 1. Topology

```
Browser → https://app.example.com
  /                 → Cloudflare Pages (site repo root)
  /assets/*         → Cloudflare Pages (hashed Vite bundle)
  /api/*            → Origin Rule → api.app.example.com (tunnel) → 127.0.0.1:2290
  /ws/*             → Origin Rule → api.app.example.com (tunnel) → 127.0.0.1:2290
  /images/*         → Origin Rule → assets.app.example.com (R2 bucket)
  /sounds/*         → same, when audio exists
  /music/*          → same, when audio exists
```

The browser only ever talks to `app.example.com`. The Origin Rule changes the
origin (Host header and DNS record) for matching paths; it does **not** rewrite
the URL. That keeps `/api/...` and `/images/...` unchanged for the client and
server, and keeps every request same-origin, so no CORS configuration is needed.

`/images/*` is entirely R2: every image path under `/images/...` maps to an R2
key `images/...`. The client's `public/images` directory is empty by design;
game UI images live in `game/images/ui/` and are uploaded to R2.

`cloudflared` runs **on the API server** and dials out; no inbound firewall
ports are opened. The build host still reaches the API server over SSH/LAN for
the `game/` mirror. The tunnel is outbound only and does not provide an inbound
rsync path.

## 2. Prerequisites

On the build host:

- Node.js and npm (the version expected by `client/package.json`);
- `rsync`;
- Git;
- AWS CLI v2 (for the R2 upload);
- a complete working tree including the gitignored `client/SimpleGame` (the
  `file:./SimpleGame/ui` dependency) and the `game/` content tree (configs,
  text, images; only a few placeholders are tracked in the public engine);
- SSH access to the API server, if the server mirror is enabled.

Cloudflare:

- a zone (for example `example.com`);
- a Pages project;
- a private R2 bucket;
- a tunnel (`cloudflared`) on the API server.

> The `client/SimpleGame` checkout is required for the build. It is not tracked
> in this repository. Follow `README.md` to clone it into `client/SimpleGame`.

> **Certificate note:** Universal SSL covers `example.com` and
> `*.example.com`, but **not** two-level names such as
> `api.app.example.com` or `assets.app.example.com`. Enable Total TLS or
> Advanced Certificate Manager for those names, or use one-level hostnames
> (`api.example.com`, `assets.example.com`) and update the Origin Rules and the
> R2 custom domain accordingly.

## 3. Repository roles

| Role | Visibility | Contents |
|---|---|---|
| Engine repo (this repository) | public | source, configs, `install.sh`, `docs/publishing.md`, `.deploy.example` |
| Site repo | **private** | generated Pages site only: `index.html`, `assets/`, `_headers`, `robots.txt`, favicon |
| Ops overlay | **private** | files copied into the site repo after the build sync (`_headers`, `robots.txt`, favicon, optional `_redirects`) |
| `.deploy` | **private** | real site repo path, R2 credentials, API server host/path |
| `push_to_production.sh` / `backup.sh` | local only | legacy manual server rsync fallback |

Do not commit the site repo, the overlay, `.deploy`, or real deployment values
to the engine. `install.sh` is intentionally generic.

## 4. `.deploy` reference

`.deploy` lives at the engine root and is sourced by `install.sh` as a shell
fragment. Use simple assignments only. Keep it out of version control:

```bash
cp .deploy.example .deploy
chmod 600 .deploy
```

`.deploy.example` is the tracked template. `.deploy` itself is gitignored by
the engine's `.gitignore` and must never be committed. Back it up separately;
it is not in the repository.

Because `.deploy` is sourced by bash, `$HOME` and other shell variables expand,
and paths containing spaces must be quoted. Use `$HOME`, not `~`, because the
values are later used as literal paths.

| Key | Stage | Required when | Secret | Example / notes |
|---|---|---|---|---|
| `SITE_REPO` | Pages | Pages enabled | no | `$HOME/src/example-site` — private repo whose root is the generated site |
| `SITE_OVERLAY` | Pages | optional | no | `$HOME/src/example-ops/pages-overlay` — copied in after the `dist` sync |
| `R2_BUCKET` | R2 | R2 enabled | no | `example-assets` |
| `R2_ENDPOINT` | R2 | R2 enabled | no | `https://<account-id>.r2.cloudflarestorage.com` |
| `R2_ACCESS_KEY_ID` | R2 | R2 enabled | **yes** | R2 API token access key |
| `R2_SECRET_ACCESS_KEY` | R2 | R2 enabled | **yes** | R2 API token secret |
| `SERVER_HOST` | server | server enabled | no | `api-host.example.net` or an SSH config alias |
| `SERVER_PATH` | server | server enabled | no | parent of the server's `--content` dir; for `run_server.sh`, the directory containing `game/` |
| `SERVER_SSH_KEY` | server | optional | no | private key path; omit to use the SSH agent/config |
| `CF_API_TOKEN` | deferred | optional | **yes** | reserved for future purge-on-deploy |
| `CF_ZONE_ID` | deferred | optional | no | reserved for future purge-on-deploy |

A stage is enabled by setting its primary key:

- Pages: `SITE_REPO`;
- R2: `R2_BUCKET` (plus endpoint and credentials);
- server: `SERVER_HOST` (plus `SERVER_PATH`).

If none of the three primary keys is set, `install.sh` fails before publishing.
`--skip-*` flags can disable an otherwise-configured stage.

Security notes:

- `chmod 600 .deploy`; `install.sh` warns if the mode is looser.
- `.deploy` is sourced, so it must contain assignments, not arbitrary commands.
- `install.sh` never uses `set -x` and never prints credential values.
- Do not put `.deploy` in the site repo or the ops overlay.

## 5. First-time setup

Checklist:

- [ ] private site repo + private overlay (`_headers`, `robots.txt`, favicon);
- [ ] Pages project connected to the site repo + `app.example.com`;
- [ ] R2 bucket + `assets.app.example.com` custom domain + API token;
- [ ] `cloudflared` tunnel + `api.app.example.com` public hostname;
- [ ] Origin Rules for `/api/*`, `/ws/*`, `/images/*`;
- [ ] Cache Rules (bypass API/WS, cache assets at 1h);
- [ ] `.deploy` filled in and `chmod 600`;
- [ ] `DRY_RUN=1 ./install.sh`;
- [ ] staging publish + verification checklist (§9);
- [ ] production publish.

### 5.1 Site repo and Pages

Create a **private** Git repository whose root will be the published site. The
repository is generated content: `index.html`, `assets/`, `_headers`, and a few
static files. Do not hand-edit it; `install.sh` replaces the generated tree with
`rsync --delete` and then copies the overlay. Start empty (remove any
host-generated `README.md` or `.gitignore`): a non-empty site repo without
`index.html` is rejected to protect against pointing `--delete` at the wrong
directory.

The clone only needs a Git remote. The first publish creates the first commit
and sets the upstream automatically (`git push -u`), so no manual initial commit
or push is required.

In the Cloudflare dashboard:

1. Create a Pages project and connect it to the site repo (grant the Cloudflare
   GitHub/GitLab app access to the private repository).
2. Set the build command to `exit 0` (the site is already built and committed).
3. Set the build output directory to the repository root (`.`, or `/` in the
   dashboard).
4. Set the production branch to the branch `install.sh` pushes.
5. Add the custom domain `app.example.com`.

The overlay must contain `_headers` with at least:

```
/
  Cache-Control: no-cache

/index.html
  Cache-Control: no-cache

/sw.js
  Cache-Control: no-cache

/assets/*
  Cache-Control: public, max-age=31536000, immutable
```

Vite hashes files under `assets/`, so the immutable rule is safe. `index.html`
must not be cached, otherwise a deploy can leave clients on an old shell that
references removed bundles. `sw.js` must also be revalidated so the service
worker can update.

Optional overlay files:

- `robots.txt`;
- favicon and other root static files;
- `_redirects` for **real redirects only**. It cannot proxy `/api/*` or
  `/images/*`; that is the Origin Rule's job.

The overlay is copied **after** the `client/dist` sync, so `rsync --delete`
cannot remove it before it is re-added. The overlay copy is additive: removing
a file from the overlay does not remove it from the site repo. Delete it from
the site repo (or reset the repo) if you need to retire an overlay file.

### 5.2 R2 bucket and custom domain

1. Create a private R2 bucket (for example `example-assets`).
2. Bind the custom domain `assets.app.example.com` to the bucket.
3. Create an R2 API token with Object Read & Write on the bucket.
4. Put the endpoint and token in `.deploy`.

Object keys keep the directory prefix:

```
images/...
sounds/...
music/...
```

That matches the browser path after the Origin Rule, so no URL rewrite is
needed. `install.sh` uploads `game/images`, `game/sounds`, and `game/music`
when those directories exist.

Do **not** upload `game/config` or `game/text` to R2. Those are server content
and are mirrored to the API server instead.

No bucket CORS configuration is needed while the browser stays on
`app.example.com`: the Origin Rule makes the request same-origin from the
browser's point of view.

Do not set `Cache-Control` on the objects. The Cloudflare Cache Rule owns the
TTL, so it can be raised later without re-uploading. See §7.

### 5.3 Cloudflare Tunnel

Install `cloudflared` on the API server and create a tunnel:

```bash
cloudflared tunnel create example-api
cloudflared tunnel route dns example-api api.app.example.com
```

Use a config such as `/etc/cloudflared/config.yml`:

```yaml
tunnel: <tunnel-id>
credentials-file: /etc/cloudflared/<tunnel-id>.json

ingress:
  - hostname: api.app.example.com
    path: ^/api/.*
    service: http://127.0.0.1:2290
  - hostname: api.app.example.com
    path: ^/ws/.*
    service: http://127.0.0.1:2290
  - service: http_status:404
```

Run it as a service (`cloudflared service install`, or a systemd unit). The
C++ server still listens on plain HTTP on `127.0.0.1:2290`; TLS terminates at
Cloudflare.

WebSockets work through the tunnel without extra options. Cloudflare closes an
idle WebSocket after roughly 100 seconds; in-match 10 Hz ticks keep it alive,
and lobby sockets rely on the client's reconnect logic.

`api.app.example.com` is itself a public hostname. If a direct API entry point
is not desired, protect it with Cloudflare Access or a service token and have
the Origin Rule carry the token. Otherwise, accept it as an alternate API
entry point.

### 5.4 Origin Rules (the path router)

On the `example.com` zone, create Origin Rules scoped to the Pages hostname.
Each rule sets **Host header** and **DNS record** to the target, with no URL
rewrite:

| Rule | Match | Origin parameters |
|---|---|---|
| API | `http.host eq "app.example.com"` and `starts_with(http.request.uri.path, "/api/")` | Host header → `api.app.example.com`; DNS record → `api.app.example.com` |
| WebSocket | same host and `starts_with(http.request.uri.path, "/ws/")` | same |
| Images | same host and `starts_with(http.request.uri.path, "/images/")` | Host header → `assets.app.example.com`; DNS record → `assets.app.example.com` |
| Sounds (later) | same host and `starts_with(http.request.uri.path, "/sounds/")` | same as images |
| Music (later) | same host and `starts_with(http.request.uri.path, "/music/")` | same as images |

Notes:

- Do not rewrite the path. The C++ server expects `/api/...` and `/ws/...`, and
  the R2 key prefix is `images/...`.
- Both targets are in the same zone, so the DNS record override is allowed
  without an Enterprise plan. SNI override is Enterprise and is not needed.
- Verify that these rules take precedence over the Pages origin for matching
  paths. If a matching path still returns a Pages 404 or a 522, use a Worker
  route for that path as a fallback.
- Test `/ws/combat` end to end after the rule is in place; the rule must
  preserve the `Upgrade` header.

### 5.5 Cache Rules

Create two Cache Rules on the zone:

**Bypass API and WebSocket**

- Match: `http.host eq "app.example.com"` and (`starts_with(http.request.uri.path, "/api/")` or `starts_with(http.request.uri.path, "/ws/")`).
- Action: **Bypass cache**.

This is required for `GET /api/avatar/<character_id>`, which is age-gated and
must never be cached by Cloudflare.

**Cache public assets**

- Match: `http.host eq "app.example.com"` and (`starts_with(http.request.uri.path, "/images/")`, or `/sounds/`, `/music/` when present).
- Action: **Cache eligible**.
- Edge TTL: **1 hour** (override origin).
- Browser TTL: **1 hour** (override origin).

Start at one hour. Once the art is stable, raise both values in the dashboard;
no re-upload is needed because the TTL is not stored on the R2 objects.

Pages caching is controlled by the site's `_headers`, not by these rules:
`index.html` is `no-cache`, `assets/*` is immutable.

### 5.6 Staging

Before the production cutover, publish to a staging hostname (for example
`staging.app.example.com`) with its own Pages project or preview deployment.
Run the full verification checklist in §9 against staging. The same R2 bucket
can be used; only the hostname and Pages project differ.

`install.sh` publishes to the destination named in `.deploy`. To stage, use a
second working copy with a staging `.deploy` (staging site repo + staging Pages
project), or temporarily switch `SITE_REPO`/`SERVER_HOST` to staging values.
Do not point a staging run at the production site repo.

## 6. Publishing

```bash
# See exactly what would happen; no remote writes.
DRY_RUN=1 ./install.sh

# Real publish.
./install.sh
```

The order is fixed:

1. **build** — `npm ci` and `npm run build` write `client/dist`.
2. **R2** — public art is uploaded before the shell that references it.
3. **Pages** — `client/dist` is synced to the site repo, the overlay is copied,
   then the site repo is committed and pushed.
4. **server** — `game/` is mirrored to the API server.

### Options

| Option | Effect |
|---|---|
| `--dry-run` / `DRY_RUN=1` | No writes to R2, the site repo, or the API server. The local build still runs unless `--skip-build` is also given. |
| `--skip-build` | Reuse `client/dist`; it must contain `index.html`. |
| `--skip-r2` | Skip the R2 stage even if `R2_BUCKET` is set. |
| `--skip-pages` | Skip the Pages stage even if `SITE_REPO` is set. |
| `--skip-server` | Skip the server mirror even if `SERVER_HOST` is set. |
| `--help` | Show usage. |

### Build stage

```bash
npm --prefix client ci --no-audit --no-fund
npm --prefix client run build
```

`npm ci` requires `client/package-lock.json` and a complete `client/SimpleGame`
checkout. Vite writes to `client/dist`; the engine never points Vite's `outDir`
at the site repo, because Vite empties its output directory.

### R2 stage

For each of `game/images`, `game/sounds`, and `game/music` that exists:

```bash
aws s3 sync "game/$dir" "s3://$R2_BUCKET/$dir" \
  --delete --endpoint-url "$R2_ENDPOINT" \
  --exclude '*.xcf' --exclude '*.md' --exclude '.gitkeep'
```

`--delete` removes stale remote objects. Excluded source files are not
uploaded, and `--exclude` also protects matching remote files from deletion. A
source directory with no publishable files is skipped rather than deleting the
whole remote prefix (usually a missing content checkout).

A separate pass fixes the content type for AVIF files:

```bash
aws s3 cp "game/$dir" "s3://$R2_BUCKET/$dir" \
  --recursive --exclude '*' --include '*.avif' \
  --content-type image/avif --endpoint-url "$R2_ENDPOINT"
```

`cp` is deliberate: `sync` would skip unchanged AVIF objects because size and
mtime already match, so the metadata would never be corrected. The AVIF pass
re-uploads those objects on each run. WebP normally gets the right type from the
AWS CLI; if `curl -I` shows otherwise, add a similar
`--include '*.webp' --content-type image/webp` pass.

The uploader does not set `Cache-Control`; the Cache Rule owns the TTL. It also
sets the AWS CLI checksum calculation/validation to `when_required`, because
recent CLI v2 releases otherwise add checksums that R2 does not accept.

### Pages stage

```bash
rsync -a --delete --exclude=.git client/dist/ "$SITE_REPO/"
[ -n "$SITE_OVERLAY" ] && rsync -a --exclude=.git "$SITE_OVERLAY/" "$SITE_REPO/"
git -C "$SITE_REPO" add -A
git -C "$SITE_REPO" commit -m "deploy <timestamp> engine@<sha>"
git -C "$SITE_REPO" push
```

The site repo only needs a Git remote; `install.sh` does not create the
repository or its remote. If the current branch has a resolvable upstream, it
runs `git push`. If it does not — a fresh clone of an empty repository shows
`origin/master [gone]` and has no `@{u}` yet — it pushes the current branch with
`git push -u <remote> <branch>` and sets the upstream. A push failure is fatal,
but it happens **after** the copy and commit, so the site repo still contains
the new build. The commit message includes the engine SHA (with a `-dirty` suffix
when the working tree has uncommitted changes), so every published site is
traceable to the source revision. If the sync produces no changes, no new commit
is created, but the push still runs so a commit left behind by an earlier failed
push can complete.

### Server stage

```bash
SERVER_PATH="${SERVER_PATH%/}"
rsync -a --delete \
  --exclude='*.db' --exclude='*.xcf' --exclude='.git/' \
  --exclude='uploads/' --exclude='*.md' --exclude='.gitkeep' \
  game/ "$SERVER_HOST:${SERVER_PATH}/game/"
```

`SERVER_PATH` is the parent of the server's `--content` directory. For
`run_server.sh`, that is the directory containing `game/`. Trailing slashes
matter: the command copies the **contents** of local `game/` into remote
`game/`.

`--delete` makes the remote tree an exact mirror. This is safe because the
server only reads its content directory; databases go to `--db-dir` and uploads
go to `uploads/avatars/` in the server's working directory. `install.sh` refuses
to mirror an empty `game/` tree, which would otherwise wipe the remote content.

Run `DRY_RUN=1 ./install.sh` before the first real mirror.

### Preflight checks

Before building, `install.sh`:

- requires `client/SimpleGame/ui`;
- refuses to run if `.deploy`, `backup.sh`, or `push_to_production.sh` is tracked
  by git;
- refuses to run if `server/build*` or `game_balance_analyzer/build*` is tracked;
- refuses to publish into `/`, your home directory, a directory that looks like
  a source repo, a non-empty site repo without `index.html`, or a site repo
  that is, contains, or lives inside the engine checkout; refuses an overlay
  inside the site repo (the Pages sync uses `--delete`);
- warns about tracked files larger than 50 MiB and refuses to run if any tracked
  file exceeds GitHub's 100 MiB limit.

These guard the repository and the local sync, not the publish destinations.

## 7. Cache policy

| Asset | Cache |
|---|---|
| Pages `/` and `/index.html` | `no-cache` via `_headers` |
| Pages `/sw.js` | `no-cache` via `_headers` (service worker updates) |
| Pages `/assets/*` | `public, max-age=31536000, immutable` via `_headers` (hashed by Vite) |
| R2 `/images/*`, `/sounds/*`, `/music/*` | Cloudflare Cache Rule: Edge 1h, Browser 1h |
| `/api/*` and `/ws/*` | Cloudflare Cache Rule: bypass |

Raising the R2 TTL later is a Cache Rule edit, not a re-upload. Browsers that
already cached an object keep it until their own copy expires; use **Purge
Cache** on the affected URL or prefix if an asset must change immediately.

## 8. Server mirror details

- **Source:** `game/` — `config/`, `text/`, `images/`, and future audio.
- **Target:** `${SERVER_PATH}/game/`, the server's `--content` directory.
- **Excludes:** `*.db`, `*.xcf`, `.git/`, `uploads/`, `*.md`, `.gitkeep`.

Why the server still needs a full `game/` copy:

- `GameConfigCache` reads `config/`;
- `TextManager` reads `text/`;
- `AvatarPool` scans `images/avatars/safe/` and `images/retinue/headshots/`;
- the server's `/images` routes remain available as a fallback (for example, if
  the Origin Rule is removed or when using the API host directly).

`game/config` and `game/text` are never published to R2 or Pages.

## 9. Verification

Run these after a staging or production publish.

### Pages shell

```bash
curl -sI https://app.example.com/ | grep -i cache-control
# cache-control: no-cache

curl -sI https://app.example.com/index.html | grep -i cache-control
# cache-control: no-cache

curl -sI https://app.example.com/sw.js | grep -i cache-control
# cache-control: no-cache

# Pick a real hashed bundle from the built index.html.
curl -sI https://app.example.com/assets/<hashed-file>.js | grep -i cache-control
# cache-control: public, max-age=31536000, immutable
```

### R2 images

```bash
curl -sI https://app.example.com/images/manor/grass.png
# HTTP/2 200, content-type: image/png, cf-cache-status present

curl -sI https://app.example.com/images/<file>.avif
# content-type: image/avif
```

If `content-type` is `application/octet-stream`, the AVIF pass did not run or
the file has a different extension.

### API

```bash
curl -s -o /dev/null -w '%{http_code}\n' \
  -X POST https://app.example.com/api/getTexts \
  -H 'content-type: application/json' \
  -d '{"language":"en","text_ids":["ui_login_title"]}'
# 200
```

`/api/*` must return `cf-cache-status: BYPASS` or `DYNAMIC`, not `HIT`.
`GET /api/avatar/<character_id>` must remain uncached and age-gated.

### WebSocket

Open the game in a browser and start combat, or use a WebSocket client against
`wss://app.example.com/ws/combat`. The handshake must return `101 Switching
Protocols`; the first client message is the auth envelope. The local
`tools/combat_smoke.sh` verifies the protocol against a local server, not the
remote host.

### Server content

```bash
ssh "$SERVER_HOST" ls -d \
  "$SERVER_PATH/game/config" \
  "$SERVER_PATH/game/text" \
  "$SERVER_PATH/game/images/avatars/safe" \
  "$SERVER_PATH/game/images/retinue/headshots"
```

Removed art must be gone from the remote tree (the mirror uses `--delete`).

### Origin Rule behavior

- `/api/...` reaches the API server, not a Pages 404;
- `/images/...` reaches R2, not Pages;
- `/ws/...` upgrades, not a Pages response.

## 10. Rollback

**Pages shell**

- Use the Pages project's deployment history to roll back to the previous
  deployment; or
- revert the deploy commit in the site repo and push:

  ```bash
  git -C "$SITE_REPO" log --oneline
  git -C "$SITE_REPO" revert <sha>
  git -C "$SITE_REPO" push
  ```

**R2 assets**

The local `game/images` tree is the source of truth. Restore the last good
asset tree from your content backup (`game/` is not fully tracked in the public
engine), then re-run the R2 stage:

```bash
DRY_RUN=1 ./install.sh --skip-build --skip-pages --skip-server
./install.sh --skip-build --skip-pages --skip-server
```

If the Cache Rule TTL is long, purge the affected URLs or prefix after the
re-upload.

**Server content**

Restore the `game/` directory from your content backup, then re-run the server
stage. `--delete` makes the next publish authoritative.

Every Pages deploy commit includes the engine SHA, so the source revision for a
published site is always recoverable.

## 11. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `/api/*` returns a Pages 404 | Origin Rule did not match, or the DNS record override is missing | Check the rule expression, Host header action, and DNS record action; test with `curl -I` |
| `/api/*` returns 522 or 1033 | Tunnel is down or ingress does not match the path | Check `cloudflared` status and the ingress `path` regexes; confirm `127.0.0.1:2290` is listening |
| `/ws/*` returns 400/426 or disconnects immediately | Origin Rule or proxy did not preserve `Upgrade` | Confirm the WS rule exists and points at the tunnel; test `/ws/combat` directly against `api.app...` |
| `/images/*` returns a Pages 404 | Images Origin Rule is missing or ordered after a broader rule | Add the rule or reorder it; verify the path prefix |
| Images return `application/octet-stream` | AVIF MIME pass skipped, or the AWS CLI guessed the type | Run the stage again; inspect with `curl -I`; add a WebP pass if needed |
| Stale shell after a deploy | `index.html` is cached | Confirm `_headers` is served from the site repo root and says `no-cache`; purge the Pages URL |
| Stale image after an upload | Browser/edge cache still within 1h | Purge the URL or prefix, or wait for the TTL |
| `install.sh` says `no publish destination configured` | `.deploy` is missing or has no `SITE_REPO`/`R2_BUCKET`/`SERVER_HOST` | Create `.deploy` from `.deploy.example` |
| `.deploy` not loaded | File missing, unreadable, or a shell syntax error | Confirm it is at the engine root; `chmod 600 .deploy`; check with `bash -n .deploy` |
| `npm ci` fails | Missing lockfile or `client/SimpleGame` | Restore `client/package-lock.json`; clone SimpleGame into `client/SimpleGame` |
| Site repo push fails | No remote, no credentials, or no permission | Add a remote / configure credentials; `install.sh` sets the upstream on the first push |
| Site repo push rejected (non-fast-forward) | Remote has commits the local site repo lacks | Pull/rebase the site repo, then re-run |
| Site repo commit fails | Git user name/email not configured | Set `user.name` and `user.email` in the site repo (or globally) |
| Server mirror fails | SSH key, host, or path wrong | Test `ssh "$SERVER_HOST"`; confirm `SERVER_PATH` contains `game/`; try `DRY_RUN=1` |
| Avatar 404 | Missing from R2 or the API returned an age-gate error | Check `game/images/avatars/safe/...`; request through `/api/avatar/<id>` with a valid session |
| Origin Rules unavailable on the plan | Plan limitation | Use a Worker route for the matching paths as a fallback |

## 12. Security

- `.deploy` is private, mode `600`, gitignored, and backed up outside the repo.
- `install.sh` never enables shell tracing and never prints credential values.
  R2 credentials are exported only for the AWS CLI commands.
- Use SSH or a Git credential helper for the site repo. A token embedded in the
  remote URL can appear in Git error output.
- The R2 bucket is private. Public reads happen only through the
  `assets.app.example.com` custom domain.
- The site repo is private; the engine repo contains no deployment values.
- `api.app.example.com` is a public API entry point. If that is undesirable,
  put Cloudflare Access or a service token in front of it and have the Origin
  Rule carry the token.
- Rotate the R2 API token if it is exposed; update `.deploy` and re-run.
- If `.deploy` was ever committed, rotate the exposed credentials and remove
  the file from history before pushing.
- Do not serve `game/config` or `game/text` from Pages or R2.

## 13. Generic vs site-specific

| In the engine (tracked) | Private (not tracked) |
|---|---|
| `install.sh` | `.deploy` |
| `docs/publishing.md` | site repo |
| `.deploy.example` | ops overlay |
| client/server source | R2 bucket, token, custom domain |
| game configs and text | tunnel credentials and hostname |
| `.gitignore` entries | `backup.sh` / `push_to_production.sh` |

The engine's build still works on its own with `npm run build`; publishing is an
optional layer on top.

## 14. Deferred work

- **Purge-on-deploy** when the R2 TTL is raised (Cloudflare API token and
  URL/prefix purge).
- **Audio prefixes** (`sounds/`, `music/`) — `install.sh` already uploads them
  when the directories exist; add Origin Rules and Cache Rules at the same time.
- **R2 versioning / object lock** for asset rollback.
- **Cloudflare Access** on `api.app.example.com`.
- **Worker fallback** if Origin Rules cannot route away from Pages as needed.
- **`wrangler pages deploy`** as an alternative to the Git-integrated site repo.
