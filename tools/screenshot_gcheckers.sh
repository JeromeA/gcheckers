#!/usr/bin/env bash
set -euo pipefail

output_path="${1:-gcheckers.png}"
display_num="${DISPLAY_NUM:-99}"
screen_geometry="${SCREEN_GEOMETRY:-1280x720x24}"

if ! command -v Xvfb >/dev/null 2>&1; then
  echo "Xvfb is required to capture screenshots." >&2
  exit 1
fi

if ! command -v import >/dev/null 2>&1; then
  echo "ImageMagick 'import' is required to capture screenshots." >&2
  exit 1
fi

if [[ ! -x ./gcheckers ]]; then
  echo "gcheckers binary not found. Run 'make gcheckers' first." >&2
  exit 1
fi

xvfb_pid=""
app_pid=""

cleanup() {
  if [[ -n "$app_pid" ]]; then
    kill "$app_pid" >/dev/null 2>&1 || true
    wait "$app_pid" >/dev/null 2>&1 || true
  fi
  if [[ -n "$xvfb_pid" ]]; then
    kill "$xvfb_pid" >/dev/null 2>&1 || true
    wait "$xvfb_pid" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

Xvfb ":${display_num}" -screen 0 "$screen_geometry" >/dev/null 2>&1 &
xvfb_pid=$!
export DISPLAY=":${display_num}"

sleep 0.2
./gcheckers >/dev/null 2>&1 &
app_pid=$!

sleep 1
import -display "$DISPLAY" -window root "$output_path"
