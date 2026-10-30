# Packages the pre-built AirConnect binaries from the release zip in this repo.
# Build (from repo root):
#   docker buildx build --platform linux/amd64,linux/arm64 \
#     --build-arg APP=airupnp -t <registry>/airupnp:1.12.4 --push .
#
# APP=airupnp (UPnP/Sonos) or APP=aircast (Chromecast)

ARG VERSION=1.12.4

FROM debian:bookworm-slim AS extract
ARG VERSION
ARG APP=airupnp
ARG TARGETARCH
ARG TARGETVARIANT
RUN apt-get update \
 && apt-get install -y --no-install-recommends unzip \
 && rm -rf /var/lib/apt/lists/*
COPY AirConnect-${VERSION}.zip /tmp/AirConnect.zip
RUN set -eux; \
    case "${TARGETARCH}${TARGETVARIANT}" in \
      amd64)   arch=x86_64 ;; \
      arm64)   arch=aarch64 ;; \
      armv7)   arch=arm ;; \
      armv6)   arch=armv6 ;; \
      386)     arch=x86 ;; \
      *) echo "unsupported platform ${TARGETARCH}${TARGETVARIANT}" >&2; exit 1 ;; \
    esac; \
    unzip -j /tmp/AirConnect.zip "${APP}-linux-${arch}" -d /out; \
    mv "/out/${APP}-linux-${arch}" /out/airconnect; \
    chmod 0755 /out/airconnect

FROM debian:bookworm-slim
# libssl3 is dlopen()ed at runtime by the bridge for the AirPlay (RAOP) crypto.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libssl3 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --uid 10001 --create-home --home-dir /config airconnect
COPY --from=extract /out/airconnect /usr/local/bin/airconnect

USER 10001:10001
WORKDIR /config
VOLUME ["/config"]

# -Z: no interactive prompt (never use -z, it daemonizes and the container exits)
# -k: exit immediately on SIGTERM so pod termination is fast
ENTRYPOINT ["/usr/local/bin/airconnect"]
CMD ["-Z", "-k", "-x", "/config/config.xml"]
