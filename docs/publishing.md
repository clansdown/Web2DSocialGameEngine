# Publishing

This document describes how the game is published:

- the Svelte client is built into `client/dist` and uploaded to **Cloudflare Pages**;
- public art (and later audio) is uploaded to a private **R2** bucket and served
  through the `assets.<zone>` custom domain;
- the C++ API and `/ws/*` are exposed through a **Cloudflare Tunnel** at
  `api.<zone>`;
- `game/` is mirrored to the API server for `--content` (config, text, avatar
  pools, and the server's `/images` fallback);
- the API server's CORS allowlist is written to an env file from `.deploy`.

One command, `./install.sh`, performs the build and all configured deployment
stages. It pulls the prebuilt server image configured in `.deploy`; it never
builds or pushes that image. Publishing the engine server image is a separate
developer workflow (`publish_engine_server_image.sh`, configured by
`.publish_engine`). The engine stays generic: deployment values live in a
gitignored `.deploy` file. The site repo, ops overlay, `.deploy`, and
`.publish_engine` are **private** and are not part of this repository.

> The hostnames below (`app.example.com`, `api.example.com`,
> `assets.example.com`) are placeholders. Replace `example.com` with your
> Cloudflare zone.

> **Quick start**
>
> ```bash
> cp .deploy.example .deploy
> $EDITOR .deploy          # fill in the destinations, origins, image, and tag
> chmod 600 .deploy
> chmod +x install.sh      # once, if your checkout lacks the executable bit
> DRY_RUN=1 ./install.sh   # prints the deploy plan, writes nothing remote
> ./install.sh             # build -> R2 -> Pages -> server content/env
> ```
>
> Deployers do not need registry credentials or a local Docker build.
> `install.sh` pulls the already-published `SERVER_IMAGE:SERVER_IMAGE_TAG`.
>
> Developers publishing the engine image use `.publish_engine`:
>
> ```bash
> cp .publish_engine.example .publish_engine
> chmod 600 .publish_engine
> ./publish_engine_server_image.sh
> ```
>
> `bash install.sh` works too if the executable bit is not set.

## 1. Topology

The browser talks to three separate origins:

```
Browser
  https://app.example.com
    /                     → Cloudflare Pages (site repo root)
    /assets/*             → Cloudflare Pages (hashed Vite bundle)

  https://api.example.com
    /api/*                → Cloudflare Tunnel → 127.0.0.1:2290
    /ws/*                 → same tunnel (WebSocket upgrade)
    /images/*             → server fallback (normally not used by the client)

  https://assets.example.com
    /images/*             → R2 bucket key images/...
    /sounds/*             → R2 bucket key sounds/... (future)
    /music/*              → R2 bucket key music/... (future)
```

There is **no path routing** between these hosts and no Origin Rule or Worker.
**Avatar exception:** custom PFPs may be stored in the R2 bucket and resolved as
public content-addressed capability URLs (`avatars/<character>/<sha256>.<ext>`).
The server never sends those keys/URLs to minors; adult viewers of adult owners
receive them. This is an accepted out-of-band sharing exception (see
`docs/minor_content_visibility.md`).

The client is built with absolute API and asset origins:

- `VITE_API_BASE_URL` → `PUBLIC_API_ORIGIN`
- `VITE_ASSETS_BASE_URL` → `PUBLIC_ASSETS_ORIGIN`
- `VITE_WS_BASE_URL` → `PUBLIC_WS_ORIGIN`

Because the API is on a different origin from the Pages shell, the API must
answer CORS preflights and send the allowlist header. CORS is configured on the
API server, not in Cloudflare.

`/images/*` is entirely R2 in production. The server still serves a `/images`
fallback for direct API access and local development, but the client points at
`assets.example.com`.

`cloudflared` runs **on the API server** and dials out; no inbound firewall
ports are opened. The build host still reaches the API server over SSH/LAN for
the content mirror and env file. The tunnel is outbound only and does not
provide an inbound rsync path.

## 2. Prerequisites

On the build host:

- Node.js and npm (the version expected by `client/package.json`);
- `rsync`;
- Git;
- AWS CLI v2 (for the R2 upload);
- a complete working tree including the gitignored `client/SimpleGame` (the
  `file:./SimpleGame/ui` dependency) and the `game/` content tree (configs,
  text, images; only a few placeholders are tracked in the public engine);
- SSH access to the API server, if the server stages are enabled;
- Docker **only for developers publishing the server image**; deployment does
  not require a local Docker daemon or registry credentials.

Cloudflare:

- a zone (for example `example.com`);
- a Pages project;
- a private R2 bucket with a custom domain;
- a tunnel (`cloudflared`) on the API server.

> The `client/SimpleGame` checkout is required for the build. It is not tracked
> in this repository. Follow `README.md` to clone it into `client/SimpleGame`.

> **Certificate note:** `app.example.com`, `api.example.com`, and
> `assets.example.com` are one-level names, covered by normal Universal SSL.
> No Total TLS or Advanced Certificate Manager is required. If you choose
> deeper names such as `api.app.example.com`, they are not covered by
> Universal SSL and need ACM.

## 3. Repository roles

| Role | Visibility | Contents |
|---|---|---|
| Engine repo (this repository) | public | source, configs, `install.sh`, `publish_engine_server_image.sh`, `docs/publishing.md`, `.deploy.example`, `.publish_engine.example` |
| Site repo | **private** | generated Pages site only: `index.html`, `assets/`, `_headers`, `robots.txt`, favicon |
| Ops overlay | **private** | files copied into the site repo after the build sync (`_headers`, `robots.txt`, favicon, optional `_redirects`) |
| `.deploy` | **private** | deployment image/tag, site repo path, R2 credentials, public origins, API server host/path/CORS/restart command |
| `.publish_engine` | **private** | developer-only image repository, registry credentials, and platform |
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
| `PUBLIC_SITE_ORIGIN` | build/CORS | server enabled (CORS default) | no | `https://app.example.com` |
| `PUBLIC_API_ORIGIN` | build | Pages build | no | `https://api.example.com` — origin only, no `/api` |
| `PUBLIC_ASSETS_ORIGIN` | build | Pages build | no | `https://assets.example.com` — origin only, no `/images` |
| `PUBLIC_WS_ORIGIN` | build | optional | no | `wss://api.example.com`; empty derives from the API origin |
| `SERVER_HOST` | server | server enabled | no | `api-host.example.net` or an SSH config alias |
| `SERVER_PATH` | server | server enabled | no | parent of the server's `--content` dir; for `run_server.sh`, the directory containing `game/` |
| `SERVER_SSH_KEY` | server | optional | no | private key path; omit to use the SSH agent/config |
| `SERVER_CORS_ORIGINS` | server | optional override | no | comma-separated browser origins; empty uses `PUBLIC_SITE_ORIGIN` |
| `SERVER_ENV_PATH` | server | optional | no | default `${SERVER_PATH}/ravenest.env`; must be outside `game/` |
| `SERVER_RESTART_CMD` | server | optional | no | e.g. `sudo systemctl restart ravenest`; Docker mode defaults to `docker compose up -d` |
| `SERVER_DEPLOY_MODE` | server | optional | no | `docker` (default) or `systemd` |
| `SERVER_DB_MODE` | server | optional | no | `bundled` (default) or `external` |
| `SERVER_DB_USER` | server | bundled | no | default `ravenest`; PostgreSQL identifier |
| `SERVER_DB_PASSWORD` | server | bundled | **yes** | URL-safe password, e.g. `openssl rand -hex 24` |
| `SERVER_DB_INIT_PATH` | server | optional | no | default `${SERVER_PATH}/postgres/init` |
| `SERVER_COMPOSE_OVERLAY_PATH` | server | optional | no | default `${SERVER_PATH}/docker-compose.bundled.yml` |
| `SERVER_GAME_DB_URL` | server | external | **yes** | `postgresql://…/ravenest_game` |
| `SERVER_CHAT_DB_URL` | server | external | **yes** | `postgresql://…/ravenest_chat` |
| `SERVER_STAFF_DB_URL` | server | external | **yes** | `postgresql://…/ravenest_staff` |
| `SERVER_INVITES_DB_URL` | server | external | **yes** | `postgresql://…/ravenest_invites` |
| `SERVER_IMAGE` | server | Docker mode | **yes** | prebuilt lowercase repository path, e.g. `ghcr.io/org/ravenest-server` |
| `SERVER_IMAGE_TAG` | server | Docker mode | **yes** | `latest` or the immutable tag printed by `publish_engine_server_image.sh`; never derived by `install.sh` |
| `SERVER_PULL_CMD` | server | optional | no | remote pull command; default `docker pull 'SERVER_IMAGE:SERVER_IMAGE_TAG'` |
| `SERVER_COMPOSE_PATH` | server | optional | no | default `${SERVER_PATH}/docker-compose.yml` |
| `SERVER_COMPOSE_ENV_PATH` | server | optional | no | default `${SERVER_PATH}/compose.env` |
| `SERVER_UPLOADS_PATH` | server | optional | no | default `${SERVER_PATH}/uploads` |
| `SERVER_UPLOADS_OWNER` | server | optional | no | owner for the uploads bind mount; image runs as `10001:10001` |
| `SERVER_ALLOW_LOOPBACK_DB_URL` | server | optional | no | `1` silences the Docker loopback warning |
| `SERVER_AVATAR_STORAGE` | server | optional | no | `local` (default) or `r2` |
| `AVATAR_R2_ACCESS_KEY_ID` | server | avatar r2 | **yes** | separate scoped R2 token access key |
| `AVATAR_R2_SECRET_ACCESS_KEY` | server | avatar r2 | **yes** | separate scoped R2 token secret |
| `AVATAR_R2_PREFIX` | server | optional | no | default `avatars/` |
| `AVATAR_PUBLIC_BASE_URL` | server | avatar r2 | no | defaults to `PUBLIC_ASSETS_ORIGIN` |
| `AVATAR_CLOUDFLARE_ZONE_ID` | server | optional | no | defaults to `CF_ZONE_ID` |
| `AVATAR_CLOUDFLARE_API_TOKEN` | server | optional | **yes** | Cache Purge-only token; both zone+token required together |
| `CF_API_TOKEN` | deferred | optional | **yes** | reserved for future purge-on-deploy |
| `CF_ZONE_ID` | deferred | optional | no | default for `AVATAR_CLOUDFLARE_ZONE_ID`; reserved for purge-on-deploy |

### 4.1 `.publish_engine` reference

`.publish_engine` is only for developers publishing the engine server image.
It is sourced by `publish_engine_server_image.sh`, is gitignored, and must never
be committed. Deployers do not need it. Copy `.publish_engine.example`, set
mode `600`, and fill in:

| Key | Required | Secret | Example / notes |
|---|---|---|---|
| `SERVER_IMAGE` | yes | no | lowercase repository path, e.g. `ghcr.io/clansdown/web2dsocialgameengine`; no tag |
| `SERVER_REGISTRY_USERNAME` | with token | no | registry account, e.g. `clansdown` |
| `SERVER_REGISTRY_TOKEN` | with username | **yes** | classic PAT with `write:packages` for GHCR |
| `SERVER_PLATFORM` | no | no | `linux/amd64` (default) or `linux/arm64` |
| `SERVER_IMAGE_TAG` | no | no | immutable tag override; empty derives `<UTC timestamp>-<sha>`; never `latest` |

The publishing script refuses a dirty git tree, builds the image, pushes the
immutable tag first and `latest` second, and prints the immutable tag:

```text
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_IMAGE_TAG=20261106T120000Z-a1b2c3d
SERVER_IMAGE_LATEST=latest
SERVER_IMAGE_DIGEST=ghcr.io/...@sha256:...
```

Use `--no-latest` to skip the moving tag, `--tag` to override the immutable
tag, `--platform` to override the platform, `--config FILE` to select another
config file, and `--dry-run` to preview.

A stage is enabled by setting its primary key:

- Pages: `SITE_REPO`;
- R2: `R2_BUCKET` (plus endpoint and credentials);
- server: `SERVER_HOST` (plus `SERVER_PATH`).

If none of the three primary keys is set, `install.sh` fails before publishing.
`--skip-*` flags can disable an otherwise-configured stage.

`install.sh` is the **single place** that turns `.deploy` into:

- the three public origins baked into `client/dist`;
- the server's `RAVENEST_CORS_ORIGINS` env file.

Security notes:

- `chmod 600 .deploy`; `install.sh` warns if the mode is looser.
- `.deploy` is sourced, so it must contain assignments, not arbitrary commands.
- `install.sh` never uses `set -x` and never prints credential values. The four
  `SERVER_*_DB_URL` values are secrets. Registry tokens live only in
  `.publish_engine`, never in `.deploy`.
- The generated runtime env file is mode `600`; `compose.env` is mode `600`;
  the compose file itself is mode `644` and contains no secrets.
- Do not put `.deploy` in the site repo or the ops overlay.

## 5. First-time setup

Checklist:

- [ ] private site repo + private overlay (`_headers`, `robots.txt`, favicon);
- [ ] Pages project connected to the site repo + `app.example.com`;
- [ ] R2 bucket + `assets.example.com` custom domain + API token;
- [ ] R2 CORS policy for the Pages origin;
- [ ] `cloudflared` tunnel + `api.example.com` public hostname;
- [ ] API service reads `SERVER_ENV_PATH`, including all four
      `RAVENEST_*_DB_URL` values, and allows `RAVENEST_CORS_ORIGINS`;
- [ ] for Docker mode: a published `SERVER_IMAGE:SERVER_IMAGE_TAG` (public
      package, or a server-side Docker login for a private one) and a writable
      `${SERVER_UPLOADS_PATH}` owned by `10001:10001`;
- [ ] choose `SERVER_DB_MODE`: `bundled` (generate `SERVER_DB_PASSWORD`) or
      `external` (create the four databases and URLs);
- [ ] Cache Rules (bypass API host, cache assets host at 1h);
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
  `/images/*`; the client uses absolute origins for those.

The overlay is copied **after** the `client/dist` sync, so `rsync --delete`
cannot remove it before it is re-added. The overlay copy is additive: removing
a file from the overlay does not remove it from the site repo. Delete it from
the site repo (or reset the repo) if you need to retire an overlay file.

### 5.2 R2 bucket and custom domain

1. Create a private R2 bucket (for example `example-assets`).
2. Bind the custom domain `assets.example.com` to the bucket.
3. Create an R2 API token with Object Read & Write on the bucket.
4. Put the endpoint and token in `.deploy`.

Object keys keep the directory prefix:

```
images/...
sounds/...
music/...
```

The browser URL is therefore:

```
https://assets.example.com/images/...
https://assets.example.com/sounds/...
https://assets.example.com/music/...
```

`install.sh` uploads `game/images`, `game/sounds`, and `game/music` when those
directories exist. Do **not** upload `game/config` or `game/text` to R2. Those
are server content and are mirrored to the API server instead.

Add an R2 CORS policy for the Pages origin:

```json
{
  "rules": [
    {
      "allowed": {
        "origins": ["https://app.example.com"],
        "methods": ["GET", "HEAD"],
        "headers": ["*"]
      },
      "exposeHeaders": ["ETag", "Content-Length", "Content-Type"],
      "maxAgeSeconds": 86400
    }
  ]
}
```

The current client displays images with `<img>` and canvas drawing, which do not
require CORS, but the policy is harmless and makes future `fetch`/font/audio use
work. Do not add `http://localhost` origins; development uses the Vite proxy.

Do not set `Cache-Control` on the objects. The Cloudflare Cache Rule owns the
TTL, so it can be raised later without re-uploading. See §7.

### 5.3 Cloudflare Tunnel

Install `cloudflared` on the API server and create a tunnel:

```bash
cloudflared tunnel create example-api
cloudflared tunnel route dns example-api api.example.com
```

Use a config such as `/etc/cloudflared/config.yml`:

```yaml
tunnel: <tunnel-id>
credentials-file: /etc/cloudflared/<tunnel-id>.json

ingress:
  - hostname: api.example.com
    service: http://127.0.0.1:2290
  - service: http_status:404
```

No path regexes: the whole API host goes to the server, including `/api/*`,
`/ws/*`, and the `/images` fallback.

Run it as a service (`cloudflared service install`, or a systemd unit). The
C++ server still listens on plain HTTP on `127.0.0.1:2290`; TLS terminates at
Cloudflare.

WebSockets work through the tunnel without extra options. Cloudflare closes an
idle WebSocket after roughly 100 seconds; in-match 10 Hz ticks keep it alive,
and lobby sockets rely on the client's reconnect logic.

`api.example.com` is itself a public hostname. If a direct API entry point is
not desired, protect it with Cloudflare Access or a service token. CORS is not
an access-control boundary.

### 5.4 API CORS

The API reads `RAVENEST_CORS_ORIGINS` at startup. It is a comma-separated
allowlist of browser origins; empty/unset means `*`.

- `install.sh` writes this value to `SERVER_ENV_PATH` from `.deploy`
  (`SERVER_CORS_ORIGINS`, defaulting to `PUBLIC_SITE_ORIGIN`).
- The service must read that file. Systemd:

  ```ini
  [Service]
  EnvironmentFile=/home/ctl/example/ravenest.env
  ExecStart=/home/ctl/example/server --content /home/ctl/example/game
  ```

- Docker Compose:

  ```yaml
  services:
    ravenest:
      env_file:
        - /home/ctl/example/ravenest.env
  ```

The env file is mode `600` and contains no secrets, only the public allowlist.
`install.sh` backs up the previous file to `<path>.previous` and skips the write
and restart when the contents are unchanged.

The API answers `OPTIONS /api/*` with `204 No Content` and includes:

```
Access-Control-Allow-Origin: <allowed origin>
Access-Control-Allow-Methods: POST, GET, OPTIONS
Access-Control-Allow-Headers: Content-Type
Access-Control-Max-Age: 86400
Vary: Origin
```

`GET /api/avatar/<id>` also sends `Cache-Control: private, no-store`.

`SERVER_RESTART_CMD` is optional. When it is empty and the env file changes,
`install.sh` writes the file and prints a reminder to restart the API server;
the publish still succeeds. Set it to apply the change during publishing.

### 5.5 Cache Rules

Create two Cache Rules on the zone:

**Bypass the API host**

- Match: `http.host eq "api.example.com"`.
- Action: **Bypass cache**.

This includes `/api/*` and `/ws/*`. It is required for `GET /api/avatar/...`,
which is age-gated and must never be cached by Cloudflare.

**Cache public assets**

- Match: `http.host eq "assets.example.com"`.
- Action: **Cache eligible**.
- Edge TTL: **1 hour** (override origin).
- Browser TTL: **1 hour** (override origin).

Start at one hour. Once the art is stable, raise both values in the dashboard;
no re-upload is needed because the TTL is not stored on the R2 objects.

Pages caching is controlled by the site's `_headers`, not by these rules:
`index.html` is `no-cache`, `assets/*` is immutable.

### 5.6 Staging

Before the production cutover, publish to staging hostnames, for example:

- `staging.example.com` for Pages;
- `staging-api.example.com` for the tunnel;
- `staging-assets.example.com` for R2 (or reuse the production asset host).

Run the full verification checklist in §9 against staging. Use a second
working copy with a staging `.deploy` (staging site repo + staging Pages
project), or temporarily switch `SITE_REPO`/`SERVER_HOST` and the public
origins. Do not point a staging run at the production site repo.

If staging uses a different Pages origin, include it in
`SERVER_CORS_ORIGINS` and the R2 CORS policy.

## 6. Publishing

```bash
# See exactly what would happen; no remote writes.
DRY_RUN=1 ./install.sh

# Real publish.
./install.sh
```

The order is fixed:

1. **build** — `npm ci` and `npm run build` write `client/dist`, with the public
   origins baked in.
2. **R2** — public art is uploaded before the shell that references it.
3. **Pages** — `client/dist` is synced to the site repo, the overlay is copied,
   then the site repo is committed and pushed.
4. **server image** — in Docker mode, the prebuilt
   `SERVER_IMAGE:SERVER_IMAGE_TAG` is pulled on the API server.
5. **server content** — `game/` is mirrored to the API server.
6. **server env** — the four `RAVENEST_*_DB_URL` values (supplied or derived),
   CORS, and avatar variables are written to the server env file.
7. **compose files** — in Docker mode, `docker-compose.yml`, the bundled
   Postgres overlay/init SQL when selected, and `compose.env` are written next
   to the content.
8. **restart** — `SERVER_RESTART_CMD` runs after a change, if configured. In
   Docker mode it defaults to `docker compose up -d`.

### Options

| Option | Effect |
|---|---|
| `--dry-run` / `DRY_RUN=1` | No writes to R2, the site repo, or the API server. The local build still runs unless `--skip-build` is also given. |
| `--skip-build` | Reuse `client/dist`; it must contain `index.html` and its public-origin stamp must match `.deploy`. |
| `--skip-r2` | Skip the R2 stage even if `R2_BUCKET` is set. |
| `--skip-pages` | Skip the Pages stage even if `SITE_REPO` is set. |
| `--skip-server` | Skip the server content, env, and restart stages even if `SERVER_HOST` is set. |
| `--help` | Show usage. |

Publishing the server image is a separate command; see
`publish_engine_server_image.sh` and the `.publish_engine` reference above.

### Build stage

```bash
export VITE_API_BASE_URL="$PUBLIC_API_ORIGIN"
export VITE_ASSETS_BASE_URL="$PUBLIC_ASSETS_ORIGIN"
export VITE_WS_BASE_URL="$PUBLIC_WS_ORIGIN"
npm --prefix client ci --no-audit --no-fund
npm --prefix client run build
```

`npm ci` requires `client/package-lock.json` and a complete `client/SimpleGame`
checkout. Vite writes to `client/dist`; the engine never points Vite's `outDir`
at the site repo, because Vite empties its output directory.

When Pages is enabled and the build is running, `install.sh` hard-fails if
`PUBLIC_API_ORIGIN` or `PUBLIC_ASSETS_ORIGIN` is missing. `PUBLIC_WS_ORIGIN`
may be empty; the client derives `wss://` from the API origin.

After a successful build, `install.sh` writes
`client/.build-public-origins`. With `--skip-build`, it compares that stamp
with `.deploy` and refuses to publish a shell built for different origins.

The values are also documented in `client/.env.example`. Local development
uses `client/.env.development.local` (gitignored); empty values use the Vite
proxy to `localhost:2290`.

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

The public-asset uploader does not set `Cache-Control`; the Cache Rule owns the
TTL. Avatar objects are different: the server sets
`public, max-age=31536000, immutable` on upload. The uploader also sets the AWS
CLI checksum calculation/validation to `when_required`, because recent CLI v2
releases otherwise add checksums that R2 does not accept.

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

### Server content stage

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
server only reads its content directory; PostgreSQL holds game state and uploads
go to `uploads/avatars/` in the server's working directory. `install.sh` refuses
to mirror an empty `game/` tree, which would otherwise wipe the remote content.

Run `DRY_RUN=1 ./install.sh` before the first real mirror.

### Server env stage

`install.sh` generates the env file from `.deploy`:

```bash
# Generated by install.sh from .deploy; do not edit.
RAVENEST_GAME_DB_URL=postgresql://...
RAVENEST_CHAT_DB_URL=postgresql://...
RAVENEST_STAFF_DB_URL=postgresql://...
RAVENEST_INVITES_DB_URL=postgresql://...
RAVENEST_CORS_ORIGINS=https://app.example.com
RAVENEST_AVATAR_STORAGE=local
```

With `SERVER_DB_MODE=external`, all four database URLs are required and
passwords with URL-reserved characters must be percent-encoded. With
`SERVER_DB_MODE=bundled`, `install.sh` derives the same four URLs from
`SERVER_DB_USER`/`SERVER_DB_PASSWORD` and the compose service name `postgres`.
The values are never printed; a dry run only says “database URLs configured
(redacted)”.

It writes a local mode-`600` temp file, compares its SHA-256 with the remote
file, and skips the write/restart when unchanged. On a change it:

1. backs up the remote file to `${SERVER_ENV_PATH}.previous`;
2. copies the new file to `${SERVER_ENV_PATH}.new`;
3. atomically moves it into place and sets mode `600`;
4. runs `SERVER_RESTART_CMD` if configured.

If the restart fails, it restores `.previous` and retries the restart. If no
restart command is configured, it writes the file and prints a gentle reminder.

This stage does **not** deploy the server binary. In Docker mode the prebuilt
image pull and compose stages below do; otherwise binary rollout is separate.

### Avatar storage

Custom player PFPs have their own backend switch:

```bash
SERVER_AVATAR_STORAGE=local   # development / no R2
SERVER_AVATAR_STORAGE=r2      # production
```

With `r2`, `install.sh` adds these to the API env file (secrets are never
printed):

```bash
RAVENEST_AVATAR_STORAGE=r2
RAVENEST_R2_ENDPOINT=https://<account>.r2.cloudflarestorage.com
RAVENEST_R2_BUCKET=ravenest-assets
RAVENEST_R2_REGION=auto
RAVENEST_R2_ACCESS_KEY_ID=...
RAVENEST_R2_SECRET_ACCESS_KEY=...
RAVENEST_AVATAR_R2_PREFIX=avatars/
RAVENEST_AVATAR_PUBLIC_BASE_URL=https://assets.example.com
```

Use a **separate scoped R2 token** for the server (Object Read & Write on the
bucket). Do not reuse the deploy token. The bucket is the same one used for
public assets; avatars live under the `avatars/` prefix.

Objects are content-addressed: `avatars/<character_id>/<sha256>.<ext>`.
Uploads set `Cache-Control: public, max-age=31536000, immutable`. The public URL
is a capability URL and is only disclosed to an adult owner or adult viewer.
Removal/replacement deletes the object and purges the exact URL through the
Cloudflare API. Configure a **Cache Purge**-only token:

```bash
AVATAR_CLOUDFLARE_ZONE_ID=...
AVATAR_CLOUDFLARE_API_TOKEN=...
```

(The zone defaults to `CF_ZONE_ID`.) Without purge credentials the object is
still deleted; an already-cached edge copy may remain until its TTL.

R2 CORS is not required for `<img>` rendering. Add it only if avatars are later
fetched or drawn into a canvas.

**Release gate:** public capability URLs must not ship until the separate F7
age-isolation audit/fix is complete (server-enforced separation of minors from
adult messages, discovery, names, and avatars). The R2 path is not gated in code;
do not enable `SERVER_AVATAR_STORAGE=r2` in production before that sign-off.

### Docker server deployment

`SERVER_DEPLOY_MODE=docker` is the default. It pulls the prebuilt image and
adds a compose stage. `SERVER_DB_MODE` selects the database topology:

- `bundled` (default) — PostgreSQL runs in the same compose stack.
- `external` — the app runs in compose but PostgreSQL is managed elsewhere.

Existing host-managed/external installs must set `SERVER_DB_MODE=external`
explicitly; otherwise the default bundled mode rejects the leftover
`SERVER_*_DB_URL` values.

```bash
SERVER_DEPLOY_MODE=docker
SERVER_DB_MODE=bundled
SERVER_DB_USER=ravenest
SERVER_DB_PASSWORD=...       # openssl rand -hex 24
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_IMAGE_TAG=latest      # or the immutable tag printed by the publish script
```

`install.sh` requires an explicit tag; it never derives one. Use `latest` for
convenience or pin the immutable tag for reproducible production deployments.
It pulls the image on the API server before switching the compose files:

```text
docker pull 'SERVER_IMAGE:SERVER_IMAGE_TAG'
```

Override `SERVER_PULL_CMD` when the remote Docker command needs `sudo` or a
wrapper. Deployment does not require registry credentials or a local Docker
build. If the image is private, the API server needs its own Docker login; a
public package needs none.

`install.sh` writes:

- `${SERVER_COMPOSE_PATH}` (`docker-compose.yml`, mode `644`, app only);
- `${SERVER_COMPOSE_OVERLAY_PATH}` (`docker-compose.bundled.yml`, mode `644`,
  bundled only);
- `${SERVER_DB_INIT_PATH}/01-databases.sql` (mode `644`, bundled only);
- `${SERVER_COMPOSE_ENV_PATH}` (`compose.env`, mode `600`) with
  `RAVENEST_IMAGE`, `RAVENEST_ENV_FILE`, `RAVENEST_GAME_PATH`,
  `RAVENEST_UPLOADS_PATH`, and — for bundled — `RAVENEST_DB_USER`,
  `RAVENEST_DB_PASSWORD`, and `RAVENEST_DB_INIT_PATH`;
- `${SERVER_ENV_PATH}` (the runtime env above).

The image contains the compiled server only. Game content remains a separate
mount: compose maps `${SERVER_PATH}/game` to `/app/game` read-only. To deploy
content manually:

```bash
rsync -a game/ user@server:/srv/ravenest/game/
```

#### Publishing the engine server image

Publishing is developer-only and separate from deployment:

```bash
./publish_engine_server_image.sh
```

It reads `.publish_engine`, refuses a dirty git tree, builds
`server/Dockerfile` through `tools/build_server_image.sh`, and pushes:

```text
<SERVER_IMAGE>:<UTC timestamp>-<git sha>
<SERVER_IMAGE>:latest
```

The immutable tag is pushed first, then `latest`; `--no-latest` skips the
moving tag. It prints the immutable tag and digest:

```text
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_IMAGE_TAG=20261106T120000Z-a1b2c3d
SERVER_IMAGE_LATEST=latest
SERVER_IMAGE_DIGEST=ghcr.io/...@sha256:...
```

Set the printed immutable tag in `.deploy` for production. The image is built
from a clean commit; `-dirty` tags are never published.

Image paths must be lowercase, so the package name does not have to match the
GitHub repository name. The first push creates a **private** GHCR package by
default. Set it public once in GitHub:

> Package page → Package settings → Change visibility → Public

Once public, the deploy host pulls anonymously; no server-side `docker login`
is required. A private package needs a server-side login or a pre-authenticated
Docker daemon.

#### Diagnosing a denied GHCR push

`tools/diagnose_ghcr_push.sh` reads `.publish_engine` and inspects the token
identity, scopes, namespace, existing package, and linked-repository access. It
is read-only by default, never prints the token, and accepts
`--publish-config FILE` for a different file:

```bash
./tools/diagnose_ghcr_push.sh
```

To verify actual write access with a throwaway package:

```bash
./tools/diagnose_ghcr_push.sh --push-test
```

The push test creates `ghcr.io/<owner>/ravenest-ghcr-diagnostic`; remove it from
GitHub Packages afterwards. The script exits non-zero when it finds a blocking
configuration, authentication, package, or Docker problem.

#### Bundled PostgreSQL

The overlay runs `postgres:16-alpine` and an idempotent `db-init` one-shot
service. `db-init` creates all four databases on every `up`, so it also handles
an existing data volume. The app waits for `db-init` to complete successfully.

The database is internal to the compose network; no host port is published. The
app receives derived URLs:

```bash
postgresql://ravenest:<password>@postgres:5432/ravenest_game
postgresql://ravenest:<password>@postgres:5432/ravenest_chat
postgresql://ravenest:<password>@postgres:5432/ravenest_staff
postgresql://ravenest:<password>@postgres:5432/ravenest_invites
```

`SERVER_DB_PASSWORD` must use URL-safe characters because the raw value is used
by PostgreSQL and embedded in those URLs. `openssl rand -hex 24` is the
recommended generator. `db-init` also resets the database role password to the
current `.deploy` value on every `up`, so rotating the password does not strand
the app.

Data lives in the `ravenest_pgdata` Docker volume. Back it up with:

```bash
cd "$SERVER_PATH"
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml \
  exec -T postgres pg_dumpall -U ravenest > "backup-$(date -u +%Y%m%dT%H%M%SZ).sql"
```

Restore into an empty stack with:

```bash
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml \
  exec -T postgres psql -U ravenest -d postgres < backup.sql
```

#### External PostgreSQL

With `SERVER_DB_MODE=external`, only the app compose file is used. PostgreSQL
must already have the four databases and a role with access. Supply all four
`SERVER_*_DB_URL` values; credentials are embedded in the URLs and may be
percent-encoded. For managed databases, include the provider's TLS setting, for
example `?sslmode=require`.

Do not point external Docker DB URLs at `127.0.0.1`: inside the bridge network
that is the app container. Use the host's LAN address, a managed hostname, or a
compose service name.

#### Restart and rollback

If `SERVER_RESTART_CMD` is empty, Docker mode defaults it to:

```bash
# bundled
cd "$SERVER_PATH" && docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml up -d --remove-orphans

# external
cd "$SERVER_PATH" && docker compose --env-file compose.env \
  -f docker-compose.yml up -d --remove-orphans
```

The restart runs when the runtime env, either compose file, the database init
SQL, or `compose.env` changes. A successful image pull also forces the compose
`up`, so a moving `latest` tag takes effect even when `compose.env` is
byte-identical; Compose only recreates containers whose image or config changed.

The app container mounts `${SERVER_PATH}/game` read-only and
`${SERVER_UPLOADS_PATH}` read-write. The image runs as `10001:10001`; if the SSH
deploy user is root, `install.sh` chowns the uploads directory automatically.
Otherwise set `SERVER_UPLOADS_OWNER=10001:10001` or chown it manually.

Rollback: restore `${SERVER_COMPOSE_ENV_PATH}.previous`, then run the applicable
compose command; or rerun `install.sh` with the previous `SERVER_IMAGE_TAG`.

Switching `SERVER_DB_MODE` does **not** migrate data. Dump the old database and
restore it into the new one before changing modes. When switching away from
bundled, bring the old stack down first so `postgres` is not left running:

```bash
cd "$SERVER_PATH"
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml down
```

`docker build` needs the server source context but not `game/`. The Dockerfile
uses Ubuntu 24.04, so a developer publishing an image must be able to reach the
distro and CMake `FetchContent` sources during the build. Deployment hosts only
pull the finished image.

### Preflight checks

Before deploying, `install.sh`:

- requires `client/SimpleGame/ui` for a normal publish;
- refuses to run if `.deploy`, `.publish_engine`, `backup.sh`, or
  `push_to_production.sh` is tracked by git;
- refuses to run if `server/build*` or `game_balance_analyzer/build*` is tracked;
- refuses to publish into `/`, your home directory, a directory that looks like
  a source repo, a non-empty site repo without `index.html`, or a site repo
  that is, contains, or lives inside the engine checkout; refuses an overlay
  inside the site repo (the Pages sync uses `--delete`);
- validates public origins (scheme + host only) and hard-fails if the Pages
  build is missing the API or asset origin;
- validates `SERVER_CORS_ORIGINS` (or `PUBLIC_SITE_ORIGIN`) and
  `SERVER_ENV_PATH` when the server stage is enabled;
- resolves `SERVER_DB_MODE` (`bundled` default, `external`) and rejects
  `systemd` + `bundled`;
- for `external`, requires all four `SERVER_*_DB_URL` values, checks the
  `postgresql://` / `postgres://` scheme, and rejects whitespace/quoting
  characters;
- for `bundled`, requires `SERVER_DB_USER`/`SERVER_DB_PASSWORD`, checks the
  password is URL-safe, rejects any `SERVER_*_DB_URL`, and derives the four
  URLs;
- in Docker mode, requires an explicit `SERVER_IMAGE_TAG`, validates
  `SERVER_IMAGE` and the tag, and validates the compose paths and selected local
  compose files; it does not require registry credentials or local Docker;
  warns when an external DB URL points at loopback;
- when `SERVER_AVATAR_STORAGE=r2`, validates the R2 endpoint/bucket/separate
  token, the public avatar base origin, and the object prefix;
- warns about tracked files larger than 50 MiB and refuses to run if any tracked
  file exceeds GitHub's 100 MiB limit.

These guard the repository, the build, and the publish destinations.

## 7. Cache policy

| Asset | Cache |
|---|---|
| Pages `/` and `/index.html` | `no-cache` via `_headers` |
| Pages `/sw.js` | `no-cache` via `_headers` (service worker updates) |
| Pages `/assets/*` | `public, max-age=31536000, immutable` via `_headers` (hashed by Vite) |
| `assets.example.com/*` | Cloudflare Cache Rule: Edge 1h, Browser 1h |
| `assets.example.com/avatars/*` | object `Cache-Control: public, max-age=31536000, immutable`; purge exact URL on removal |
| `api.example.com/*` | Cloudflare Cache Rule: bypass |
| `/api/avatar/<id>` | origin sends `Cache-Control: private, no-store`; local streams bytes, R2 redirects |

Raising the R2 TTL later is a Cache Rule edit, not a re-upload. Browsers that
already cached an object keep it until their own copy expires; use **Purge
Cache** on the affected URL or prefix if an asset must change immediately.

## 8. Server mirror and runtime details

- **Content source:** `game/` — `config/`, `text/`, `images/`, and future audio.
- **Content target:** `${SERVER_PATH}/game/`, the server's `--content` directory.
- **Excludes:** `*.db`, `*.xcf`, `.git/`, `uploads/`, `*.md`, `.gitkeep`.
- **Runtime env:** `${SERVER_ENV_PATH}` (default `${SERVER_PATH}/ravenest.env`),
  containing the four `RAVENEST_*_DB_URL` values, `RAVENEST_CORS_ORIGINS`, and
  the avatar-storage variables above.
- **Compose env:** Docker mode also writes `${SERVER_COMPOSE_ENV_PATH}` with the
  image tag and mount paths.
- **Image pull:** Docker mode pulls `SERVER_IMAGE:SERVER_IMAGE_TAG` on the API
  server before switching compose files (`SERVER_PULL_CMD` overrides the command).
- **Restart:** optional `SERVER_RESTART_CMD`; Docker mode defaults it to
  `docker compose up -d`.

Why the server still needs a full `game/` copy:

- `GameConfigCache` reads `config/`;
- `TextManager` reads `text/`;
- `AvatarPool` scans `images/avatars/safe/` and `images/retinue/headshots/`;
- the server's `/images` routes remain available as a fallback (for example,
  when using the API host directly or during local development).

