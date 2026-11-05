# syntax=docker/dockerfile:1

# Production image. Build context is `server/`:
#   docker build -t ravenest-server:dev server/
#
# The runtime image contains only the compiled binaries and their shared
# libraries. Game content is mounted at /app/game (read-only) and uploads live
# at /app/uploads.

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libargon2-dev \
    libcurl4-openssl-dev \
    libpq-dev \
    libssl-dev \
    pkg-config \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/server
COPY . .

RUN cmake -S /src/server -B /src/server/build \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/server/build --parallel "$(nproc)"


FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# t64 package names are the Ubuntu 24.04 time_t-transition variants.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libargon2-1 \
    libcurl4t64 \
    libpq5 \
    libssl3t64 \
    tini \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Fixed uid/gid so a bind-mounted uploads directory can be chowned predictably.
RUN groupadd --system --gid 10001 ravenest \
    && useradd --system --uid 10001 --gid ravenest \
        --home-dir /app --no-create-home --shell /usr/sbin/nologin ravenest \
    && mkdir -p /app/uploads \
    && chown -R ravenest:ravenest /app

WORKDIR /app

COPY --from=builder /src/server/build/server /app/server
COPY --from=builder /src/server/build/invite_admin /app/invite_admin
COPY --from=builder /src/server/build/staff_admin /app/staff_admin

RUN chmod 0755 /app/server /app/invite_admin /app/staff_admin

USER ravenest

EXPOSE 2290

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/app/server", "--content", "/app/game"]
