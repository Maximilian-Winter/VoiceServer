# Build stage
FROM debian:bookworm-slim AS builder

# Install dependencies including OpenSSL dev libraries
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev

WORKDIR /app

# Copy source files
COPY CMakeLists.txt .
COPY external ./external
COPY src ./src

# Build all servers
RUN mkdir build && cd build \
    && cmake .. \
    && cmake --build . --verbose

# Runtime stage
FROM debian:bookworm-slim

# Install runtime OpenSSL libraries
RUN apt-get update && apt-get install -y \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY voice_server_config.json /app/
COPY dh2048.pem /app/
# Copy built executables from builder stage
COPY --from=builder /app/build/bin/voice_server /app/

EXPOSE 12345 8080

RUN chmod +x /app/voice_server

ENTRYPOINT ["./voice_server", "voice_server_config.json"]