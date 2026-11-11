# Web2DSocialGameEngine
A game engine for Social 2D (Sprite-based) Web games. Client + Server.

## Safety and age visibility

Player-authored content is projected per viewer. Minors receive only
server-owned safe names/avatars/messages/baronies/reactions/GIFs and may author
only safe catalog content. See
[`docs/minor_content_visibility.md`](docs/minor_content_visibility.md).

## Prerequisites

Before running the game engine, clone SimpleGame into the SimpleGame/ directory:

```bash
git clone https://github.com/clansdown/SimpleGame.git SimpleGame
```

The SimpleGame repository is required for this project to function correctly.

## Configuration Files & Docker Deployment

### Required Configuration Files

Create the following files in the root directory (these files are in `.gitignore`):

#### `.game_name` (legacy)
Used by `build_release.sh` for legacy Docker image naming.
```
Ravenest
```

#### `.deploy` — deployment configuration

Copy `.deploy.example` to `.deploy` and fill in the instance settings. A Docker
deployment pulls an already-published image; it does not build or push one:

```bash
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_IMAGE_TAG=latest
```

No registry credentials are needed to deploy. Use `latest` for convenience, or
pin the immutable tag printed by the publishing script for reproducible
production deployments. Image paths must be lowercase.

Game content is not in the image: compose mounts `${SERVER_PATH}/game`
read-only at `/app/game`, so configs are copied separately.

#### `.publish_engine` — developer image publishing

Only developers publishing engine server images need this file. Copy
`.publish_engine.example` to `.publish_engine`, `chmod 600` it, and set:

```bash
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_REGISTRY_USERNAME=clansdown
SERVER_REGISTRY_TOKEN=ghp_xxxxxxxxxxxxxxxx  # PAT with write:packages
SERVER_PLATFORM=linux/amd64
```

Then publish from a clean working tree:

```bash
./publish_engine_server_image.sh
```

The script pushes an immutable `<UTC timestamp>-<git sha>` tag and `latest`. It
refuses dirty trees. On GHCR, the first push creates a **private** package; set
it public once (package settings → Change visibility → Public) so deploy hosts
can pull anonymously.

#### Legacy `.docker_repo` / `.docker_creds`

`build_release.sh` still reads these legacy files:

```text
.docker_repo:  ghcr.io/yourusername/ravenest-server
.docker_creds: yourusername:ghp_xxxxxxxxxxxxxxxx
```

New publishing should use `.publish_engine` and
`publish_engine_server_image.sh`.

#### Image build helper

`build_server.sh`, `build_release.sh`, and `publish_engine_server_image.sh`
build the multi-stage `server/Dockerfile` through
`tools/build_server_image.sh`. The production image runs as uid `10001`, expects
game content at `/app/game`, and writes uploads to `/app/uploads`.

Production deployment is done with `install.sh`, which pulls the configured
prebuilt image and writes the compose files:

```bash
# .deploy — bundled PostgreSQL (default)
SERVER_DEPLOY_MODE=docker
SERVER_DB_MODE=bundled
SERVER_DB_USER=ravenest
SERVER_DB_PASSWORD=...       # generate once: openssl rand -hex 24
SERVER_IMAGE=ghcr.io/clansdown/web2dsocialgameengine
SERVER_IMAGE_TAG=latest
```

In Docker mode, `install.sh` copies operational tools to
`${SERVER_PATH}/tools/`:

```bash
ssh user@server /srv/ravenest/tools/verify_deployment.sh
ssh user@server /srv/ravenest/tools/restart_deployment.sh
```

The verifier checks the image/tag, content and uploads mounts, container health,
port binding, HTTP response, and bundled PostgreSQL volume/databases. It does
not run automatically.

The restart helper runs `docker compose up -d` and then restarts the app, which
is currently required for config changes. Use `--pull` after an image tag change
and `--verify` to run the verifier afterward. A safe config-reload design that
avoids restarting is tracked in `docs/TODO_config_reload.md`.

The same directory contains `staff_admin.sh` and `invite_admin.sh` wrappers:

```bash
# Owner / superuser:
ssh user@server /srv/ravenest/tools/staff_admin.sh grant <username> superadmin

# Standard staff admin:
ssh user@server /srv/ravenest/tools/staff_admin.sh grant <username> customer_service

ssh user@server /srv/ravenest/tools/invite_admin.sh create --count 5
```

They run the corresponding CLI inside the running `ravenest` container, so no
local build or host copy of the binaries is needed. Run `staff_admin.sh roles`
to list presets; aliases `superuser` and `staff` map to `superadmin` and
`customer_service`.

