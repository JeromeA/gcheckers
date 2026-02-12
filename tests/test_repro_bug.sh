#!/usr/bin/env bash
set -euo pipefail

BROADWAYD_BIN="${BROADWAYD_BIN:-gtk4-broadwayd}"
BROADWAY_TEST_PORT="${BROADWAY_TEST_PORT:-8120}"
BROADWAY_TEST_DISPLAY="${BROADWAY_TEST_DISPLAY:-40}"
BROADWAY_STARTUP_DELAY="${BROADWAY_STARTUP_DELAY:-0.3}"
XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-runtime}"
BROADWAY_TEST_LOG="${BROADWAY_TEST_LOG:-/tmp/broadwayd-${BROADWAY_TEST_PORT}.log}"

if ! command -v "$BROADWAYD_BIN" >/dev/null 2>&1; then
  echo "Skipping bug reproduction test: ${BROADWAYD_BIN} not available."
  exit 0
fi

mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

broadway_pid=""
cleanup() {
  if [ -n "$broadway_pid" ]; then
    kill "$broadway_pid" >/dev/null 2>&1 || true
    wait "$broadway_pid" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" "$BROADWAYD_BIN" --port "$BROADWAY_TEST_PORT" \
  ":$BROADWAY_TEST_DISPLAY" >"$BROADWAY_TEST_LOG" 2>&1 &
broadway_pid=$!
sleep "$BROADWAY_STARTUP_DELAY"

log_file="$(mktemp)"
XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" GDK_BACKEND=broadway BROADWAY_DISPLAY=":$BROADWAY_TEST_DISPLAY" \
  G_MESSAGES_DEBUG=all ./gcheckers >"$log_file" 2>&1

line="$(grep -m1 "GTK SCROLLEDWINDOW INCONSISTENCY" "$log_file" || true)"
if [ -z "$line" ]; then
  echo "Expected GTK SCROLLEDWINDOW INCONSISTENCY in logs but did not find it."
  exit 1
fi

echo "$line"
rm -f "$log_file"
