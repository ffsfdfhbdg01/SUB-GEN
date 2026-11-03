#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Builds the macOS installer (SPEC_BUILD_RELEASE §4.1): component packages for the VST3, the Audio Unit (with the
# postinstall that refreshes the AU cache) and the uninstall script, then productbuild with
# installers/macos/distribution.xml. Unsigned. Checks the payload against the install paths of §4.1; installs nothing.
# Usage: scripts/package.sh [build dir] [output dir]   (defaults: build/release, dist)
set -euo pipefail
cd "$(dirname "$0")/.."
build=${1:-build/release}
out=${2:-dist}
ident() { sed -n "s/^set($1 \"\(.*\)\")$/\1/p" cmake/ProductIdentity.cmake; }
name=$(ident SUBGEN_PRODUCT_NAME)
vendor=$(ident SUBGEN_VENDOR)
bundle=$(ident SUBGEN_BUNDLE_ID)
version=$(tr -d '[:space:]' < VERSION)
artefacts=
for config in Release RelWithDebInfo Debug MinSizeRel; do
    if [ -d "$build/SubGen_artefacts/$config" ]; then
        artefacts="$build/SubGen_artefacts/$config"
        break
    fi
done
[ -n "$artefacts" ] || { echo "no plug-in build in $build" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fill() { sed -e "s|@NAME@|$name|g" -e "s|@VENDOR@|$vendor|g" -e "s|@BUNDLE@|$bundle|g" -e "s|@VERSION@|$version|g" "$1"; }

# one component package per payload; a bundle is not relocatable, so an update never lands in a copy elsewhere
component() { # id root install-location output [scripts]
    local plist="$work/$1.plist"
    pkgbuild --analyze --root "$2" "$plist" >/dev/null
    local i=0
    while plutil -extract "$i" xml1 -o /dev/null "$plist" >/dev/null 2>&1; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
        i=$((i + 1))
    done
    pkgbuild --quiet --root "$2" --component-plist "$plist" --install-location "$3" --identifier "$bundle.$1" \
        --version "$version" ${5:+--scripts "$5"} "$4"
}
mkdir -p "$work/vst3" "$work/au" "$work/support" "$work/pkgs" "$work/resources" "$out"
ditto "$artefacts/VST3/$name.vst3" "$work/vst3/$name.vst3"
ditto "$artefacts/AU/$name.component" "$work/au/$name.component"
fill installers/macos/uninstall.sh > "$work/support/uninstall.sh"
chmod 755 "$work/support/uninstall.sh"
component vst3 "$work/vst3" /Library/Audio/Plug-Ins/VST3 "$work/pkgs/$name-VST3.pkg"
component au "$work/au" /Library/Audio/Plug-Ins/Components "$work/pkgs/$name-AU.pkg" installers/macos/scripts
component support "$work/support" "/Library/Application Support/$vendor/$name" "$work/pkgs/$name-Support.pkg"

cp LICENSE "$work/resources/LICENSE.txt"
fill installers/macos/resources/README.txt > "$work/resources/README.txt"
fill installers/macos/distribution.xml > "$work/distribution.xml"
pkg="$out/$name-$version-macOS.pkg"
productbuild --quiet --distribution "$work/distribution.xml" --package-path "$work/pkgs" --resources "$work/resources" "$pkg"

# what the installer puts where: each component's payload under its install location
pkgutil --expand "$pkg" "$work/check"
: > "$work/installed.txt"
for component in "$work/check"/*.pkg; do
    location=$(sed -n 's/.*install-location="\([^"]*\)".*/\1/p' "$component/PackageInfo")
    (cd "$work" && gzip -dc "$component/Payload" | cpio -it 2>/dev/null) \
        | sed -e 's|^\./||' -e '/^\.$/d' -e "s|^|$location/|" >> "$work/installed.txt"
done
missing=0
for want in "/Library/Audio/Plug-Ins/VST3/$name.vst3/Contents/MacOS/$name" \
            "/Library/Audio/Plug-Ins/VST3/$name.vst3/Contents/Info.plist" \
            "/Library/Audio/Plug-Ins/Components/$name.component/Contents/MacOS/$name" \
            "/Library/Audio/Plug-Ins/Components/$name.component/Contents/Info.plist" \
            "/Library/Application Support/$vendor/$name/uninstall.sh"; do
    grep -qxF "$want" "$work/installed.txt" || { echo "missing from the payload: $want" >&2; missing=1; }
done
grep -q '<postinstall file=' "$work/check/$name-AU.pkg/PackageInfo" || { echo "the AU package has no postinstall" >&2; missing=1; }
[ "$missing" -eq 0 ] || exit 1
echo "$pkg: $(wc -l < "$work/installed.txt" | tr -d ' ') files; VST3, AU and the uninstaller in their §4.1 places"
