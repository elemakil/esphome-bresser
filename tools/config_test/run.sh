#!/usr/bin/env bash
# Assert ESPHome accepts the good fixtures and rejects the bad ones for the right reason.
#
# Each fixture under fixtures/ is a YAML *fragment*: just the sensor:/binary_sensor:/
# text_sensor: blocks under test (see common.yaml for why that's enough). For every fixture
# this script concatenates common.yaml + the fixture into work/config-test.yaml inside a
# scratch config directory that also holds a fresh copy of components/, then runs
#   esphome config work/config-test.yaml
# Needs `esphome` on PATH (pip install esphome==2026.10.1, or run inside
# ghcr.io/esphome/esphome:2026.10.1).
#
# bad_*.yaml's first line is `# expect: <substring>`: the runner asserts the config is
# REJECTED *and* that the failure output contains that substring, so a fixture can't pass by
# being rejected for an unrelated reason (e.g. a typo'd field name).
#
# Works from any cwd: every path below is resolved from this script's own location.
set -uo pipefail

if ! command -v esphome >/dev/null 2>&1; then
    echo "error: esphome not found on PATH." >&2
    echo "       Install esphome==2026.10.1 (pip) or run inside ghcr.io/esphome/esphome:2026.10.1." >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WORK_DIR="$SCRIPT_DIR/work"
CONFIG_NAME="config-test.yaml"

fail=0

# Fresh scratch dir every run, so a fixture from a previous run (or a stale components/
# copy) can't leak into this one.
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cp -r "$REPO_ROOT/components" "$WORK_DIR/components"

# A fixture normally states only the entity blocks it tests and inherits common.yaml. Some
# cases have to vary the hub or the radio itself, which cannot be expressed by appending to a
# common file that already has one. Such a fixture may ship a sibling <name>.common.yaml,
# which is used in its place.
run_one() {
    local fixture="$1"
    local base="$SCRIPT_DIR/common.yaml"
    local override="${fixture%.yaml}.common.yaml"
    [ -f "$override" ] && base="$override"
    cat "$base" "$fixture" >"$WORK_DIR/$CONFIG_NAME"
    esphome config "$WORK_DIR/$CONFIG_NAME"
}

shopt -s nullglob

for f in "$SCRIPT_DIR"/fixtures/good_*.yaml; do
    case "$f" in *.common.yaml) continue ;; esac
    if out=$(run_one "$f" 2>&1); then
        echo "  ok    $(basename "$f")"
    else
        echo "  FAIL  $(basename "$f") should be valid"
        # shellcheck disable=SC2001  # ${var//a/b} cannot indent the first line as well.
        echo "$out" | sed 's/^/        /'
        fail=1
    fi
done

for f in "$SCRIPT_DIR"/fixtures/bad_*.yaml; do
    case "$f" in *.common.yaml) continue ;; esac
    expect="$(head -1 "$f" | sed 's/^# expect: //')"
    out=$(run_one "$f" 2>&1)
    code=$?
    if [ "$code" -eq 0 ]; then
        echo "  FAIL  $(basename "$f") should be rejected"
        fail=1
    elif echo "$out" | grep -qF "$expect"; then
        echo "  ok    $(basename "$f") rejected: $expect"
    else
        echo "  FAIL  $(basename "$f") rejected for the wrong reason (wanted: $expect)"
        # shellcheck disable=SC2001  # ${var//a/b} cannot indent the first line as well.
        echo "$out" | sed 's/^/        /'
        fail=1
    fi
done

exit $fail
