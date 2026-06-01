FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libmysqlcppconn-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    g++ \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy built binary
COPY --from=builder /app/build/vibeoj-server /app/vibeoj-server

# Copy MySQL connector shared library from builder (guaranteed version match)
COPY --from=builder /usr/lib/x86_64-linux-gnu/libmysqlcppconn.so* /usr/lib/x86_64-linux-gnu/

# Copy seed data (static files are served by nginx, not needed here)
COPY --from=builder /app/data /app/data

RUN mkdir -p /tmp/judge

EXPOSE 8080

CMD ["./vibeoj-server"]
