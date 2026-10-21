# Third-party licences

SUB-GEN is free software under the GNU Affero General Public License v3.0 or later (see [LICENSE](LICENSE)).
It is built from the components below. The list follows the versions pinned in this repository and JUCE's own
software bill of materials, [JUCE/JUCE.spdx.json](JUCE/JUCE.spdx.json); it is checked again for every release.

## Compiled into the plug-in

| Component | Version | Licence | Notes |
|---|---|---|---|
| JUCE framework | 9.0.4 | AGPL-3.0-only (dual-licensed AGPLv3 / commercial; used here under AGPLv3) | `JUCE/` submodule; modules juce_core, juce_audio_basics, juce_dsp, juce_audio_processors, juce_audio_utils and their dependencies |
| VST3 SDK | 3.8.0 | MIT | vendored in JUCE (`juce_audio_processors_headless`) |
| PreSonus plug-in extensions | — | public domain | vendored in JUCE (`juce_audio_processors_headless/format_types/pslextensions`) |
| AudioUnitSDK | 1.1.0 | Apache-2.0 | vendored in JUCE (`juce_audio_plugin_client`), macOS AU only |
| zlib | 1.3.2 | Zlib | vendored in JUCE (`juce_core`) |
| HarfBuzz | 14.2.1 | MIT-Modern-Variant | vendored in JUCE (`juce_graphics`) |
| SheenBidi | 2.9.0 | Apache-2.0 | vendored in JUCE (`juce_graphics`) |
| LunaSVG | 3.5.0 | MIT | vendored in JUCE (`juce_graphics`) |
| PlutoVG | 1.3.2 | MIT | vendored in JUCE (`juce_graphics`) |
| libpng | 1.6.58 | libpng-2.0 | vendored in JUCE (`juce_graphics`) |
| jpeglib (IJG) | 10.0 | IJG | vendored in JUCE (`juce_graphics`) |
| libwebp | 1.6.0 | BSD-3-Clause | vendored in JUCE (`juce_graphics`) |
| FLAC | 1.5.0 | BSD-3-Clause | vendored in JUCE (`juce_audio_formats`) |
| libogg | 1.3.6 | BSD-3-Clause | vendored in JUCE (`juce_audio_formats`) |
| libvorbis | 1.3.7 | BSD-3-Clause | vendored in JUCE (`juce_audio_formats`) |
| Opus, opusfile, libopusenc | 1.6.1, 0.12-59-g6dfd29e, 0.3 | BSD-3-Clause | vendored in JUCE (`juce_audio_formats`) |

Only in the AAX build (manual CI job, `-DSUBGEN_AAX=ON`): AAX SDK 2.9, Avid AAX licence or GPL-3.0-only.

Vendored in the same JUCE modules but not compiled into SUB-GEN: Oboe (Android), ASIO SDK (`JUCE_ASIO` is off), the
LV2 SDK with lilv, serd, sord and sratom (no LV2 format or LV2 hosting), and the webview interop script
(`JUCE_WEB_BROWSER=0`).

## Build and test tools (not distributed)

| Component | Version | Licence | Use |
|---|---|---|---|
| CPM.cmake | 0.43.2 | MIT | CMake dependency download (`cmake/CPM.cmake`) |
| Catch2 | 3.16.1 | BSL-1.0 | unit tests |
| pluginval | 1.0.4 | GPL-3.0 | plug-in validation (downloaded to `tools/bin/`, not committed) |