`game/config` and `game/text` are never published to R2 or Pages.

The env file must live outside `game/`; the content mirror uses `--delete` and
would remove anything under it.

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

### Avatar uploads

With `SERVER_AVATAR_STORAGE=r2`, log in as an adult character and upload a PNG.
The response must contain a content-addressed key and an absolute
`https://assets.../avatars/<id>/<sha256>.<ext>` URL:

```bash
curl -sI "https://assets.example.com/avatars/<id>/<sha256>.png"
# HTTP/2 200
# content-type: image/png
# cache-control: public, max-age=31536000, immutable
```

Confirm `GET /api/avatar/<id>?username=...&token=...` returns `302` to that URL
for an authorized adult, and `404` for an unauthenticated or minor viewer. After
removal, the origin object should be gone (404) and the exact URL purged at the
edge.

### R2 images

```bash
curl -sI https://assets.example.com/images/manor/grass.png
# HTTP/2 200, content-type: image/png

curl -sI https://assets.example.com/images/<file>.avif
# content-type: image/avif

curl -sI -H 'Origin: https://app.example.com' \
  https://assets.example.com/images/manor/grass.png | grep -i access-control
# access-control-allow-origin: https://app.example.com
```

If `content-type` is `application/octet-stream`, the AVIF pass did not run or
the file has a different extension.