Operational diagnostics are also installed there:

```bash
# Fast app/database/HTTP/status check
ssh user@server /srv/ravenest/tools/deployment_status.sh

# Generic filtered logs (read-only, redacted)
ssh user@server /srv/ravenest/tools/fetch_logs.sh --since 30m
ssh user@server /srv/ravenest/tools/fetch_logs.sh \
  --grep 'authRefresh|refresh failed|could not load session'

# Diagnose a refresh-cookie failure for one session
ssh user@server /srv/ravenest/tools/diagnose_auth.sh \
  --cookie-file /tmp/ravenest-refresh.cookie
```

`diagnose_auth.sh` never prints the refresh or access token. Prefer
`RAVENEST_REFRESH_COOKIE` or `--cookie-file`; `--cookie-stdin` is also
available. Use `--dry-run` to check the refresh endpoint without a cookie.

`deploy/docker-compose.yml` is the app-only base. With
`SERVER_DB_MODE=bundled`, `install.sh` also writes
`deploy/docker-compose.bundled.yml` and a `db-init` service that creates the four
databases; PostgreSQL runs in the same stack with no published port and its data
in the `ravenest_pgdata` volume.

For managed or host-managed PostgreSQL, set `SERVER_DB_MODE=external` and supply
the four `SERVER_GAME_DB_URL` / `SERVER_CHAT_DB_URL` / `SERVER_STAFF_DB_URL` /
`SERVER_INVITES_DB_URL` values. `systemd` app deployments must use `external`.

Local development can use the `app` profile in the root `docker-compose.yml`:

```bash
RAVENEST_DB_PASSWORD=ravenest_dev docker compose --profile app up -d --build
```

## Nginx Configuration

> The current production deployment uses Cloudflare Pages, R2, and a Cloudflare
> Tunnel with separate `app`, `api`, and `assets` origins; see
> [`docs/publishing.md`](docs/publishing.md). The nginx configuration below is a
> same-origin/self-hosting alternative. If you split the API onto its own
> origin, configure `RAVENEST_CORS_ORIGINS` as described there.

