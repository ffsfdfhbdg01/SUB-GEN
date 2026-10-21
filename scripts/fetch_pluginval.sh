#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Downloads pluginval 1.0.4 for macOS into tools/bin/ (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.0.4
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
ditto -x -k "$tmp/pluginval.zip" "$dest"
echo "pluginval $version -> $app"
