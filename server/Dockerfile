FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    git \
    perl \
    make \
    build-essential \
    libssl-dev \
    libpq-dev \
    libargon2-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/server

COPY server/ .

RUN mkdir -p build && cd build && \
    cmake .. && make

EXPOSE 2290

CMD ["/app/server/build/server"]