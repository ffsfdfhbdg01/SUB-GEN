#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Development build and every test (C++ units and the docs check). Extra arguments go to ctest.
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure "$@"
