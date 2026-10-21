#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Downloads pluginval 1.0.4 for macOS into tools/bin/ (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.0.4
sha256=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f  # pluginval_macOS.zip, 9202795 bytes
dest=tools/bin
app="$dest/pluginval.app"
if [ -x "$app/Contents/MacOS/pluginval" ]; then
    echo "pluginval is already in $app"
    exit 0
fi
mkdir -p "$dest"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL "https://github.com/Tracktion/pluginval/releases/download/v$version/pluginval_macOS.zip" -o "$tmp/pluginval.zip"
echo "$sha256  $tmp/pluginval.zip" | shasum -a 256 -c - >/dev/null || { echo "pluginval_macOS.zip: SHA-256 mismatch" >&2; exit 1; }
ditto -x -k "$tmp/pluginval.zip" "$dest"
echo "pluginval $version -> $app"
