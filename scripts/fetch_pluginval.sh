#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Downloads pluginval 1.0.4 for macOS into tools/bin/ (git-ignored). The archive is checked against its SHA-256 and
# unpacked in a temporary directory before it replaces the app; a stamp next to the app records the verified digest,
# and an app without a matching stamp is fetched again.
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.0.4
sha256=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f  # pluginval_macOS.zip, 9202795 bytes
dest=tools/bin
app="$dest/pluginval.app"
stamp="$dest/pluginval-$version.sha256"
if [ -x "$app/Contents/MacOS/pluginval" ] && [ "$(cat "$stamp" 2>/dev/null)" = "$sha256" ]; then
    echo "pluginval $version (verified) is already in $app"
    exit 0
fi
mkdir -p "$dest"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL "https://github.com/Tracktion/pluginval/releases/download/v$version/pluginval_macOS.zip" -o "$tmp/pluginval.zip"
echo "$sha256  $tmp/pluginval.zip" | shasum -a 256 -c - >/dev/null || { echo "pluginval_macOS.zip: SHA-256 mismatch" >&2; exit 1; }
ditto -x -k "$tmp/pluginval.zip" "$tmp/unpacked"
[ -x "$tmp/unpacked/pluginval.app/Contents/MacOS/pluginval" ] || { echo "pluginval_macOS.zip: unexpected layout" >&2; exit 1; }
rm -rf "$app" "$stamp"
mv "$tmp/unpacked/pluginval.app" "$app"
echo "$sha256" > "$stamp"
echo "pluginval $version -> $app"
