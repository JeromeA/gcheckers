#!/usr/bin/env bash
set -euo pipefail

broadway_display="${BROADWAY_DISPLAY_NUM:-6}"
broadway_port="${BROADWAY_PORT:-8086}"
gdk_backend="${GDK_BACKEND:-broadway}"
gsk_renderer="${GSK_RENDERER:-cairo}"
log_file="${1:-/tmp/gcheckers-inconsistency.log}"

if [[ ! -x ./gcheckers ]]; then
  echo "gcheckers binary not found. Run 'make gcheckers' first." >&2
  exit 1
fi

if ! command -v gtk4-broadwayd >/dev/null 2>&1; then
  echo "gtk4-broadwayd is required to run inconsistency automation test." >&2
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

: >"$log_file"
gtk4-broadwayd --port "$broadway_port" ":${broadway_display}" >/dev/null 2>&1 &
broadway_pid=$!

export GDK_BACKEND="$gdk_backend"
export GSK_RENDERER="$gsk_renderer"
export BROADWAY_DISPLAY=":${broadway_display}"
export G_MESSAGES_DEBUG=all

./gcheckers --auto-force-moves=5 --exit-after-seconds=2 >"$log_file" 2>&1

if ! rg -q "GTK SCROLLEDWINDOW INCONSISTENCY" "$log_file"; then
  echo "Expected GtkScrolledWindow inconsistency diagnostic was not hit." >&2
  echo "Captured output:" >&2
  cat "$log_file" >&2
  exit 1
fi

echo "Inconsistency diagnostic hit as expected."