### API and CORS

```bash
# Preflight
curl -si -X OPTIONS https://api.example.com/api/getTexts \
  -H 'Origin: https://app.example.com' \
  -H 'Access-Control-Request-Method: POST' \
  -H 'Access-Control-Request-Headers: content-type'
# HTTP/2 204, access-control-allow-origin: https://app.example.com

# Actual request
curl -si -X POST https://api.example.com/api/getTexts \
  -H 'Origin: https://app.example.com' \
  -H 'content-type: application/json' \
  -d '{"language":"en","text_ids":["ui_login_title"]}'
# HTTP/2 200, access-control-allow-origin: https://app.example.com
```

`/api/*` must return `cf-cache-status: BYPASS` or `DYNAMIC`, not `HIT`.
`GET /api/avatar/<character_id>` must remain uncached and age-gated.

A `No 'Access-Control-Allow-Origin' header` browser error means the origin is
not in `RAVENEST_CORS_ORIGINS`, or the env file was written but the server was
not restarted.

### WebSocket

Open the game in a browser and start combat, or use a WebSocket client against
`wss://api.example.com/ws/combat`. The handshake must return `101 Switching
Protocols`; the first client message is the auth envelope. The local
`tools/combat_smoke.sh` verifies the protocol against a local server, not the
remote host.

