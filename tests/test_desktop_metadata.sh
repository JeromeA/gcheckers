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
app_id="io.github.jeromea.gcheckers"
desktop_file="$repo_root/data/$app_id.desktop"
metainfo_file="$repo_root/data/$app_id.metainfo.xml"
icon_file="$repo_root/data/icons/hicolor/scalable/apps/$app_id.svg"
schema_file="$repo_root/data/schemas/$app_id.gschema.xml"
install_root=$(mktemp -d "${TMPDIR:-/tmp}/gcheckers-install.XXXXXX")
trap 'rm -rf "$install_root"' EXIT HUP INT TERM

grep -q "^Icon=$app_id\$" "$desktop_file"
grep -q "^Exec=gcheckers\$" "$desktop_file"
grep -q "<id>$app_id</id>" "$metainfo_file"
grep -q "<launchable type=\"desktop-id\">$app_id.desktop</launchable>" "$metainfo_file"
grep -q "<project_license>GPL-3.0-only</project_license>" "$metainfo_file"
grep -q "<release version=\"0.1.0\" date=\"2026-04-15\">" "$metainfo_file"
grep -q "<p>Initial public release.</p>" "$metainfo_file"
grep -q "raw.githubusercontent.com/JeromeA/gcheckers/v0.1.0/" "$metainfo_file"
grep -q "/doc/Puzzle.png" "$metainfo_file"
if grep -q "raw.githubusercontent.com/JeromeA/gcheckers/main/" "$metainfo_file"; then
  echo "Metainfo screenshots must use immutable commit or tag URLs, not the main branch." >&2
  exit 1
fi
grep -q "<schema id=\"$app_id\" path=\"/io/github/jeromea/gcheckers/\">" "$schema_file"
test -f "$icon_file"

make -C "$repo_root" install PREFIX="$install_root/prefix" >/dev/null

test -f "$install_root/prefix/bin/gcheckers"
test -f "$install_root/prefix/share/applications/$app_id.desktop"
test -f "$install_root/prefix/share/metainfo/$app_id.metainfo.xml"
test -f "$install_root/prefix/share/icons/hicolor/scalable/apps/$app_id.svg"
test -f "$install_root/prefix/share/glib-2.0/schemas/$app_id.gschema.xml"
test -f "$install_root/prefix/share/glib-2.0/schemas/gschemas.compiled"
