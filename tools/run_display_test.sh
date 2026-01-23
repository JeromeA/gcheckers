#!/usr/bin/env bash
set -euo pipefail

broadway_display="${BROADWAY_DISPLAY_NUM:-5}"
broadway_port="${BROADWAY_PORT:-8085}"
gdk_backend="${GDK_BACKEND:-broadway}"
test_binary="${1:-}"

if [[ -z "$test_binary" ]]; then
  echo "Usage: $0 <test-binary> [args...]" >&2
  exit 2
fi

if ! command -v gtk4-broadwayd >/dev/null 2>&1; then
  echo "gtk4-broadwayd is required to run display-dependent tests." >&2
  exit 1
fi

if [[ ! -x "$test_binary" ]]; then
  echo "Test binary not found or not executable: $test_binary" >&2
  exit 1
fi

broadway_pid=""

cleanup() {
  if [[ -n "$broadway_pid" ]]; then
    kill "$broadway_pid" >/dev/null 2>&1 || true
    wait "$broadway_pid" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

gtk4-broadwayd --port "$broadway_port" ":${broadway_display}" >/dev/null 2>&1 &
broadway_pid=$!

export GDK_BACKEND="$gdk_backend"
export BROADWAY_DISPLAY=":${broadway_display}"

sleep 0.2
shift
exec "$test_binary" "$@"