### Client bundle

Check that the built shell points at the intended hosts:

```bash
grep -R "api.example.com" client/dist/assets | head
grep -R "assets.example.com" client/dist/assets | head
```

With `--skip-build`, `install.sh` already compared the build stamp. A manual
`npm run build` does not write a stamp; rebuild through `install.sh` before a
real publish.

### Server content and env

```bash
ssh "$SERVER_HOST" ls -d \
  "$SERVER_PATH/game/config" \
  "$SERVER_PATH/game/text" \
  "$SERVER_PATH/game/images/avatars/safe" \
  "$SERVER_PATH/game/images/retinue/headshots"

ssh "$SERVER_HOST" cat "$SERVER_ENV_PATH"
# RAVENEST_GAME_DB_URL=postgresql://...
# RAVENEST_CHAT_DB_URL=postgresql://...
# RAVENEST_STAFF_DB_URL=postgresql://...
# RAVENEST_INVITES_DB_URL=postgresql://...
# RAVENEST_CORS_ORIGINS=https://app.example.com
```

In Docker mode with `SERVER_DB_MODE=external`:

```bash
ssh "$SERVER_HOST" \
  "cd '$SERVER_PATH' && docker compose --env-file compose.env -f docker-compose.yml ps"
ssh "$SERVER_HOST" \
  "cd '$SERVER_PATH' && docker compose --env-file compose.env -f docker-compose.yml logs --tail=100 ravenest"
```

