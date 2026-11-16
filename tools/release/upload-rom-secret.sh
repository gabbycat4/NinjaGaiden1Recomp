#!/usr/bin/env bash
# Stores your Ninja Gaiden ROM as encrypted GitHub Actions secrets, so the
# release workflow can build the game on GitHub's machines. Run it once (again
# only if you change ROM dumps). Secrets are never shown in logs or to visitors
# of the repository, and the ROM is never part of a download.
#
#   bash tools/release/upload-rom-secret.sh [rom.nes] [owner/repo]
#
# A secret holds at most 48 KB, so the ROM is compressed, base64-encoded and
# split into parts NG1_ROM_0, NG1_ROM_1, ... (tools/release/restore_rom.py
# puts them back together and checks the result).
set -euo pipefail

rom="${1:-ninjagaiden.nes}"
repo="${2:-gabbycat4/NinjaGaiden1Recomp}"

[ -f "$rom" ] || { echo "No ROM at $rom (pass its path as the first argument)" >&2; exit 1; }
command -v gh >/dev/null || { echo "Needs the GitHub CLI (gh), logged in" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

xz -9e -c "$rom" | base64 -w0 > "$tmp/rom.b64"
split -b 40000 -d -a 1 "$tmp/rom.b64" "$tmp/part"

n=0
for f in "$tmp"/part*; do
    gh secret set "NG1_ROM_$n" --repo "$repo" < "$f"
    n=$((n + 1))
done
if [ "$n" -gt 8 ]; then
    echo "The ROM needed $n parts; the workflow reads at most 8" >&2
    exit 1
fi
# Clear leftovers from an earlier, larger upload.
for i in $(seq "$n" 7); do
    gh secret delete "NG1_ROM_$i" --repo "$repo" >/dev/null 2>&1 || true
done

echo "Stored the ROM in $n secrets on $repo."
