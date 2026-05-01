#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
if [ -f "$script_dir/Makefile" ]; then
  repo_root=$script_dir
elif [ -f "$script_dir/../Makefile" ]; then
  repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
elif [ -f "$script_dir/../../Makefile" ]; then
  repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
else
  echo "Unable to locate repository root from $0" >&2
  exit 1
fi

checkers_manifest="$repo_root/flatpak/io.github.jeromea.gcheckers.yaml"
boop_manifest="$repo_root/flatpak/io.github.jeromea.gboop.yaml"

grep -q "^id: io.github.jeromea.gcheckers$" "$checkers_manifest"
grep -q "^runtime: org.gnome.Platform$" "$checkers_manifest"
grep -q "^runtime-version: '50'$" "$checkers_manifest"
grep -q "^sdk: org.gnome.Sdk$" "$checkers_manifest"
grep -q "^command: gcheckers$" "$checkers_manifest"
grep -q "make install-checkers install-schemas PREFIX=/app" "$checkers_manifest"
grep -q -- "--share=network" "$checkers_manifest"
grep -q -- "--socket=wayland" "$checkers_manifest"
grep -q -- "--socket=fallback-x11" "$checkers_manifest"
grep -q -- "--device=dri" "$checkers_manifest"
grep -q "type: dir" "$checkers_manifest"
grep -q "path: \\." "$checkers_manifest"

grep -q "^id: io.github.jeromea.gboop$" "$boop_manifest"
grep -q "^runtime: org.gnome.Platform$" "$boop_manifest"
grep -q "^runtime-version: '50'$" "$boop_manifest"
grep -q "^sdk: org.gnome.Sdk$" "$boop_manifest"
grep -q "^command: gboop$" "$boop_manifest"
grep -q "make install-boop install-schemas PREFIX=/app" "$boop_manifest"
grep -q -- "--socket=wayland" "$boop_manifest"
grep -q -- "--socket=fallback-x11" "$boop_manifest"
grep -q -- "--device=dri" "$boop_manifest"
grep -q "type: dir" "$boop_manifest"
grep -q "path: \\." "$boop_manifest"