With `SERVER_DB_MODE=bundled`, add `-f docker-compose.bundled.yml` and check
that `postgres` is healthy and `db-init` exited `0`:

```bash
ssh "$SERVER_HOST" \
  "cd '$SERVER_PATH' && docker compose --env-file compose.env -f docker-compose.yml -f docker-compose.bundled.yml ps"
ssh "$SERVER_HOST" \
  "cd '$SERVER_PATH' && docker compose --env-file compose.env -f docker-compose.yml -f docker-compose.bundled.yml logs --tail=100 postgres db-init ravenest"
```

Removed art must be gone from the remote tree (the mirror uses `--delete`).

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

**Server env**

Restore `${SERVER_ENV_PATH}.previous` and restart the service. `install.sh`
creates that backup before each env change.

**Docker image**

Rerun `install.sh` with the previous `SERVER_IMAGE_TAG`, or restore
`${SERVER_COMPOSE_ENV_PATH}.previous` and run the applicable compose command:

```bash
cd "$SERVER_PATH"
# external
docker compose --env-file compose.env -f docker-compose.yml up -d --remove-orphans
# bundled
docker compose --env-file compose.env \
  -f docker-compose.yml -f docker-compose.bundled.yml up -d --remove-orphans
```

The previous image tag must still exist in the registry (tags are immutable;
do not overwrite them).

