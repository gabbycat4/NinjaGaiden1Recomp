#!/usr/bin/env bash
# Copies this project's licence and the licences of everything compiled into
# the game (engine, menu, SDL2, ImGui, ...) into one folder for a release.
#   bash tools/release/collect_licenses.sh <dest>
set -euo pipefail
dest="$1"
mkdir -p "$dest"
cp LICENSE "$dest/NinjaGaiden1Recomp-LICENSE.txt"
cp NOTICE "$dest/NinjaGaiden1Recomp-NOTICE.txt"
find nesrecomp recomp-ui \( -name .git -o -name tests -o -name test_data \) -prune -o \
     -type f \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'OFL*' -o -name PERMISSIONS.md \) -print |
while read -r f; do
    cp "$f" "$dest/$(printf '%s' "$f" | tr '/' '-')"
done
