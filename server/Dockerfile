FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    git \
    perl \
    make \
    build-essential \
    libssl-dev \
    zlib1g-dev \
    libsqlite3-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/server

COPY server/ .

RUN mkdir -p build && cd build && \
    cmake .. && make

EXPOSE 2290

CMD ["/app/server/build/server"]