**Client public origins**

Changing `PUBLIC_API_ORIGIN`, `PUBLIC_ASSETS_ORIGIN`, or `PUBLIC_WS_ORIGIN`
requires a rebuild (or a `--skip-build` stamp match). There is no runtime config
file in the Pages shell.

Every Pages deploy commit includes the engine SHA, so the source revision for a
published site is always recoverable.

## 11. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Browser reports a CORS error on `/api/*` | Origin missing from `RAVENEST_CORS_ORIGINS`, or env changed but server not restarted | Check `$SERVER_ENV_PATH`, preflight with `curl`, restart the service |
| `OPTIONS /api/*` returns 404/405 | Server binary does not have the CORS routes | Rebuild and deploy the server binary; confirm the prefix `OPTIONS /api/*` |
| API 404 | `PUBLIC_API_ORIGIN` includes `/api`, or points at Pages | Use the API origin only (`https://api.example.com`) |
| Images 404 | `PUBLIC_ASSETS_ORIGIN` empty/wrong, or R2 key missing | Check the build stamp, the R2 path, and `curl -I` on the asset host |
| Images return `application/octet-stream` | AVIF/WebP MIME pass skipped | Re-run the stage; inspect with `curl -I` |
| Mixed-content or blocked WS | Page is https but API/WS is http/ws | Use `https://` and `wss://` |
| WebSocket disconnects immediately | Tunnel/proxy did not preserve `Upgrade` | Confirm the tunnel host routes all paths; test `/ws/combat` directly |
| Stale shell after a deploy | `index.html` cached | Confirm `_headers` is served from the site repo root and says `no-cache`; purge Pages |
| Stale image after an upload | Browser/edge cache still within 1h | Purge the URL or prefix, or wait for the TTL |
| `install.sh` says `PUBLIC_API_ORIGIN is required` | Missing build origin | Add it to `.deploy`; this is intentional for split-host Pages |
| `client/dist public origins do not match .deploy` | `--skip-build` reused a differently configured build | Re-run without `--skip-build` |
| `SERVER_CORS_ORIGINS or PUBLIC_SITE_ORIGIN is required` | Server enabled without an allowlist | Set `SERVER_CORS_ORIGINS` or `PUBLIC_SITE_ORIGIN` |
| `SERVER_GAME_DB_URL is not set` | `SERVER_DB_MODE=external` without the four DB URLs | Add all four `SERVER_*_DB_URL` values to `.deploy` |
| `SERVER_DB_PASSWORD is required` | Bundled mode without a password | Set `SERVER_DB_PASSWORD` (`openssl rand -hex 24`) |
| `SERVER_*_DB_URL values are set while SERVER_DB_MODE=bundled` | Leftover external URLs | Set `SERVER_DB_MODE=external` or clear the URLs |
| `SERVER_DB_MODE=bundled requires SERVER_DEPLOY_MODE=docker` | systemd app with bundled DB | Use `external`, or switch the app to Docker |
| `db-init` exits non-zero | PostgreSQL not healthy, wrong password, or missing init SQL | Check `postgres`/`db-init` logs; confirm `SERVER_DB_INIT_PATH/01-databases.sql` exists |
| Bundled app restarts before DB is ready | `db-init` dependency not merged | Confirm both `-f` compose files are used |
| `SERVER_IMAGE must be a full repository path` | Docker mode without a registry/repository | Set `SERVER_IMAGE=ghcr.io/org/name` |
| `SERVER_IMAGE must be lowercase` | GHCR/repository path contains uppercase | Use a lowercase package path, e.g. `ghcr.io/org/ravenest-server` |
| `SERVER_IMAGE_TAG is not set` | Docker deploy without a pinned tag | Set `latest` or the immutable tag printed by `publish_engine_server_image.sh` |
| `docker login failed` | Publishing with bad registry credentials | Fix `SERVER_REGISTRY_USERNAME`/`SERVER_REGISTRY_TOKEN` in `.publish_engine` |
| `docker push` returns `denied` | GHCR token, package, or namespace permission | Run `./tools/diagnose_ghcr_push.sh --push-test` |
| `publish_engine_server_image.sh` refuses to run | Working tree is dirty | Commit or stash changes; publishing always uses a clean commit |
| DB URL points at loopback in Docker mode | `127.0.0.1` is the app container, not the host | Use the host LAN address, managed hostname, or compose service name |
| Container cannot write uploads | `${SERVER_UPLOADS_PATH}` is not owned by `10001:10001` | Set `SERVER_UPLOADS_OWNER=10001:10001` or chown it |
| `docker compose up` cannot find `RAVENEST_IMAGE` | `compose.env` missing or wrong path | Check `SERVER_COMPOSE_ENV_PATH` and the `docker compose --env-file` argument |
| `install.sh` says `no publish destination configured` | `.deploy` missing or has no primary key | Create `.deploy` from `.deploy.example` |
| `.deploy` not loaded | File missing, unreadable, or a shell syntax error | Confirm it is at the engine root; `chmod 600 .deploy`; check with `bash -n .deploy` |
| `npm ci` fails | Missing lockfile or `client/SimpleGame` | Restore `client/package-lock.json`; clone SimpleGame into `client/SimpleGame` |
| Site repo push fails | No remote, no credentials, or no permission | Add a remote / configure credentials; `install.sh` sets the upstream on the first push |
| Site repo push rejected (non-fast-forward) | Remote has commits the local site repo lacks | Pull/rebase the site repo, then re-run |
| Site repo commit fails | Git user name/email not configured | Set `user.name` and `user.email` in the site repo (or globally) |
| Server restart failed | `SERVER_RESTART_CMD` wrong, sudo needs a TTY, or service failed to start | Test the command over SSH; `install.sh` restored the previous env and retried |
| Server content mirror fails | SSH key, host, or path wrong | Test `ssh "$SERVER_HOST"`; confirm `SERVER_PATH` contains `game/`; try `DRY_RUN=1` |
| `SERVER_ENV_PATH must be outside game/` | Env file placed under the mirrored content tree | Move it to `${SERVER_PATH}/ravenest.env` or another path outside `game/` |
| Avatar 404 | Missing from R2/API, or age-gate error | Check `game/images/avatars/safe/...`; request `/api/avatar/<id>` with a valid session |

