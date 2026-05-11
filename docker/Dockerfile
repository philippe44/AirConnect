# syntax=docker/dockerfile:1
#
# Build stage: compile AirConnect from source for the target architecture.
# Requires submodules to be initialized: git submodule update --init --recursive
#
FROM debian:bookworm-slim AS builder

ARG TARGETARCH

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Map Docker architecture names to Makefile PLATFORM values
RUN case "${TARGETARCH}" in \
        amd64) PLATFORM=x86_64  ;; \
        arm64) PLATFORM=aarch64 ;; \
        *) echo "Unsupported architecture: ${TARGETARCH}" && exit 1 ;; \
    esac && \
    make -C airupnp CC=gcc HOST=linux PLATFORM=${PLATFORM} -j$(nproc) && \
    make -C aircast CC=gcc HOST=linux PLATFORM=${PLATFORM} -j$(nproc) && \
    # Copy static binaries to a known path for the runtime stage
    cp bin/airupnp-linux-${PLATFORM}-static /airupnp && \
    cp bin/aircast-linux-${PLATFORM}-static /aircast && \
    chmod +x /airupnp /aircast

#
# Runtime stage: minimal image with just the static binaries.
#
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /airupnp /usr/local/bin/airupnp
COPY --from=builder /aircast /usr/local/bin/aircast
COPY docker/docker-entrypoint.sh /docker-entrypoint.sh

RUN chmod +x /docker-entrypoint.sh

# AIRCONNECT_MODE: aircast | airupnp | both (default)
ENV AIRCONNECT_MODE=both

# Additional flags passed to the selected binary/binaries
ENV AIRUPNP_ARGS="-Z -l 1000:2000"
ENV AIRCAST_ARGS="-Z"

ENTRYPOINT ["/docker-entrypoint.sh"]
