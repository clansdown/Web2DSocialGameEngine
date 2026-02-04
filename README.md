# Web2DSocialGameEngine
A game engine for Social 2D (Sprite-based) Web games. Client + Server.

## Prerequisites

Before running the game engine, clone SimpleGame into the SimpleGame/ directory:

```bash
git clone https://github.com/clansdown/SimpleGame.git SimpleGame
```

The SimpleGame repository is required for this project to function correctly.

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

Copyright Christopher T. Lansdown, 2026 