## 12. Security

- `.deploy` is private, mode `600`, gitignored, and backed up outside the repo.
- `.publish_engine` is private, mode `600`, gitignored, and contains the registry
  token. Deployment never reads it; only `publish_engine_server_image.sh` does.
- `install.sh` never enables shell tracing and never prints credential values.
  R2 credentials are exported only for the AWS CLI commands.
- `publish_engine_server_image.sh` passes the registry token to `docker login`
  through stdin and never prints it.
- Use SSH or a Git credential helper for the site repo. A token embedded in the
  remote URL can appear in Git error output.
- The R2 bucket is private. Public reads happen only through the
  `assets.example.com` custom domain.
- `api.example.com` is a public API entry point. CORS is a browser policy, not
  authentication; the API still requires explicit JSON/query credentials.
- The server env file contains the four database URLs (with credentials) and,
  for bundled mode, the compose stack also stores the database password in
  `compose.env`. Both files are written mode `600`.
- The site repo is private; the engine repo contains no deployment values.
- Rotate the R2 API token if it is exposed; update `.deploy` and re-run.
- If `.deploy` was ever committed, rotate the exposed credentials and remove
  the file from history before pushing.
- Do not serve `game/config` or `game/text` from Pages or R2.
- Custom-avatar URLs carry `username` and `token` in the query string (browser
  image requests cannot send a JSON auth body). They are never cached by the
  server or Cloudflare.

