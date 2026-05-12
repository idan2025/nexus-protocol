# syntax=docker/dockerfile:1.7
#
# NEXUS Pillar Server image.
#
# Two-stage build: compile pillard from source against the libnexus
# static library, then run on a slim Debian base. No network port
# is bound by Docker until you `-p` map it; the pillar inside the
# container listens on 4242/tcp.
#
# Build + push (from this repo root):
#   docker buildx build \
#     --platform linux/amd64 \
#     -t idan2025/nexus-pillar:latest \
#     -t idan2025/nexus-pillar:0.6.8 \
#     --push .
#
# Run:
#   docker run -d --name nexus-pillar \
#     -p 4242:4242 \
#     -e PILLAR_NAME="Tel-Aviv-Pillar" \
#     -v nexus-pillar-data:/var/lib/nexus \
#     idan2025/nexus-pillar:latest
#
# PILLAR_NAME (optional, 32 chars max): friendly display name. Phones
# that connect get a NICKNAME NXM with this string so they show
# "Tel-Aviv-Pillar" instead of the pillar's 4-byte address. Leave unset
# to keep the pillar visually anonymous on connecting devices.
#
# The mounted volume holds the persistent identity at
# /var/lib/nexus/pillar.identity so the pillar's short address is
# stable across restarts.

# ---- Stage 1: build ----
FROM debian:bookworm-slim AS build
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake make g++ gcc python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN mkdir -p build \
 && cd build \
 && cmake .. -DCMAKE_BUILD_TYPE=Release \
 && make -j"$(nproc)" pillard \
 && strip app/pillard

# ---- Stage 2: runtime ----
FROM debian:bookworm-slim
ARG DEBIAN_FRONTEND=noninteractive

# Minimal runtime: pillard is statically linked against libnexus and
# only needs libc + libpthread, both already in debian-slim. ca-certs
# kept for any future TLS bootstrap; remove if you want a smaller image.
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates tini \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system --gid 1100 nexus \
 && useradd  --system --uid 1100 --gid nexus --home /var/lib/nexus \
             --shell /usr/sbin/nologin nexus \
 && install -d -o nexus -g nexus -m 0700 /var/lib/nexus

COPY --from=build /src/build/app/pillard /usr/local/bin/pillard

USER nexus
WORKDIR /var/lib/nexus
VOLUME ["/var/lib/nexus"]

EXPOSE 4242/tcp

# tini reaps zombies + forwards SIGTERM cleanly to pillard so that
# `docker stop` triggers the daemon's graceful shutdown path.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/pillard"]
CMD ["-f", "-p", "4242", "-i", "/var/lib/nexus/pillar.identity"]
