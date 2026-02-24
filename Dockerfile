# Multi-stage build for minimal image size
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Build (IXWebSocket fetched automatically via FetchContent)
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF .. && \
    make -j$(nproc)

# Runtime stage
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl3 \
    libcurl4 \
    libspdlog1 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -u 1000 quantclaw

# Set working directory
WORKDIR /app

# Copy built binary from builder stage
COPY --from=builder /app/build/quantclaw /usr/local/bin/quantclaw

# Create workspace directory (OpenClaw-compatible layout)
RUN mkdir -p /home/quantclaw/.quantclaw/agents/default/workspace && \
    mkdir -p /home/quantclaw/.quantclaw/agents/default/sessions && \
    mkdir -p /home/quantclaw/.quantclaw/logs && \
    chown -R quantclaw:quantclaw /home/quantclaw

# Set volumes for persistence
VOLUME ["/home/quantclaw/.quantclaw"]

# Expose ports
EXPOSE 18789

# Switch to non-root user
USER quantclaw

# Set working directory
WORKDIR /home/quantclaw

# Health check via gateway
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD quantclaw health || exit 1

# Default command: run gateway in foreground
ENTRYPOINT ["quantclaw"]
CMD ["gateway"]
