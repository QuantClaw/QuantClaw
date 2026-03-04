# QuantClaw Production Image — Multi-stage build
#
# Stages:
#   cpp-builder  — compiles C++ binary (Ubuntu 22.04)
#   node-builder — compiles Sidecar TS→JS (node:20-slim, official image)
#   runtime      — minimal Ubuntu 22.04 image with both artifacts
#
# Build:
#   docker build -f scripts/Dockerfile -t quantclaw:$(cat scripts/DOCKER_VERSION) .
# Run:
#   docker run -d --name quantclaw \
#     -p 18800:18800 -p 18801:18801 \
#     -v quantclaw_data:/home/quantclaw/.quantclaw \
#     quantclaw gateway run

ARG UBUNTU_VERSION=22.04

# ── Stage 1: C++ build ───────────────────────────────────────────────────────
FROM ubuntu:${UBUNTU_VERSION} AS cpp-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libssl-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF .. \
    && make -j$(nproc) \
    && strip build/quantclaw 2>/dev/null || true

# ── Stage 2: Sidecar (Node.js) build ─────────────────────────────────────────
# Use the official Node.js image — avoids nodesource installation issues and
# guarantees correct npm version for lockfileVersion 3.
FROM node:20-slim AS node-builder

# Copy package files first for better layer caching.
# Use explicit filenames (not glob) — some BuildKit versions mis-expand
# package*.json and skip package-lock.json, causing `npm ci` to fail.
WORKDIR /src/sidecar
COPY sidecar/package.json sidecar/package-lock.json ./
RUN npm ci

# Copy source and compile
COPY sidecar/src ./src
COPY sidecar/tsconfig.json ./
RUN npm run build \
    && npm prune --omit=dev

# ── Stage 3: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:${UBUNTU_VERSION} AS runtime

ARG VERSION
ARG BUILD_DATE
ARG VCS_REF

LABEL org.opencontainers.image.title="QuantClaw" \
      org.opencontainers.image.description="High-performance AI agent runtime (C++17)" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.created="${BUILD_DATE}" \
      org.opencontainers.image.revision="${VCS_REF}" \
      org.opencontainers.image.source="https://github.com/QuantClaw/QuantClaw" \
      org.opencontainers.image.licenses="MIT"

ENV DEBIAN_FRONTEND=noninteractive

# C++ runtime deps only — Node.js binary is copied from node-builder below
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libcurl4 \
    libspdlog1 \
    zlib1g \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user (mirrors ixwebsocket's adduser pattern)
RUN groupadd -r quantclaw && useradd -r -g quantclaw -m -d /home/quantclaw quantclaw

# Install C++ binary
COPY --from=cpp-builder --chown=root:root /src/build/quantclaw /usr/local/bin/quantclaw
RUN chmod +x /usr/local/bin/quantclaw \
    && ldd /usr/local/bin/quantclaw

# Copy Node.js binary from node-builder (avoids nodesource installation in runtime)
COPY --from=node-builder /usr/local/bin/node /usr/local/bin/node

# Install Sidecar (dist + pruned node_modules)
COPY --from=node-builder --chown=quantclaw:quantclaw /src/sidecar/dist /home/quantclaw/sidecar/dist
COPY --from=node-builder --chown=quantclaw:quantclaw /src/sidecar/node_modules /home/quantclaw/sidecar/node_modules

# Runtime workspace layout (OpenClaw-compatible: agents/main/)
RUN mkdir -p /home/quantclaw/.quantclaw/agents/main/workspace \
             /home/quantclaw/.quantclaw/agents/main/sessions \
             /home/quantclaw/.quantclaw/logs \
    && chown -R quantclaw:quantclaw /home/quantclaw/.quantclaw

VOLUME ["/home/quantclaw/.quantclaw"]

EXPOSE 18800 18801

USER quantclaw
WORKDIR /home/quantclaw

HEALTHCHECK --interval=30s --timeout=10s --start-period=10s --retries=3 \
    CMD quantclaw health || exit 1

ENTRYPOINT ["quantclaw"]
CMD ["gateway", "run"]
