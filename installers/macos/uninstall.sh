#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Removes @NAME@ installed by its macOS installer: the VST3 and the Audio Unit, this folder and the installer's
# receipts. Your own presets and settings in your home folder stay. Run it with sudo:
#   sudo "/Library/Application Support/@VENDOR@/@NAME@/uninstall.sh"
set -u
NAME='@NAME@'
VENDOR='@VENDOR@'
BUNDLE='@BUNDLE@'
for v in "$NAME" "$VENDOR" "$BUNDLE"; do # plain names only: they are parts of the paths removed below
    case "$v" in '' | *[!A-Za-z0-9._-]*) echo "bad name in this script: '$v'" >&2; exit 2 ;; esac
done
if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo \"$0\"" >&2
    exit 1
fi
rm -rf "/Library/Audio/Plug-Ins/VST3/$NAME.vst3" "/Library/Audio/Plug-Ins/Components/$NAME.component"
for id in "$BUNDLE.vst3" "$BUNDLE.au" "$BUNDLE.support"; do
    pkgutil --forget "$id" >/dev/null 2>&1 || true
done
rm -rf "/Library/Application Support/$VENDOR/$NAME"
rmdir "/Library/Application Support/$VENDOR" 2>/dev/null || true
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
echo "$NAME removed."
