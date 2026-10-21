#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Release build (universal binaries on macOS). Extra arguments go to `cmake --build`.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset release
cmake --build --preset release "$@"
