#!/usr/bin/env bash
set -euo pipefail

output_path="${1:-gcheckers.png}"
broadway_display="${BROADWAY_DISPLAY_NUM:-5}"
broadway_port="${BROADWAY_PORT:-8085}"
screen_size="${SCREEN_SIZE:-1280x720}"
gsk_renderer="${GSK_RENDERER:-cairo}"
gdk_backend="${GDK_BACKEND:-broadway}"
chromium_bin="${CHROMIUM_BIN:-google-chrome}"

if ! command -v gtk4-broadwayd >/dev/null 2>&1; then
  echo "gtk4-broadwayd is required to capture screenshots." >&2
  exit 1
fi

if ! command -v "$chromium_bin" >/dev/null 2>&1; then
  echo "Chrome is required to capture screenshots." >&2
  exit 1
fi

if [[ ! -x ./gcheckers ]]; then
  echo "gcheckers binary not found. Run 'make gcheckers' first." >&2
  exit 1
fi

broadway_pid=""
app_pid=""

cleanup() {
  if [[ -n "$app_pid" ]]; then
    kill "$app_pid" >/dev/null 2>&1 || true
    wait "$app_pid" >/dev/null 2>&1 || true
  fi
  if [[ -n "$broadway_pid" ]]; then
    kill "$broadway_pid" >/dev/null 2>&1 || true
    wait "$broadway_pid" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

gtk4-broadwayd --port "$broadway_port" ":${broadway_display}" >/dev/null 2>&1 &
broadway_pid=$!
export GDK_BACKEND="$gdk_backend"
export GSK_RENDERER="$gsk_renderer"
export BROADWAY_DISPLAY=":${broadway_display}"

IFS='x' read -r screen_width screen_height <<<"$screen_size"
if [[ -z "$screen_width" || -z "$screen_height" ]]; then
  echo "SCREEN_SIZE must be in WIDTHxHEIGHT format (got '$screen_size')." >&2
  exit 1
fi

sleep 0.2
./gcheckers >/dev/null 2>&1 &
app_pid=$!

sleep 1
"$chromium_bin" --headless --disable-gpu --no-sandbox --window-size="${screen_width},${screen_height}" \
  --screenshot="$output_path" "http://127.0.0.1:${broadway_port}/"
