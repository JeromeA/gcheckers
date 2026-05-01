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
checkers_app_id="io.github.jeromea.gcheckers"
boop_app_id="io.github.jeromea.gboop"
checkers_desktop_file="$repo_root/data/$checkers_app_id.desktop"
checkers_metainfo_file="$repo_root/data/$checkers_app_id.metainfo.xml"
checkers_icon_file="$repo_root/data/icons/hicolor/scalable/apps/$checkers_app_id.svg"
checkers_schema_file="$repo_root/data/schemas/$checkers_app_id.gschema.xml"
boop_desktop_file="$repo_root/data/$boop_app_id.desktop"
boop_metainfo_file="$repo_root/data/$boop_app_id.metainfo.xml"
boop_icon_file="$repo_root/data/icons/hicolor/scalable/apps/$boop_app_id.svg"
boop_schema_file="$repo_root/data/schemas/$boop_app_id.gschema.xml"
install_root=$(mktemp -d "${TMPDIR:-/tmp}/gcheckers-install.XXXXXX")
trap 'rm -rf "$install_root"' EXIT HUP INT TERM

grep -q "^Icon=$checkers_app_id\$" "$checkers_desktop_file"
grep -q "^Exec=gcheckers\$" "$checkers_desktop_file"
grep -q "<id>$checkers_app_id</id>" "$checkers_metainfo_file"
grep -q "<launchable type=\"desktop-id\">$checkers_app_id.desktop</launchable>" "$checkers_metainfo_file"
grep -q "<project_license>GPL-3.0-only</project_license>" "$checkers_metainfo_file"
grep -q "<release version=\"0.1.0\" date=\"2026-04-15\">" "$checkers_metainfo_file"
grep -q "<p>Initial public release.</p>" "$checkers_metainfo_file"
grep -q "raw.githubusercontent.com/JeromeA/gcheckers/v0.1.0/" "$checkers_metainfo_file"
grep -q "/doc/Puzzle.png" "$checkers_metainfo_file"
if grep -q "raw.githubusercontent.com/JeromeA/gcheckers/main/" "$checkers_metainfo_file"; then
  echo "Metainfo screenshots must use immutable commit or tag URLs, not the main branch." >&2
  exit 1
fi
grep -q "<schema id=\"$checkers_app_id\" path=\"/io/github/jeromea/gcheckers/\">" "$checkers_schema_file"
test -f "$checkers_icon_file"

grep -q "^Icon=$boop_app_id\$" "$boop_desktop_file"
grep -q "^Exec=gboop\$" "$boop_desktop_file"
grep -q "<id>$boop_app_id</id>" "$boop_metainfo_file"
grep -q "<launchable type=\"desktop-id\">$boop_app_id.desktop</launchable>" "$boop_metainfo_file"
grep -q "<project_license>GPL-3.0-only</project_license>" "$boop_metainfo_file"
grep -q "<binary>gboop</binary>" "$boop_metainfo_file"
if grep -q "build-time game selection" "$boop_metainfo_file"; then
  echo "Boop metainfo still describes compile-time game selection." >&2
  exit 1
fi
grep -q "<schema id=\"$boop_app_id\" path=\"/io/github/jeromea/gboop/\">" "$boop_schema_file"
test -f "$boop_icon_file"

make -C "$repo_root" install PREFIX="$install_root/prefix" >/dev/null

test -f "$install_root/prefix/bin/gcheckers"
test -f "$install_root/prefix/bin/gboop"
test -f "$install_root/prefix/bin/ghomeworlds"
test -f "$install_root/prefix/share/applications/$checkers_app_id.desktop"
test -f "$install_root/prefix/share/applications/$boop_app_id.desktop"
test -f "$install_root/prefix/share/metainfo/$checkers_app_id.metainfo.xml"
test -f "$install_root/prefix/share/metainfo/$boop_app_id.metainfo.xml"
test -f "$install_root/prefix/share/icons/hicolor/scalable/apps/$checkers_app_id.svg"
test -f "$install_root/prefix/share/icons/hicolor/scalable/apps/$boop_app_id.svg"
test -f "$install_root/prefix/share/glib-2.0/schemas/$checkers_app_id.gschema.xml"
test -f "$install_root/prefix/share/glib-2.0/schemas/$boop_app_id.gschema.xml"
test -f "$install_root/prefix/share/glib-2.0/schemas/gschemas.compiled"
