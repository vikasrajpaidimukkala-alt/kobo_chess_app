#!/bin/sh
# Run make inside pgaskin's NickelTC image, the same way NickelMenu is built.
# Fedora: podman + SELinux needs the :z volume option.

set -eu

IMAGE="${NICKELTC_IMAGE:-ghcr.io/pgaskin/nickeltc:1.0}"
ROOT="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)"

if command -v podman >/dev/null 2>&1; then
    ENGINE=podman
    VOLUME="${ROOT}:${ROOT}:z"
elif command -v docker >/dev/null 2>&1; then
    ENGINE=docker
    VOLUME="${ROOT}:${ROOT}"
else
    echo "Need podman or docker to run ${IMAGE}" >&2
    echo "Or extract NickelTC and: make CROSS_COMPILE=/path/to/bin/arm-nickel-linux-gnueabihf-" >&2
    exit 1
fi

if [ "$#" -eq 0 ]; then
    set -- all
fi

exec "${ENGINE}" run \
    --rm -it \
    --volume="${VOLUME}" \
    --user="$(id -u):$(id -g)" \
    --workdir="${ROOT}" \
    --env=HOME \
    --env=CROSS_TC=arm-nickel-linux-gnueabihf \
    --entrypoint=make \
    "${IMAGE}" \
    "$@"