## 13. Generic vs site-specific

| In the engine (tracked) | Private (not tracked) |
|---|---|
| `install.sh`, `publish_engine_server_image.sh` | `.deploy`, `.publish_engine` |
| `docs/publishing.md` | site repo |
| `.deploy.example`, `.publish_engine.example` | ops overlay |
| client/server source | R2 bucket, token, custom domain |
| game configs and text | tunnel credentials, hostname, registry token |
| `.gitignore` entries | `backup.sh` / `push_to_production.sh` |
| `client/.env.example` | `client/.env.development.local` |

The engine's build still works on its own with `npm run build`; publishing is an
optional layer on top. A manual `npm run build` with no `VITE_*` values produces
a same-origin shell (the legacy reverse-proxy layout).

## 14. Deferred work

- **Purge-on-deploy** when the R2 TTL is raised (Cloudflare API token and
  URL/prefix purge).
- **Audio prefixes** (`sounds/`, `music/`) — `install.sh` already uploads them
  when the directories exist; add them to the R2 CORS policy and cache rule.
- **R2 versioning / object lock** for asset rollback.
- **Cloudflare Access** on `api.example.com`.
- **`wrangler pages deploy`** as an alternative to the Git-integrated site repo.
- **Server binary deployment** — `install.sh` mirrors content and writes the
  runtime env, but does not build or copy the server binary.
