#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
if [ -f "$script_dir/Makefile" ]; then
  repo_root=$script_dir
elif [ -f "$script_dir/../Makefile" ]; then
  repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
else
  echo "Unable to locate repository root from $0" >&2
  exit 1
fi

manifest="$repo_root/io.github.JeromeA.gcheckers.yaml"

grep -q "^id: io.github.JeromeA.gcheckers$" "$manifest"
grep -q "^runtime: org.gnome.Platform$" "$manifest"
grep -q "^runtime-version: '48'$" "$manifest"
grep -q "^sdk: org.gnome.Sdk$" "$manifest"
grep -q "^command: gcheckers$" "$manifest"
grep -q "make install PREFIX=/app" "$manifest"
grep -q -- "--share=network" "$manifest"
grep -q -- "--socket=wayland" "$manifest"
grep -q -- "--socket=fallback-x11" "$manifest"
grep -q -- "--device=dri" "$manifest"
grep -q "type: dir" "$manifest"
grep -q "path: \\." "$manifest"