The uWebSockets server listens on HTTP only (port 2290). HTTPS termination should be handled by a reverse proxy such as nginx, with certificates managed by certbot (Let's Encrypt).

### Example Nginx Configuration

```nginx
server {
    listen 80;
    server_name example.com;

    # API endpoints - forward to uWebSockets server
    location /api/ {
        proxy_pass http://localhost:2290;

        # Use HTTP 1.1
        proxy_http_version 1.1;
        proxy_set_header Connection "";

        # Client identifying headers
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Forwarded-Host $host;
        proxy_set_header X-Forwarded-Port $server_port;
        proxy_set_header User-Agent $http_user_agent;
        proxy_set_header Host $host;

        # Request tracking (optional)
        proxy_set_header X-Request-ID $request_id;
    }

    # Frontend static assets (example)
    location / {
        root /var/www/frontend;
        try_files $uri $uri/ /index.html;
    }
}
```

### Setting Up HTTPS

Use certbot to configure HTTPS support:

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d example.com
```

Certbot will automatically configure SSL certificates and update the nginx configuration for HTTPS.

### Header Reference

The following headers are forwarded to the uWebSockets server for client identification:

| Header | Purpose |
|--------|---------|
| X-Real-IP | True client IP address |
| X-Forwarded-For | Forwarded IP chain (if behind additional proxies) |
| X-Forwarded-Proto | Original protocol (http/https) |
| X-Forwarded-Host | Original hostname |
| X-Forwarded-Port | Original port |
| User-Agent | Client browser information |
| Host | Original host header |
| X-Request-ID | Unique request identifier for logging/debugging |

The uWebSockets server can access these headers via the HttpRequest object in endpoint handlers.

## Build Scripts

### Local Build (No Docker Push)

```bash
./build_server.sh
```
Builds server binary and Docker image locally. Saves Docker tar to `server/bin/ravenest-server.tar`. No network or authentication required.

### Publishing (Cloudflare Pages + R2 + API tunnel)

`./install.sh` builds the client, uploads public assets to R2, publishes the
site repo to Cloudflare Pages, mirrors `game/` to the API server, and writes the
API server's CORS env file. The Pages site, API, and assets are three separate
origins; the client bakes their addresses in at build time from `.deploy`.
See [`docs/publishing.md`](docs/publishing.md) for setup, the `.deploy`
reference, CORS, cache policy, verification, and rollback.

### Publish the Engine Server Image

```bash
./publish_engine_server_image.sh              # Immutable tag + latest
./publish_engine_server_image.sh --no-latest  # Immutable tag only
./publish_engine_server_image.sh --tag v1.0.0 # Explicit immutable tag
```

Publishes from a clean git tree using `.publish_engine`, then prints the
immutable tag for pinning in `.deploy`.

Legacy `build_release.sh` still builds and pushes using `.docker_repo` and
`.docker_creds`; prefer `publish_engine_server_image.sh`.

## Local Testing

### Interactive Testing (Human)

Use `local_test_server.sh` for development and manual testing:

```bash
./local_test_server.sh                # Start on default port 2290
./local_test_server.sh -p 8080       # Use custom port
./local_test_server.sh -d            # Debug mode with request logs
```

The server runs in the background with status info shown. It runs from `test_run/` (so its databases live there, not in `game/`) and reads assets from `game/` via `--content`. Kill it with:
```bash
kill $(cat test_run/.local_test_server.pid)
```

### Automated Testing (Agent)

Use `agent_test_server.sh` for CI/CD and automated agent testing:

```bash
./agent_test_server.sh -N 100        # Process 100 requests then exit
./agent_test_server.sh -M 30         # Run for 30 seconds then exit
./agent_test_server.sh -N 10 -M 60   # Exit on first limit reached
./agent_test_server.sh -N 5 -l log.txt --keep-db
```

**Agent Test Options:**
| Option | Description |
|--------|-------------|
| `-N N` | Exit after N requests (must be >= 1) |
| `-M M` | Exit after M seconds (must be >= 1) |
| `-p PORT` | Bind to port 3290 (default) or custom |
| `-l FILE` | Write logs to file |
| `-q` | Quiet mode |
| `-v` | Verbose mode |
| `--keep-db` | Keep `test_run/agent/` directory after exit |

**Exit Codes:**
- `0` - Success
- `1` - Build failure
- `2` - Server startup failed

**Behavior:**
- Runs from `test_run/agent/` (databases live there; wiped and recreated each run) and reads assets from `game/` via `--content`
- Runs on port 3290 (or custom with `-p`)
- Exits immediately when limit reached (cuts off in-flight requests)
- Displays summary: total requests, time elapsed

## Images

Game images are stored in `server/images/` and are not committed to version control (see `.gitignore`).

### Directory Structure

```
server/images/
    combatants/
        {combatant_id}/
            idle/1.png, 2.png, ...
            attack/1.png, 2.png, ...
            defend/1.png, 2.png, ...
            die/1.png, 2.png, ...
    buildings/
        {building_id}/
            construction/1.png, 2.png, ...
            idle/1.png, 2.png, ...
            harvest/1.png, 2.png, ...        # optional
    heroes/
        {hero_id}/
            idle/1.png, 2.png, ...
            attack/1.png, 2.png, ...
            {skill_id}/1.png, 2.png, ...      # icon frames
                activate/1.png, 2.png, ...    # optional animation
    portraits/
        {portrait_id}/
            1.png, 2.png, ...                 # required
```

**Directory Pattern:** `images/[type]/[id]/[subtype]/[number].[extension]`

- **Types:** `combatants`, `buildings`, `heroes`, `portraits`
- **IDs:** Match config file IDs (portraits use integer IDs)
- **Subtypes:**
  - `combatants`: idle, attack, defend, die (all required)
  - `buildings`: construction, idle (required), harvest (optional)
  - `heroes`: idle, attack (required), `{skill_id}/` (per skill, required icon frames)
  - `{skill_id}/activate/` (optional animation subdirectory)
  - `portraits`: no subtype, files directly under `{portrait_id}/` (required, non-empty)
- **Files:** Numeric filenames (1, 2, 3...) with extensions: `.png`, `.jpg`, `.jpeg`, `.gif`, `.svg`, `.webp`

### Server Auto-Detection

Server walks `server/images/` at startup to detect available images. No filenames specified in config files.

### Linter Validation

`check_configs.py` validates images directory after successful config parsing:

- **Errors:** Empty required directories
- **Warnings:** Empty optional directories, orphaned files/dirs, missing required directories
- `--no-warnings` suppresses warnings
- Skips validation if `server/images/` doesn't exist

Run validation:
```bash
./tools/check_configs.py              # Show errors and warnings
./tools/check_configs.py --no-warnings  # Errors only
```

Copyright Christopher T. Lansdown, 2026 