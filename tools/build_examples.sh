#!/usr/bin/env bash
# Validate or compile the configs in examples/ against THIS checkout's component.
#
#   tools/build_examples.sh config                 every example
#   tools/build_examples.sh compile                every example
#   tools/build_examples.sh compile examples/esp32-s3-cc1101.yaml
#
# The examples are written for users, so they load the component from GitHub. Building them
# verbatim would test whatever is on main, not the change under review. So for each example
# this assembles a scratch directory under build/examples/<name>/ holding:
#
#   <name>.yaml    the example, with its external_components source rewritten to the local
#                  copy below, and its device name set to <name> so the two examples never
#                  share an ESPHome build directory
#   components/    a fresh copy of this checkout's components/
#   secrets.yaml   throwaway values from tools/ci_secrets.sh. Nothing built here may be
#                  flashed.
#
# then runs `esphome <config|compile>` on it. Needs `esphome` on PATH (pip install
# esphome==2026.10.1, or run inside ghcr.io/esphome/esphome:2026.10.1). The scratch directory's .esphome/ is
# kept between runs, so a repeated compile is incremental.
#
# Works from any cwd: every path is resolved from this script's own location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WORK_ROOT="$REPO_ROOT/build/examples"

# The exact source line every example must carry. If an example stops matching, the rewrite
# below would silently leave it pointing at GitHub -- so a miss is an error, not a skip.
REMOTE_SOURCE="source: github://elemakil/esphome-bresser@main"
LOCAL_SOURCE="source: {type: local, path: components}"

usage() {
    echo "usage: tools/build_examples.sh <config|compile> [examples/<name>.yaml ...]" >&2
    exit 2
}

sub="${1:-}"
case "$sub" in
    config | compile) shift ;;
    *) usage ;;
esac

if ! command -v esphome >/dev/null 2>&1; then
    echo "error: esphome not found on PATH." >&2
    echo "       Install esphome==2026.10.1 (pip) or run inside ghcr.io/esphome/esphome:2026.10.1." >&2
    exit 2
fi

if [ $# -eq 0 ]; then
    set -- "$REPO_ROOT"/examples/*.yaml
fi

fail=0
for example in "$@"; do
    [ -f "$example" ] || {
        echo "error: no such example: $example" >&2
        exit 2
    }
    slug="$(basename "$example" .yaml)"
    work="$WORK_ROOT/$slug"
    mkdir -p "$work"

    # Refresh everything but .esphome/.
    rm -rf "$work/components" "$work/secrets.yaml" "$work/$slug.yaml"
    cp -r "$REPO_ROOT/components" "$work/components"

    grep -qF "$REMOTE_SOURCE" "$example" || {
        echo "error: $example does not contain '$REMOTE_SOURCE'," >&2
        echo "       so it cannot be pointed at this checkout. Update REMOTE_SOURCE in $0." >&2
        exit 1
    }
    sed -e "s#$REMOTE_SOURCE#$LOCAL_SOURCE#" \
        -e "s#^\(  name: \)bresser-receiver\$#\1$slug#" \
        "$example" >"$work/$slug.yaml"

    FORCE_CI_SECRETS=1 "$SCRIPT_DIR/ci_secrets.sh" "$work" >/dev/null

    echo "=== $slug"
    if ! esphome "$sub" "$work/$slug.yaml"; then
        echo "FAIL  $slug"
        fail=1
    fi
done

exit "$fail"
