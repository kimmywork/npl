#!/bin/bash
# Run the repository gate in an aarch64/x86_64 Linux container (local colima
# VM by default). Copies tracked and untracked-but-not-ignored files only, so
# host build products cannot leak in.
#
# Usage: tools/check_linux.sh [run_tests.sh options...]
#   DOCKER_CONTEXT (default colima), NPL_LINUX_IMAGE (default npl-linux:bookworm)
#   NPL_GC_THRESHOLD_BYTES (default 134217728; the default 2 GiB VM cannot hold
#   self-compilation at the 1 GiB native default)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONTEXT=${DOCKER_CONTEXT:-colima}
IMAGE=${NPL_LINUX_IMAGE:-npl-linux:bookworm}
THRESHOLD=${NPL_GC_THRESHOLD_BYTES:-134217728}
docker() { command docker --context "$CONTEXT" "$@"; }

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    # Host proxies on 127.0.0.1 are unreachable from the VM; build without them.
    docker build -q --build-arg http_proxy= --build-arg https_proxy= \
        --build-arg HTTP_PROXY= --build-arg HTTPS_PROXY= \
        -t "$IMAGE" "$ROOT/tools/linux" >/dev/null
fi

COPY="$ROOT/build/linux/src"
rm -rf "$COPY"
mkdir -p "$COPY"
(cd "$ROOT" && { git ls-files -z; git ls-files -z --others --exclude-standard; } \
    | rsync -a --from0 --files-from=- ./ "$COPY/")

docker run --rm -e NPL_GC_THRESHOLD_BYTES="$THRESHOLD" -e CC=clang \
    -v "$COPY:/src" "$IMAGE" tools/run_tests.sh "$@"
