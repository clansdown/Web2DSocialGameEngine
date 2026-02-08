# Web2DSocialGameEngine
A game engine for Social 2D (Sprite-based) Web games. Client + Server.

## Prerequisites

Before running the game engine, clone SimpleGame into the SimpleGame/ directory:

```bash
git clone https://github.com/clansdown/SimpleGame.git SimpleGame
```

The SimpleGame repository is required for this project to function correctly.

## Configuration Files & Docker Deployment

### Required Configuration Files

Create the following files in the root directory (these files are in `.gitignore`):

#### `.game_name`
Your game name used for Docker image naming.
```
Ravenest
```

#### `.docker_repo`
Your Docker registry repository URL for pushing images.
```
ghcr.io/yourusername/ravenest-server
```
Alternative formats:
- GitHub Container Registry: `ghcr.io/username/repo-name`
- Docker Hub: `registry.hub.docker.com/username/repo-name`
- Private registry: `registry.local:5000/repo-name`

#### `.docker_creds`
Docker registry credentials in format `username:token`.
```
myuser:ghp_xxxxxxxxxxxxxxxx
```

For registries:
- **GitHub Container Registry**: Create a PAT with `write:packages` permission
- **Docker Hub**: Create an Access Token (NOT your Docker Hub password)

### Docker Registry Setup

#### GitHub Container Registry
```bash
# Create PAT at: https://github.com/settings/tokens
# Required scope: write:packages
# Format: yourusername:ghp_token_here
```

#### Docker Hub
```bash
# Create Access Token at: https://hub.docker.com/settings/security
# Format: yourusername:your_access_token_here
```

#### Tagging Best Practices

- **Semantic versioning**: `v1.0.0`, `v1.0.1`, `v2.0.0`
- **Branch-based**: `dev`, `staging`, `main`
- **Date-based**: `2025-02-04`, `2025-02-04T19-09-14`

Example usage:
```bash
./build_release.sh v1.0.0      # Pushes to myregistry/ravenest-server:v1.0.0
./build_release.sh              # Pushes to myregistry/ravenest-server:20250204T190914
```

## Nginx Configuration

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

### Build and Push Release

```bash
./build_release.sh v1.0.0       # Custom tag
./build_release.sh              # Timestamp tag
```
Builds server, creates Docker image, tags with version, and pushes to registry using `.docker_repo` and `.docker_creds`.

## Local Testing

### Interactive Testing (Human)

Use `local_test_server.sh` for development and manual testing:

```bash
./local_test_server.sh                # Start on default port 2290
./local_test_server.sh -p 8080       # Use custom port
./local_test_server.sh -d            # Debug mode with request logs
```

The server runs in the background with status info shown. Uses databases in current directory (`game.db`, `messages.db`) - these are permanent and never cleaned up. Kill it with:
```bash
kill $(cat .local_test_server.pid)
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
| `--keep-db` | Keep `.agent_test_db/` directory after exit |

**Exit Codes:**
- `0` - Success
- `1` - Build failure
- `2` - Server startup failed

**Behavior:**
- Uses `.agent_test_db/` directory for databases (wiped and recreated each run)
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
```

**Directory Pattern:** `images/[type]/[id]/[subtype]/[number].[extension]`

- **Types:** `combatants`, `buildings`, `heroes`
- **IDs:** Match config file IDs
- **Subtypes:**
  - `combatants`: idle, attack, defend, die (all required)
  - `buildings`: construction, idle (required), harvest (optional)
  - `heroes`: idle, attack (required), `{skill_id}/` (per skill, required icon frames)
  - `{skill_id}/activate/` (optional animation subdirectory)
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