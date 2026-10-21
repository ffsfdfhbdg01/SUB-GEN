#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Validates a build: pluginval (strictness 10) on the VST3 and the AU, then auval. Exits non-zero on any failure.
# Usage: scripts/validate.sh [build dir]   (default: build/release)
set -euo pipefail
cd "$(dirname "$0")/.."
build=${1:-build/release}
ident() { sed -n "s/^set($1 \"\(.*\)\")$/\1/p" cmake/ProductIdentity.cmake; }
name=$(ident SUBGEN_PRODUCT_NAME)
manufacturer=$(ident SUBGEN_MANUFACTURER_CODE)
code=$(ident SUBGEN_PLUGIN_CODE)
artefacts=
for config in Release RelWithDebInfo Debug MinSizeRel; do
    if [ -d "$build/SubGen_artefacts/$config" ]; then
        artefacts="$build/SubGen_artefacts/$config"
        break
    fi
done
[ -n "$artefacts" ] || { echo "no plug-in build in $build" >&2; exit 2; }
pluginval=tools/bin/pluginval.app/Contents/MacOS/pluginval
[ -x "$pluginval" ] || { echo "pluginval is missing: run scripts/fetch_pluginval.sh" >&2; exit 2; }

status=0
for bundle in "$artefacts/VST3/$name.vst3" "$artefacts/AU/$name.component"; do
    echo "== pluginval --strictness-level 10: $bundle"
    "$pluginval" --strictness-level 10 --validate "$bundle" || status=1
done
echo "== auval -v aufx $code $manufacturer"
auval -v aufx "$code" "$manufacturer" || status=1
exit $status
