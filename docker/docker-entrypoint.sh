#!/bin/sh
set -e

# Allow overriding args from the environment; defaults match upstream behaviour.
AIRUPNP_ARGS="${AIRUPNP_ARGS:--Z -l 1000:2000}"
AIRCAST_ARGS="${AIRCAST_ARGS:--Z}"

run_both() {
    /usr/local/bin/airupnp ${AIRUPNP_ARGS} &
    AIRUPNP_PID=$!
    trap 'kill "$AIRUPNP_PID" 2>/dev/null; wait "$AIRUPNP_PID" 2>/dev/null' EXIT INT TERM
    exec /usr/local/bin/aircast ${AIRCAST_ARGS}
}

case "${AIRCONNECT_MODE:-both}" in
    aircast)  exec /usr/local/bin/aircast  ${AIRCAST_ARGS}  "$@" ;;
    airupnp)  exec /usr/local/bin/airupnp  ${AIRUPNP_ARGS}  "$@" ;;
    both)     run_both ;;
    *)
        echo "Unknown AIRCONNECT_MODE='${AIRCONNECT_MODE}'. Use: aircast | airupnp | both" >&2
        exit 1
        ;;
esac
