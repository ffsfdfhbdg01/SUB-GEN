# SUB-GEN — plan implementacji Fazy 3 (rdzeń wtyczki, CI, spike'i)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zbudować rdzeń wtyczki SUB-GEN w C++20/JUCE 9.0.4 i spełnić „Definition of Done” Fazy 3 z TEST_PLAN §4: zielone testy jednostkowe `dsp`, `engines`, `chain` i `routing`, zielone CI na macOS i Windows, pluginval 10 PASS oraz zamknięte spike'i SPK-1…SPK-6. Rdzeń obejmuje:
- bibliotekę DSP przeniesioną z prototypu Fazy 2;
- parametry, I/O i routing;
- procesor VST3/AU.

**Architecture:**
- **Moduły i zależności:** kod w `src/` w modułach zależnych tylko „w dół”: core → dsp → engines → chain → routing → analysis → params → plugin.
- **Targety:**
  - `subgen_dsp` — target INTERFACE (źródła `core`…`analysis`), kompilowany do testów i do wtyczki; nie zależy od GUI JUCE.
  - `SubGen` — shared code z `juce_add_plugin`. Składa tor S1…S8 w pętli poddbloków po 64 próbki na bezwzględnym liczniku próbek, więc wynik nie zależy od rozmiaru bloku hosta.
- **Testy:** każdy blok DSP ma test Catch2 z kryterium z TEST_PLAN. Algorytmy stanowe dodatkowo porównuje się z krótkimi danymi złotymi wyeksportowanymi z `proto/` (prototyp jest implementacją wzorcową).

**Tech Stack:**
- C++20, CMake 4.3 (≥ 3.25), Ninja;
- JUCE 9.0.4 (submoduł git, płytki); Catch2 v3.16.1 przez CPM.cmake v0.43.2;
- Apple clang 17 (CLT 16.4, bez Xcode); MSVC 2022 (tylko CI);
- GitHub Actions; pluginval 1.0.4;
- Python 3.13 (`proto/`, narzędzia w `tools/`).

**Spec:**
- `docs/SPEC.md` 0.3, `docs/SPEC_IO_UI.md`, `docs/SPEC_BUILD_RELEASE.md`, `docs/PARAMETERS.md`, `docs/TEST_PLAN.md` 0.3;
- `docs/DECISIONS.md` (ADR-001…029);
- `docs/PHASE2_REPORT.md` (wartości, Załączniki A–C, §11 — poprawki po przeglądzie D8);
- implementacja wzorcowa: `proto/subgen_proto/`.

**Poza zakresem:** UI, metering, presety, A/B i undo (Faza 4); testy integracyjne z hostem headless, soak, T-STAB-01/02/04, validator Steinberga T-FMT-03 (Faza 5); instalatory produkcyjne i release (Faza 6). Każda z tych faz dostanie własny plan.

**Polecenia** (z katalogu repo):

| Co | Polecenie |
|---|---|
| konfiguracja i build | `cmake --preset dev && cmake --build --preset dev` |
| wszystkie testy | `ctest --preset dev --output-on-failure` |
| testy jednego modułu | `build/dev/tests/subgen_dsp_tests "[divide]"` |
| testy procesora | `build/dev/tests/subgen_plugin_tests` |
| wtyczka universal | `cmake --preset release && cmake --build --preset release` |
| walidacja | `scripts/validate.sh` |
| dane złote | `PYTHONPATH=proto .venv/bin/python proto/tools/export_golden.py --out tests/golden` (`--check` porównuje z repo) |
| parametry | `.venv/bin/python tools/gen_params.py --check` |
| zakończenie zadania (task-done) | `sh -c "cmake --build --preset dev && ctest --preset dev --output-on-failure"` |

## Zgody i punkty zatrzymania

**Zgody już udzielone (DECISIONS B2, pytanie 9):**
- Ninja przez Homebrew (~0,3 MB);
- JUCE jako submoduł git — płytki klon tagu 9.0.4; repo na GitHubie ma 289 MB, faktyczny rozmiar trafia do dziennika;
- pluginval 1.0.4 (macOS, ~25 MB, do `tools/bin/`, poza gitem);
- bez pełnego Xcode.

Nowe, małe zależności: Catch2 v3.16.1 i CPM v0.43.2 — źródła pobierane przy konfiguracji do `.cache/cpm`, kilka MB. Obejmuje je zatwierdzenie tego planu.

**Przegląd D8 przetworzony (2026-10-21):** poprawki prototypu (R1–R5 i Minor) są w `proto/` (251 testów, 93/93 kontroli), a ich skutki — w SPEC 0.3 i w zadaniach 6–11, 14–16, 19, 24, 25 i 30 tego planu. Dane złote eksportuje się z tego stanu prototypu.

**Punkty zatrzymania:**
- **STOP 1 (zadanie 3) — pierwszy push do publicznego repo.**
  - Przed pushem pokazuję, co stanie się publiczne: adres autora commitów `ffsfdfhbdg@gmail.com` (opcja: przepisać na adres noreply GitHuba, co zmienia hashe lokalnych commitów), `docs/` (m.in. PROMPT.md, RESEARCH.md z informacjami o dyskach), `proto/`, plany.
  - Push dopiero po wyraźnym OK, według PROMPT §14: `main` (stan dokumentów z Fazy 1), potem `dev`; nigdy force-push; bez zmian ustawień repo.
  - Bez zgody kroki CI w zadaniach 3, 26 i 28 czekają, a reszta idzie lokalnie (Windows pozostaje niezweryfikowany — ryzyko R2).
- **STOP 2 (zadanie 27):** test ręczny w Cubase 13 (SPK-1, SPK-2) — wykonuje użytkownik.
- **STOP 3 (zadanie 30):** bramka Fazy 3.

**Proces:** wykonanie inline (superpowers:executing-plans), TDD, dziennik `.superpowers/sdd/2026-10-20-subgen-phase3-core/progress.md`, commity lokalne na `dev`. Na końcu przegląd całej gałęzi w niezależnej sesji tylko do odczytu (jak D8), jeśli użytkownik się zgodzi.

## Global Constraints

**Język, licencja, warunki builda**
- C++20; CMake ≥ 3.25; Ninja. Każdy plik źródłowy zaczyna się od `// SPDX-License-Identifier: AGPL-3.0-or-later` (Python: `#`).
- Ostrzeżenia jako błędy tylko na naszych plikach: `-Wall -Wextra -Wpedantic -Werror`, MSVC `/W4 /WX`. Bez `-ffast-math`.
- macOS: `CMAKE_OSX_DEPLOYMENT_TARGET` 11.0; release universal `arm64;x86_64`; testy deweloperskie w natywnej architekturze, RelWithDebInfo.
- Windows: x64, statyczne CRT (`MultiThreaded$<$<CONFIG:Debug>:Debug>`).
- Definicje JUCE: `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0` — brak sieci i telemetrii (SPEC §3).

**Precyzja i real-time (SPEC §3, §4.3, §4.6, §4.7)**
- Wewnętrznie `double`; I/O float32 i float64 (`supportsDoublePrecisionProcessing() == true`).
- fs 44,1–192 kHz; blok 1–8192, zmienny.
- Ścieżki `process` bez alokacji, blokad, I/O i wyjątków; bufory alokuje `prepare(...)`. Testy sprawdzają to przez `AllocGuard`.
- `ScopedNoDenormals`; straż NaN/Inf na wejściu źródła i na wyjściu toru sub.

**Determinizm (antycel 4)**
- Poddbloki po 64 próbki na bezwzględnym zegarze próbek; brak losowości w torze audio.
- Wynik niezależny od rozmiaru bloku: cel bit-exact, a z FFT ≤ −130 dB.

**Latencja (SPEC §4.4, liczby dokładnie)**
- Lookahead silnika E: Live 0, Mix 45 ms, Print 55 ms (wspólny dla wszystkich silników).
- Lookahead kompresora: 0 / 0 / 5 ms. Lookahead limitera: 0 / 2 / 5 ms.
- L_look = 0 / 47 / 65 ms. L_os = ceil(latencja oversamplera JUCE).
- N_LIN = 4096·2^⌈log2(fs/48 kHz)⌉ + 1. L_lin = (N_LIN − 1)/2 + 256 (partycja konwolucji 256 próbek, SPEC §4.4, §7.6).
- L_total = L_os + L_look + L_lin + round(align_ms·fs/1000).
- Latencja zależy wyłącznie od `quality`, `filt_phase` i `align_ms`.

**Tożsamość (antycel 1):** `sub_level` = Off (Add) lub `mix` = 0 (Mix) → wyjście = wejście opóźnione o L_total, bit-exact. Każdy stopień w stanie neutralnym (0 dB, Off) jest dokładną tożsamością.

**Wygładzanie i crossfade:** klasy wygładzania G20/F20/L20 oraz crossfade „X ms” z kolumny PARAMETERS realizują klasy DSP wewnątrz `setParams`/`setControl`; procesor przekazuje wartości docelowe co poddblok.

**Wersje i dane**
- Wersje przypięte: JUCE 9.0.4, Catch2 v3.16.1, CPM v0.43.2, pluginval 1.0.4.
- Dane złote w `tests/golden/` (format `.sgg`, ≤ 3 MB łącznie, deterministyczne: `export_golden.py --check`), eksportowane z `proto/` po przeglądzie D8. Tam, gdzie prototyp liczy offline (bramka DIVIDE w Mix/Print), wzorcem jest jego wariant przyczynowy z zadania 8.

**Repozytorium i dysk**
- Commity po angielsku `type: summary` z trailerem `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; bez pusha bez zgody (STOP 1); `main` tylko przez merge po OK.
- Build w `build/` (poza gitem). Wolne miejsce na Sam-dysk ≥ 10 GiB po każdym kamieniu milowym (`df -h /Volumes/Sam-dysk`; dziś 20 GiB).

## Review Focus
1. **Blok hosta większy niż zgłoszony w `prepareToPlay`, blok 1 i rozmiary nieparzyste** — przetwarzanie w kawałkach, bez alokacji i bez zmiany wyniku. Test: zadanie 25.
2. **Ponowne `prepareToPlay` z innym fs lub layoutem w trakcie sesji** — parametry zachowane, stany wyzerowane, latencja zgłoszona na nowo. Test: zadanie 25 (oraz reset silników w zadaniu 13).
3. **NaN/Inf tylko w sidechainie, DC, pełna skala po ciszy** — wyjście skończone, silniki wracają do pracy. Testy: zadanie 25 (sidechain) i zadanie 8 (DC, prostokąt).
4. **Szybka automatyzacja wszystkich parametrów A w każdym bloku** — brak NaN, skoki ograniczone. Test: zadanie 25 (10⁴ zmian; pełne T-STAB-01 w Fazie 5).
5. **`out_mode` LFE bez kanału LFE, `sc_use` bez szyny sidechain, layout mono** — zachowanie Mono z W-01, źródło main z W-02, mono działa. Test: zadanie 25.

## Kamienie milowe
| Kamień | Zadania | Zakres |
|---|---|---|
| M1 | 1–3 | szkielet, build, CI (SPK-1, SPK-3, SPK-5) |
| M2 | 4–6 | prymitywy DSP i wsparcie testów |
| M3 | 7–13 | silniki |
| M4 | 14–21 | tor sub (SPK-6 w zadaniu 19) |
| M5 | 22–26 | parametry, routing, Auto-Align, procesor, stan |
| M6 | 27–30 | spike'i SPK-1/2/4, benchmarki, raport |

---

### Task 1: Szkielet repo, JUCE 9.0.4, CMake, pusta wtyczka, Catch2 (SPK-1 lokalnie)

**Files:**
- Create:
  - CMake: `CMakeLists.txt`, `CMakePresets.json`, `cmake/ProductIdentity.cmake`, `cmake/CPM.cmake` (v0.43.2), `cmake/Warnings.cmake`;
  - pliki projektu: `VERSION` (`0.1.0`), `LICENSE` (tekst AGPLv3: `gh api licenses/agpl-3.0 --jq .body`), `THIRD_PARTY_LICENSES.md`, `README.md`, `CHANGELOG.md` (Keep a Changelog, „Unreleased”), `.clang-format`;
  - `.gitmodules` + `JUCE/`;
  - kod i testy: `src/plugin/PluginProcessor.h/.cpp`, `tests/CMakeLists.txt`, `tests/unit/test_smoke.cpp`.
- Modify: `.gitignore` (`build/`, `.cache/`, `tools/bin/`).

**Interfaces:**
- **Produces (targety CMake):**
  - `subgen_dsp` — INTERFACE: źródła `src/{core,dsp,engines,chain,routing,analysis}`, include `src/`, linkuje `juce::juce_core`, `juce::juce_audio_basics`, `juce::juce_dsp`.
  - `SubGen` — `juce_add_plugin`, formaty VST3 AU Standalone; z opcją `-DSUBGEN_AAX=ON` także AAX (tylko ręczny job CI). Linkuje `subgen_dsp`, `juce::juce_audio_utils`, `src/params`, `src/plugin`.
  - `subgen_dsp_tests` (linkuje `subgen_dsp`) i `subgen_plugin_tests` (linkuje `SubGen`) z `Catch2::Catch2WithMain` i `catch_discover_tests`.
  - Funkcja `subgen_strict_warnings(<target>)` — ostrzeżenia nakładane przez `set_source_files_properties` wyłącznie na pliki z `src/` i `tests/`.
- **Produces (presety):** `dev` (Ninja, RelWithDebInfo, natywna architektura, testy), `release` (Ninja, Release, `arm64;x86_64`), `asan` (ASan + UBSan), `tsan`, `ci-windows` (Ninja, Release, MSVC).
- **Produces (procesor):** `subgen::plugin::SubGenProcessor : juce::AudioProcessor` — na razie stereo in/out, przepuszczanie, latencja 0, double precision, `createEditor()` → `juce::GenericAudioProcessorEditor`.
- **Produces (identyfikatory):** `cmake/ProductIdentity.cmake` — `SUBGEN_PRODUCT_NAME "SUB-GEN"`, `SUBGEN_VENDOR "Vendor"`, `SUBGEN_BUNDLE_ID "com.vendor.subgen"`, `SUBGEN_MANUFACTURER_CODE "Vndr"`, `SUBGEN_PLUGIN_CODE "Sbgn"`. To jedyne miejsce identyfikatorów zależnych od nazwy (SPEC §2, ADR-002).

- [ ] **Step 1: Narzędzia.**
  - Instalacja: `brew install ninja`.
  - Submoduł:
    ```
    git submodule add --depth 1 https://github.com/juce-framework/JUCE.git JUCE
    git -C JUCE fetch --depth 1 origin tag 9.0.4
    git -C JUCE checkout 9.0.4
    git config -f .gitmodules submodule.JUCE.shallow true
    ```
  - Weryfikacja: `git -C JUCE describe --tags` → `9.0.4`. Do dziennika: `du -sh JUCE` (oczekiwane ≤ 300 MB) i `df -h /Volumes/Sam-dysk`.
- [ ] **Step 2: Test (czerwony).** `TEST_CASE("processor passes audio through unchanged", "[smoke]")`:
  - procesor przygotowany jak przez hosta: stereo, 48 kHz, 512;
  - sinus 60 Hz w buforach float i double → wyjście == wejście bit w bit;
  - `getLatencySamples() == 0`.

  Uruchomić `cmake --preset dev && cmake --build --preset dev` → oczekiwany błąd kompilacji (brak `PluginProcessor.h`).
- [ ] **Step 3: Implementacja.**
  - `juce_add_plugin(SubGen …)` z: `PRODUCT_NAME`, `COMPANY_NAME`, `BUNDLE_ID`, `PLUGIN_MANUFACTURER_CODE` i `PLUGIN_CODE` z ProductIdentity; `FORMATS VST3 AU Standalone`; `IS_SYNTH FALSE`; `NEEDS_MIDI_INPUT FALSE`; `AU_MAIN_TYPE kAudioUnitType_Effect`; `VST3_CATEGORIES Fx`; `COPY_PLUGIN_AFTER_BUILD TRUE`; `VERSION` z pliku `VERSION`.
  - CPM z `CPM_SOURCE_CACHE=${CMAKE_SOURCE_DIR}/.cache/cpm`.
  - Opcje, których JUCE 9.0.4 nie zna, rozstrzygnąć według `JUCE/docs/CMake API.md` i zapisać jako Ruling.
- [ ] **Step 4: Zielono i SPK-1.**
  - `cmake --build --preset dev && ctest --preset dev` → `[smoke]` PASS.
  - `cmake --preset release && cmake --build --preset release`, potem:
    - `lipo -info` dla binariów VST3 i AU → `arm64 x86_64`;
    - `auval -v aufx Sbgn Vndr` → „AU VALIDATION SUCCEEDED”.
  - Do dziennika: czas buildu release, `du -sh build` i wpis „SPK-1 (lokalnie): …”.
- [ ] **Step 5: Commit** `build: project skeleton with JUCE 9.0.4, CMake presets and a pass-through plugin`.

### Task 2: Skrypty, pluginval lokalnie (SPK-3 lokalnie), kontrola spójności dokumentów

**Files:**
- Create:
  - skrypty: `scripts/build.sh`, `scripts/test.sh`, `scripts/fetch_pluginval.sh`, `scripts/validate.sh`;
  - narzędzia: `tools/check_docs.py`, `tools/docs_allowlist.txt`, `tools/tests/test_check_docs.py`.
- Modify: `tests/CMakeLists.txt` (ctest `check_docs`).

**Interfaces:**
- `scripts/validate.sh` uruchamia:
  - `tools/bin/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate <bundle>` dla VST3 i AU;
  - `auval -v aufx Sbgn Vndr`;
  - kod wyjścia ≠ 0 przy błędzie.
- `scripts/fetch_pluginval.sh` pobiera `https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip` do `tools/bin/`.
- `tools/check_docs.py [--docs docs]` → exit 0 albo lista naruszeń (SPEC_BUILD_RELEASE §11):
  - (a) dokładnie 141 unikalnych ID po rozwinięciu `eqN_*`;
  - (b) każdy identyfikator lower_snake w backtickach w SPEC.md, SPEC_IO_UI.md i TEST_PLAN.md jest w rejestrze albo w `docs_allowlist.txt`;
  - (c) każdy cytowany `T-XXX-NN` istnieje w TEST_PLAN.

- [ ] **Step 1: Testy (czerwone).** `test_registry_has_141_ids`, `test_unknown_backtick_id_is_reported` (dokument tymczasowy z `` `no_such_param` ``), `test_unknown_test_id_is_reported` (`T-FOO-99`), `test_current_docs_pass`. Uruchomić `.venv/bin/python -m unittest discover -s tools/tests` → ImportError.
- [ ] **Step 2: Implementacja.**
  - `check_docs.py` parsuje tabele Markdown z PARAMETERS.md.
  - `docs_allowlist.txt` powstaje z obecnych dokumentów (sygnały i wielkości wewnętrzne, np. `x_b`, `e_pk`, `e_rms`, `f_sub1`, `d`, `u`); listę przejrzeć ręcznie.
  - Zielono; ctest `check_docs` wywołuje skrypt przez `Python3_EXECUTABLE`.
- [ ] **Step 3: Skrypty i walidacja.**
  - `scripts/fetch_pluginval.sh`, potem `scripts/validate.sh` na buildzie release z zadania 1 → PASS dla VST3 i AU (SPK-3 lokalnie, wpis do dziennika).
  - `scripts/build.sh` (preset release) i `scripts/test.sh` (preset dev + ctest).
- [ ] **Step 4: Commit** `build: helper scripts, local pluginval and the docs consistency check`.

### Task 3: CI GitHub Actions (SPK-3, SPK-5) i pierwszy push (STOP 1)

**Files:**
- Create: `.github/workflows/build_and_test.yml`, `scripts/ci/check_windows_deps.ps1`.

**Interfaces (workflow):**
- **Ustawienia:**
  - wyzwalacze: `push` (`dev`, `main`), `pull_request`, `workflow_dispatch` (wejście `aax`, domyślnie false);
  - `permissions: contents: read`;
  - `concurrency: ${{ github.workflow }}-${{ github.ref }}` z `cancel-in-progress: true`;
  - akcje przypięte do najnowszej wersji głównej sprawdzonej w kroku 1.
- **Jobs:**
  - `build-macos` (`macos-15`):
    - checkout z płytkimi submodułami, `brew install ninja`;
    - preset `release` z testami, build, `ctest`;
    - `auval -v aufx Sbgn Vndr`; pluginval 1.0.4 (strictness 10) na VST3 i AU;
    - `lipo -info` (T-FMT-05), `otool -L` (T-FMT-06);
    - artefakty: zip VST3 i AU.
  - `build-windows` (`windows-2025`):
    - `ilammy/msvc-dev-cmd` x64, preset `ci-windows`, build, `ctest`;
    - pluginval 1.0.4 na VST3;
    - `check_windows_deps.ps1`: `dumpbin /dependents` nie może pokazywać `MSVCP*.dll`, `VCRUNTIME*.dll` ani `api-ms-win-crt-*` (SPK-5);
    - artefakt.
  - `sanitizers` (`macos-15`): presety `asan` i `tsan` — `subgen_dsp_tests` oraz `subgen_plugin_tests`.
  - `rtsan` (`ubuntu-24.04`, clang ≥ 20, `continue-on-error: true` — SHOULD): `subgen_dsp_tests` z `-fsanitize=realtime`.
  - `aax-build` (tylko przy `workflow_dispatch` z `aax: true`): build z `-DSUBGEN_AAX=ON`, artefakt niepodpisany.

- [ ] **Step 1: Wersje i środowisko.**
  - Sprawdzić `gh api repos/{actions/checkout,actions/upload-artifact,ilammy/msvc-dev-cmd}/releases/latest`.
  - Ustalić, czy obraz `windows-2025` ma Ninja (inaczej `choco install ninja`).
  - Zapisać w dzienniku.
- [ ] **Step 2: Workflow.**
  - Napisać plik; sprawdzić składnię: `ruby -ryaml -e 'YAML.load_file(".github/workflows/build_and_test.yml")'`.
  - Commit `ci: build, test and validate on macOS and Windows`.
- [ ] **Step 3: STOP 1 — zgoda na pierwszy push** (treść w sekcji „Zgody i punkty zatrzymania”). Bez zgody: zapisać w dzienniku, że CI czeka, i przejść do zadania 4.
- [ ] **Step 4: Push i weryfikacja.**
  - Po zgodzie: `git push origin main`, potem `git push -u origin dev`.
  - `gh run watch --exit-status` w tle; oczekiwane: wszystkie joby zielone poza ewentualnie `rtsan` (wynik zapisany).
  - Błędy CI naprawiać kolejnymi commitami (każdy z przyczyną w dzienniku), aż CI będzie zielone.
  - Do dziennika: „SPK-3: pluginval PASS macOS/Windows”, „SPK-5: brak bibliotek runtime MSVC”.

### Task 4: Wsparcie testów — FFT w double, pomiary, sygnały, dane złote, AllocGuard

**Files:**
- Create:
  - kod: `src/dsp/Fft.h/.cpp`;
  - wsparcie testów: `tests/support/{Measure,Signals,Golden,AllocGuard}.h/.cpp`, `tests/unit/test_support.cpp`;
  - eksporter: `proto/tools/__init__.py`, `proto/tools/export_golden.py`, `proto/tests/test_export_golden.py`;
  - dane: `tests/golden/signals.sgg`.
- Modify: `proto/subgen_proto/signals.py` (+ `xorshift_noise`), `proto/tests/test_signals.py`.

**Interfaces:**
- `subgen::dsp::RealFft(int order)` — radix-2, double:
  - `size()`;
  - `forward(const double* in, std::complex<double>* out)` — N/2 + 1 binów;
  - `inverse(const std::complex<double>* in, double* out)` — skala 1/N;
  - alokacja tylko w konstruktorze.
- `subgen::test` (Measure) — semantyka jak `proto/subgen_proto/util.py` i `drive.nonharmonic_db`:
  - `double rms(std::span<const double>)`;
  - `double nullDb(ref, test)` — minimum −400;
  - `PeakInfo peakFreq(x, fs, fLo, fHi, pad = 16)` — Blackman-Harris, parabola na log-amplitudzie; `struct PeakInfo { double freqHz, amplitude; }`;
  - `double zcFreq(x, fs)`;
  - `std::vector<double> harmonicAmps(x, fs, f0, n = 10)`;
  - `double thdDb(x, fs, f0, n = 10)`;
  - `double bandDb(x, fs, f1, f2)`;
  - `double nonharmonicDb(x, fs, f0, fMax)`.
- Signals — parametry i wartości domyślne jak w `signals.py`:
  - `sine`, `logSweep`, `kick`, `bass808`, `twoTone`, `square`, `impulse`, `silence`, `dc`, `nanInf`, `denormalRamp`;
  - `xorshiftNoise(n, seed)`;
  - `bandNoise(fs, seconds, fLo, fHi, levelDb, seed)` — widmo kształtowane FFT, źródło xorshift.
- `xorshiftNoise` / `signals.xorshift_noise` — identycznie w C++ i Pythonie, uint64:
  ```
  x ^= x >> 12; x ^= x << 25; x ^= x >> 27
  y = (x · 2685821657736338717) >> 11
  wynik = y / 2^53 · 2 − 1
  ```
- Format `.sgg` (little-endian):
  ```
  "SGG1", uint32 count
  count × ( uint16 nameLen, nazwa UTF-8, uint64 n, float64[n] )
  ```
  `subgen::test::Golden::load(file)[name]` → `std::span<const double>`; katalog z definicji `SUBGEN_GOLDEN_DIR`.
- `subgen::test::AllocGuard` — liczy wywołania globalnego `operator new` w zakresie (operator nadpisany w bibliotece wsparcia testów).
- `proto/tools/export_golden.py --out tests/golden [--check]`:
  - jedna funkcja `export_<moduł>()` na zadanie;
  - `--check` porównuje bajtowo z plikami w repo;
  - budżet katalogu ≤ 3 MB.

- [ ] **Step 1: Testy (czerwone).**
  - Python: `test_xorshift_noise_known_values` (pierwsze 4 wartości dla seed 1 wpisane w test), `test_sgg_roundtrip`, `test_export_check_is_deterministic`, `test_golden_budget_under_3_mb`.
  - C++ `[support]`:
    - `fft roundtrip within 1e-12 (N = 8..65536)`;
    - `fft matches a naive DFT for N = 64 within 1e-12`;
    - `peakFreq finds 40.3 Hz within 0.01 Hz and amplitude within 1 %`;
    - `thdDb of a sine with 1 % third harmonic is -40 +- 0.2 dB`;
    - `C++ generators match the Python excerpts in signals.sgg within 1e-12` (sine, kick, bass808, twoTone, square, xorshiftNoise, bandNoise; po 4096 próbek);
    - `AllocGuard counts a vector allocation`.
- [ ] **Step 2: Uruchomić** → oczekiwane: brak modułu w Pythonie, błąd kompilacji w C++.
- [ ] **Step 3: Implementacja** i eksport `signals.sgg`.
- [ ] **Step 4: Zielono.**
  - `ctest --preset dev`;
  - `PYTHONPATH=proto .venv/bin/python -m unittest discover -s proto/tests -t proto` (251 dotychczasowych testów + nowe);
  - `export_golden.py --check`.
- [ ] **Step 5: Commit** `test: measurement helpers, test signals, golden-data format and allocation guard`.

### Task 5: Filtry TPT, projekty BW/LR/Bessel, kształty Cytomic (T-FILT-01…04)

**Files:**
- Create: `src/dsp/Svf.h/.cpp`, `src/dsp/FilterDesign.h/.cpp`, `src/dsp/FilterCascade.h/.cpp`, `tests/unit/test_filters.cpp`, `tests/golden/filters.sgg`.
- Modify: `export_golden.py` (`export_filters`).

**Interfaces:**
- **Sekcje SVF:**
  - `enum class SvfMode { LowPass, HighPass, BandPass, Bell, LowShelf, HighShelf }`;
  - `struct SvfCoeffs { double g, k, a1, a2, a3, m0, m1, m2; }`;
  - `SvfCoeffs makeSvf(SvfMode, double fc, double q, double gainDb, double fs)` — kształty Cytomic z SPEC §7.2;
  - `class SvfSection { void set(const SvfCoeffs&); double process(double); void reset(); std::complex<double> response(double f, double fs) const; }`;
  - `struct OnePoleCoeffs { double g; bool highPass; }`, `class OnePoleSection` — TPT 1. rzędu, LP/HP.
- **Projekty:**
  - `enum class FilterFamily { Butterworth, LinkwitzRiley, Bessel }`, `enum class FilterKind { LowPass, HighPass }`;
  - `struct CascadeDesign { std::array<SvfCoeffs, 8> sos; int numSos; bool hasFirstOrder; OnePoleCoeffs first; }`;
  - `CascadeDesign designFilter(FilterFamily, FilterKind, int order, double fc, double fs)` — bez alokacji:
    - Q Butterwortha = 1/(2·sin((2i − 1)π/(2n)));
    - LR = dwa Butterworthy o połowie rzędu (LR2 = 2×BW1);
    - Bessel rzędów 2/4/8 z tablic `proto/subgen_proto/svf.py: design_bessel` (norm 'mag');
  - `int slopeToOrder(int dbPerOct)`.
- **Kaskada:** `class FilterCascade { void set(const CascadeDesign&); double process(double); void process(std::span<double>); void reset(); std::complex<double> response(double f, double fs) const; double groupDelaySeconds(double f, double fs) const; }`.

- [ ] **Step 1: Golden `filters.sgg` z SciPy.**
  - Funkcje: `butter`, `bessel(norm='mag')`, `sosfreqz`, `group_delay`.
  - Siatka:
    - fs: 44100, 48000, 96000, 192000;
    - fc: 15, 22, 30, 60, 120, 500 Hz;
    - filtry: BW 2/4/8/16, LR 4/8/16, Bessel 2/4/8; LP i HP;
    - 64 częstotliwości log od 5 Hz do 0,45·fs.
  - Zapisywane wielkości: |H| [dB] i opóźnienie grupowe [s].
- [ ] **Step 2: Testy (czerwone)** `[filters]`:
  - `T-FILT-01 Butterworth and LR match SciPy within 0.01 dB from 5 Hz to 0.45 fs` i `group delay at fc within 0.5 %`;
  - `T-FILT-02 Cytomic bell and shelves match RBJ within 0.05 dB (10 Hz-20 kHz, -18..18 dB)` (RBJ analitycznie w teście) i `0 dB shape is identity within 1e-12`;
  - `T-FILT-03 cutoff modulated 15-500 Hz (10 Hz sine and random) stays finite with |y| <= 4 max|x|`;
  - `T-FILT-04 Bessel matches SciPy within 0.01 dB` i `group-delay variation to fc/2 <= 1 % (orders 4, 8), <= 2.5 % (order 2)`;
  - `K-01 Butterworth order 3 has Q = 1 and a first-order section`;
  - `processed sine gain equals response() within 0.001 dB`;
  - `designFilter and process do not allocate`.

  Uruchomić → błąd kompilacji.
- [ ] **Step 3: Implementacja** — TPT SVF Cytomic: `g = tan(π·fc/fs)`, `a1 = 1/(1 + g(g + k))`, `a2 = g·a1`, `a3 = g·a2`.
- [ ] **Step 4: Zielono**; commit `feat(dsp): TPT filters, Butterworth/LR/Bessel designs and Cytomic shapes`.

### Task 6: Obwiednie, wygładzanie, linie opóźniające

**Files:**
- Create: `src/dsp/Envelope.h`, `src/dsp/Smoother.h`, `src/dsp/DelayLine.h`, `tests/unit/test_envelope.cpp`, `tests/golden/envelope.sgg`.

**Interfaces:**
- **Obwiednie:**
  - `class PeakFollower { void prepare(double fs, double attackMs, double releaseMs); double process(double x); void reset(); }` — jak `envelope.peak_follower`;
  - `class RmsFollower { void prepare(double fs, double tauMs); double process(double x); void reset(); }`;
  - `class SlidingMax { void prepare(int maxWindow); void setWindow(int w); double process(double x); void reset(double initial = 0.0); }` — przyczynowe maksimum z ostatnich w próbek, kolejka monotoniczna w buforze pierścieniowym; próbki sprzed startu liczą się jako `initial` (limiter: −1, czyli wzmocnienie 1); jak `envelope.sliding_peak(x, w, initial)`;
  - `class SlidingMeanSquare { void prepare(int maxWindow); void setWindow(int w); double process(double x); }` — suma bieżąca, przeliczana od nowa co w próbek (bez dryfu).
- **Wygładzanie** — wspólne API `reset(double fs, double timeMs, double value)`, `setTarget`, `next`, `current`, `isSmoothing`:
  - `LinearSmoother` (L20);
  - `GainSmoother` (G20) — multiplikatywnie; cel 0,0 osiągany dokładnie (poniżej −120 dB przejście liniowe do 0);
  - `LogSmoother` (F20).
- **Opóźnienie:** `class DelayLine { void prepare(int maxDelay); void setDelay(int samples); double process(double); void reset(); }`.

- [ ] **Step 1: Testy (czerwone)** `[envelope]`:
  - `golden followers and sliding max on a kick and xorshift noise within 1e-12`;
  - `SlidingMax equals brute force on 1e5 random samples with window changes`;
  - `SlidingMax after reset(1.0) on zeros gives 1.0 for the first w - 1 samples, then 0.0`;
  - `SlidingMeanSquare drifts less than 1e-12 after 1e7 samples`;
  - `GainSmoother reaches its target within 20 ms (1e-3) and exactly 0.0 after Off`;
  - `DelayLine delays exactly`;
  - `process paths do not allocate`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(dsp): envelope followers, sliding max, smoothers and delay lines`.

### Task 7: Wspólne elementy silników — interfejs, pasmo S3, aktywność S4, bramka pewności, obwiednia

**Files:**
- Create: `src/core/Quality.h`, `src/engines/Engine.h`, `src/engines/SourceBand.h/.cpp`, `src/engines/Activity.h/.cpp`, `src/engines/NoteTracker.h/.cpp`, `src/engines/ConfidenceGate.h`, `src/engines/EnvelopeShaper.h`, `tests/unit/test_engine_common.cpp`, `tests/golden/notes.sgg` — czasy granic nut (detect, begin, end) dla 5 uderzeń SIG-03 przez filtr zakresu: w ciszy, z szumem pasmowym (`bandNoise`) −60 dBFS i co 0,45 s z ogonami T60 1 s; wejście test odtwarza przez `Signals` i `SourceBand`.
- Modify: `export_golden.py` (`export_notes`).

**Interfaces:**
- **Jakość i lookahead:** `enum class Quality { Live, Mix, Print }` oraz funkcje zwracające liczbę próbek, zaokrągloną z ms·fs/1000:

  | Funkcja | Live | Mix | Print |
  |---|---|---|---|
  | `engineLookaheadSamples(Quality, double fs)` | 0 | 45 ms | 55 ms |
  | `compLookaheadSamples` | 0 | 0 | 5 ms |
  | `limiterLookaheadSamples` | 0 | 2 ms | 5 ms |

- **Typy pomocnicze w `Engine.h`:** `enum class KeyMode { None, Detect, Trigger }`, `enum class PhaseGen { Locked, Free, Reset }`.
- **`struct EngineControl`** — wartości fizyczne po przeliczeniu parametrów:
  - wspólne: `sub1Gain`, `sub2Gain` (liniowe, Off = 0), `shape` [0, 1], `follow`, `envAttackMs`, `envReleaseMs`, `dynamics` [−1, 1], `confGate` [0, 1], `baseFixed`, `baseFreqHz`, `rangeLoHz`, `rangeHiHz`, `rangeSlope`, `detThreshDb`, `keyMode` {None, Detect, Trigger};
  - TRACK: `trkSpeed` [0, 1], `trkGlideMs`, `phaseGen` {Locked, Free, Reset};
  - TRIGGER: `trigThreshDb`, `trigHoldMs`, `trigLockoutMs`, `trigFStartHz`, `trigPitchTimeMs`, `trigAttackMs`, `trigDecayMs`, `trigPhaseDeg`, `trigVel` [0, 1], `trigLegato`;
  - HARMONIC/HYBRID: `harmAmount` [0, 1], `harmBalance` [−1, 1], `harmMax`, `hybDivide`, `hybHarmonic`.
- **Interfejs silnika:** `class Engine { virtual void prepare(double fs, int maxBlock, int lookahead) = 0; virtual void reset() = 0; virtual void setControl(const EngineControl&) = 0; virtual void process(std::span<const double> source, std::span<const double> key, std::span<double> out) = 0; virtual double subFrequencyHz() const = 0; virtual double confidence() const = 0; virtual ~Engine() = default; }`. Wyjście `out` = `sub1Gain·Sub1 + sub2Gain·Sub2`, opóźnione o `lookahead` próbek względem `source` (SPEC §6.1, §4.4).
- **Pasmo S3:** `class SourceBand { void prepare(double fs); void setRange(double loHz, double hiHz, int slopeDb); void setBandpass(double centreHz); double process(double); double groupDelayMs(double fHz) const; }` — Butterworth TPT, min-phase; tryb Fixed: ±½ oktawy, 24 dB/okt.
- **Aktywność S4:** `class Activity { void prepare(double fs); void setThreshold(double db); double process(double xb); bool active() const; double ePeak() const; double eRms() const; }`:
  - `e_pk`: atak 1 ms, release 50 ms; `e_rms`: 20 ms;
  - histereza 3 dB, hold 20 ms;
  - zwraca wzmocnienie wygaszania (release 20 ms).
- **Granice nut (SPEC §5.4, R2):** `struct NoteEvent { bool start, end; double detect; std::int64_t begin; }`; `class NoteTracker { void prepare(double fs); void setThreshold(double db); NoteEvent process(double xb, bool active); void reset(); }` — semantyka `activity.note_starts` / `note_ends`, czasy w próbkach od `reset()`:
  - start = narastające zbocze `active` albo atak: szybka obwiednia (0,2/15 ms) ≥ 12 dB ponad √2·RMS 50 ms sprzed 5 ms i ≥ √2·`det_thresh`; `detect` ułamkowe (interpolacja przecięcia);
  - `begin` = ostatnia próbka „cicha” (szybka obwiednia ≤ 1 dB ponad to odniesienie; cyfrowa cisza jest cicha) + 1, nie później niż `detect`;
  - blokada 50 ms wspólna dla obu rodzajów startu; koniec = opadające zbocze `active`.
- **Bramka pewności (SPEC §6.1):** `class ConfidenceGate { void prepare(double fs); void setGate(double confGate01); double process(double c, bool provisional); bool open() const; }` — semantyka `divide.gate_states` i `apply_confidence_gate`:
  - otwiera przy c ≥ hi = 0,2 + 0,6·g, zamyka poniżej max(hi − 0,2; hi/2);
  - pewność tymczasowa trzyma bramkę otwartą; gdy się kończy, pierwsza prawdziwa wartość musi osiągnąć hi, inaczej bramka zamyka się z release 10 ms;
  - wzmocnienie: atak 5 ms, release 50 ms.
- **Obwiednia:** `class EnvelopeShaper { void prepare(double fs); void set(bool follow, double attackMs, double releaseMs, double dynamics); double process(double level, double ePeak, double eRms, bool onset); }`. Makro `dynamics` jest [F3]; początkowe odwzorowanie (Ruling, strojenie odsłuchem w Fazie 5):
  - release × 4^d;
  - atak × (1 + 3·max(0, d));
  - przy d < 0 akcent `(−d)·max(0, e_pk − e_rms)` przez 5 ms po onsecie;
  - d = 0 → tożsamość.

- [ ] **Step 1: Testy (czerwone)** `[engines-common]`:
  - `SourceBand equals the FilterCascade designs within 1e-12`;
  - `Activity opens at the threshold and closes 3 dB lower after a 20 ms hold (+-10 %)`;
  - `NoteTracker: one start per SIG-03 hit (20/20) from silence, over band noise -90 and -60 dBFS and 31 Hz hum -70 dBFS: begin within [t0 - 1 sample, t0 + 3 ms], detect - begin <= 10 ms`;
  - `NoteTracker: hits every 0.45 s over T60 1 s tails: 20/20 starts, begin within t0 + 5 ms`;
  - `NoteTracker: a sustained 808 note starts once; in-band noise (5 seeds, 3 s) starts <= 3 notes; after digital silence begin is the first sound +- 1 sample; ends fall between separated hits`;
  - `NoteTracker golden: detect within 1e-6 samples, begin and ends exact`;
  - `ConfidenceGate reproduces the gate_mask sequences for conf_gate 0, 50 and 100` (port testów `gate_mask`, D8 I1);
  - `ConfidenceGate: a provisional opening stays open only if the first real confidence reaches hi; a rejected one releases with 10 ms`;
  - `ConfidenceGate gain: attack 5 ms, release 50 ms (one release time constant: 0.368 +- 0.02)`;
  - `EnvelopeShaper with dynamics 0 is the plain follower`;
  - `lookahead at 48 kHz: Mix 2160, Print 2640 samples`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): engine interface, source band, activity, note boundaries and confidence gate`.

### Task 8: DIVIDE (T-DIV-01…09)

**Files:**
- Create: `src/engines/Divide.h/.cpp`, `tests/unit/test_divide.cpp`, `tests/golden/divide.sgg` — sinus 55 Hz od ciszy, jedno uderzenie SIG-03, nuta SIG-04 i dwa uderzenia z końcem nuty między nimi, w Live i w Mix; tablice `y1`, `y2`, `f_est`, `conf`, `provisional` i wyjście z bramką, w oknach ≤ 8192 próbek wokół startów nut, łącznie ≤ 1 MB.
- Modify: `proto/subgen_proto/divide.py` (+ `conf_as_known`), `proto/subgen_proto/render.py` (+ `divide_sub_causal`), `proto/tests/test_divide.py`, `proto/tests/test_render.py`, `export_golden.py` (`export_divide`).

**Interfaces:**
- `class Divide final : public Engine`, plus dla testów `double lastPeriodSamples() const`, `bool gateOpen() const` i `int resetCount() const`.
- Algorytm — SPEC §6.2 w wersji 0.3 (implementacja wzorcowa `proto/subgen_proto/divide.py`, `render.divide_sub`):
  - pętla na próbkę; θ = max(0,05·e_pk, −90 dBFS), w Live 0,03;
  - ułamkowe przejścia przez zero; s1, s2, `arm2_pending`;
  - dzielnik pracuje tylko w nucie (`NoteTracker`, zadanie 7), poza nią jest zamrożony;
  - reset s1 = s2 = +1, `arm2_pending`, uzbrojenia i historii okresów: po końcu nuty — przy najbliższym przejściu przez zero; na początku nuty w Mix/Print — przy ostatnim przejściu przez zero w [⌈detect⌉ − E, max(begin, detect − E)], a bez takiego przejścia — w max(begin, ⌈detect⌉ − E); w Live — przy wykryciu, z rampą 0,5 ms. Prototyp w gałęzi bez przejścia bierze ⌊max(begin, detect − E)⌋, czyli o próbkę wcześniej, gdy begin < detect − E. To gałąź rzadka (w sondach z riserem i wolnym atakiem begin wypadał 1–10 ms przed wykryciem), więc dane złote jej nie obejmują (Ruling);
  - f_est = mediana 3 okresów, potem filtr 5 Hz;
  - pewność z trendu (mediana 5; pierwsza reszta po trzech okresach); od resetu na początku nuty do pierwszej reszty pewność tymczasowa 1; bramka z zadania 7, w Mix/Print czytająca pewność E naprzód;
  - wygładzanie LP4 przy 0,87·f_est·2^(5·shape) (Sub −2: f_est/2), najwyżej 0,45·fs, z poślizgiem 5 ms; od każdego resetu do pierwszej estymaty f_est = `range_hi`;
  - Constant przez `SlidingMax` z oknem 1,25 okresu, w granicach `range`.
- **Dwie osie czasu (Ruling — prototyp liczy offline):**
  - przejścia przez zero, okresy, pewność i granice nut liczy się na bieżącym `x_b`; po wykryciu startu stan wraca do punktu resetu (≥ detect − E), a przejścia od tego punktu (≤ E próbek z linii opóźniającej) przelicza się od nowa;
  - przełączenia s1/s2, wygładzanie i Constant pracują na osi opóźnionej o E, ze zdarzeniami o bezwzględnym czasie. Późniejsze wykrycie nie zmienia zdarzeń sprzed bieżącej pozycji tej osi, więc `y1`, `y2` i `f_est` są równe tablicom `divide.divide`;
  - bramka w Mix/Print czyta pewność z osi bieżącej, czyli taką, jaką wtyczka zna w chwili odczytu. Prototyp offline zna pewność tymczasową już w oknie [punkt resetu, wykrycie) przed każdą nutą. Dlatego wzorcem wyjścia z bramką jest `render.divide_sub_causal`. W Live oba wzorce są równe.
- **Wzorzec przyczynowy (Python):**
  - `divide.conf_as_known(x_b, fs, *, live, lookahead_s, thresh_db=-55.0) -> tuple[np.ndarray, np.ndarray]` — pewność i flaga tymczasowa znane w chwili t. Starty nut przetwarza się od ostatniego do pierwszego: próbki [m − E, m), gdzie m = ⌈detect⌉, pochodzą z `divide` na `x_b[:m]`;
  - `render.divide_sub_causal(x_b, fs, *, shape_pct=0.0, conf_gate_pct=50.0, quality="mix")` — `divide_sub` z bramką czytającą `conf_as_known`.

- [ ] **Step 1: Wzorzec przyczynowy (TDD w `proto/`).** Testy (czerwone):
  - `test_conf_as_known_equals_offline_outside_the_windows_before_detections`;
  - `test_conf_as_known_equals_offline_in_live`;
  - `test_divide_sub_causal_keeps_the_kick_onset` (T-DIV-08: ≥ 50 % w Mix i Print).

  Potem implementacja, zielony pakiet Pythona i eksport `divide.sgg`.
- [ ] **Step 2: Testy C++ (czerwone)** `[divide]`:
  - `T-DIV-01 Sub-1 and Sub-2 frequency within 0.1 % (30-200 Hz at 44.1/48/96 kHz)`;
  - `T-DIV-02 3f/2 <= -18 dB with shape 0`;
  - `T-DIV-03 default output (shape 0, gate), Mix and Live: 20 SIG-03 hits in silence, over band noise -90/-80/-70/-60 dBFS and 31 Hz hum -70 dBFS: first-lobe sign 20/20, waveform correlation >= 0.99`;
  - `T-DIV-03 hits every 0.45 and 0.5 s over T60 1 s tails: native Sub-1 first lobe 20/20, output hits 1..19 share one sign; Live: the sign is consistent from 1 ms after each reset`;
  - `T-DIV-03 a sustained 808 note is not reset inside; 40 kicks -24..-3 dBFS: native polarity 40/40, correlation >= 0.99`;
  - `T-DIV-04 in-band SNR 10 dB: period error < 2 %; at 3 dB the gate is open <= 10 % of the time`;
  - `T-DIV-05 chord from silence, Live and Mix: <= source - 40 dB after 100 ms; in-band noise: gate open <= 3 %, sub >= 12 dB below the noise`;
  - `T-DIV-06 shape 100: step <= the input step, energy above 2 kHz <= -60 dB re peak`;
  - `T-DIV-07 Constant -12 dBFS +- 0.5 dB for -40..-6 dBFS inputs; Follow proportional`;
  - `T-DIV-08 first 40 ms of a SIG-03 hit: gated sub energy >= 50 % of the ungated (Live, Mix, Print); Mix: gate gain at the first lobe 0.99-1.01, Live < 0.9`;
  - `T-DIV-09 shape 25/50/75/100 % on a 55 Hz sine: f/2 level within +-0.5 dB of shape 0; shape 100: 3f/2 to f/2 ratio within +-1 dB of the native`;
  - `confidence is provisionally 1 from the note start (55 Hz sine: conf >= 0.5 from 2 ms on)`;
  - `each note starts the smoothing low-pass from 0.87 range_hi`;
  - `a -85 dBFS sine is not divided at det_thresh -55 dBFS but is at -90 dBFS; below the -90 dBFS arming floor nothing is divided`;
  - `golden: y1, y2, f_est, conf and provisional within 1e-9 of divide.divide (Live, Mix); the gated output within 1e-9 of render.divide_sub_causal`;
  - Review Focus 3: `clipped square, DC offset and an onset at the waveform peak still divide and never produce NaN`, `+12 dBFS stays finite and divides`.
- [ ] **Step 3:** uruchomić → błąd kompilacji.
- [ ] **Step 4:** implementacja.
- [ ] **Step 5:** zielono; commit `feat(engines): DIVIDE octave divider with note boundaries and a causal gate`.

### Task 9: Detektor wysokości MPM i prior (T-TRK-01)

**Files:**
- Create: `src/engines/PitchMpm.h/.cpp`, `src/engines/PitchPrior.h`, `tests/unit/test_pitch.cpp`, `tests/golden/pitch.sgg` — ścieżki f0/pewność dla nuty SIG-04 i SIG-05.

**Interfaces:**
- **Detektor:** `struct PitchFrame { double timeSamples, f0Hz, confidence; }`; `class PitchMpm { void prepare(double fs, double fMaxHz); void setMinFrequency(double rangeLoHz); int process(std::span<const double> xb, std::span<PitchFrame> out); void reset(); }` — ramka co 2 ms:
  - decymacja całkowita D = ⌊fs/2000⌋ z filtrem AA Butterworth 8. rzędu przy 800 Hz;
  - okno W = ⌈2,5·fs_d/max(range_lo, 20)⌉;
  - ramki świadome onsetu: próbki poniżej 1 % szczytu ramki pomijane, lag ≤ połowa reszty;
  - NSDF liczone bezpośrednio; szczyt kluczowy 0,9; parabola;
  - pewność = clip((jasność − 0,6)/0,4).
- **Prior:** `class PitchPrior { double update(double f0, double conf); void reset(); }` — semantyka `track.apply_prior`:
  - okno 0,25;
  - 3 zgodne ramki, także na starcie;
  - ramki z c < 0,5 pomijane, 5 takich ramek z rzędu zeruje stan.

- [ ] **Step 1: Testy (czerwone)** `[pitch]`:
  - `T-TRK-01 SIG-01/04/05: median error <= 0.5 % and 0 octave errors (conf >= 0.5 and |err| > 600 cents) at 44.1 and 48 kHz`;
  - `44.1 kHz decimates by 22 (fs_d = 2004.545 Hz)`;
  - `decimation is causal: silence before a note stays exactly 0 after the anti-alias filter` (D8 M13);
  - `confidence reaches 0.5 within 2 periods + 4 ms of a note after silence (30-120 Hz)`;
  - `band-limited pink noise has median confidence <= 0.1`;
  - `a lone outlier cannot seed the track; an octave jump needs 3 agreeing frames`;
  - `golden f0/confidence tracks within 1e-6 relative`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): MPM pitch detector with onset-aware frames and continuity prior`.

### Task 10: TRACK — oscylator, tryby fazy, PLL, lookahead (T-TRK-02…04)

**Files:**
- Create: `src/engines/Pll.h`, `src/engines/Track.h/.cpp`, `tests/unit/test_track.cpp`.

**Interfaces:**
- **PLL:** `class Pll { void prepare(double fs); void setBandwidth(double hz, double zeta = 0.707); void seed(double fHz, double phaseRad); double process(double x, double amplitude); double frequencyHz() const; double phase() const; }` — 2. rzędu; pasmo = −3 dB zamkniętej pętli; detektor fazy `−x·sin(φ̂)/(A/2)` jak w prototypie.
- **Silnik:** `class Track final : public Engine`.
- Analiza i synteza na różnych osiach czasu:
  - PitchMpm + PitchPrior pracują na wejściu bieżącym;
  - synteza i amplituda — na osi opóźnionej o `lookahead`: wysokość i pewność czytane z historii ramek w chwili t + lookahead, amplituda bieżąca.
- Oscylator: Sub −1 = f0/2, Sub −2 = f0/4; kształt `(1 − shape)·sin + shape·tri`.
- `trk_speed` → wygładzanie log f ze stałą τ = 80·(5/80)^speed ms (Ruling: bez osobnego progu zmiany).
- `trk_glide` → portamento liniowe w log f.
- Tryby fazy:
  - Locked: PLL 5 Hz, przełączane na 20 Hz, gdy f0 zmienia się o więcej niż 1 półton w 20 ms ([F3], Ruling); φ_sub = φ_PLL/2 + c0 z regułą polaryzacji;
  - Free;
  - Reset — domykany w zadaniu 11 (onset).
- Fixed: dron przy `base_freq` bramkowany aktywnością.

- [ ] **Step 1: Testy (czerwone)** `[track]`:
  - `T-TRK-02 glide 80->40 Hz in 200 ms: tracking lag <= 50 ms (W = 83.5 ms)`;
  - `T-TRK-03 sub onset within +-2 ms of the source onset in Mix (notes >= 55 Hz) and Print (notes >= 41.2 Hz); Live reports its delay`;
  - `T-TRK-04 Locked steady-state phase error <= 15 deg rms, clean and at in-band SNR 10 dB`;
  - `triangle aliasing < -90 dB for f <= 100 Hz at 44.1 kHz`;
  - `Fixed mode is a gated drone at base_freq`;
  - `amplitude is not read ahead: the sub does not start before the note`;
  - `no pitch before the first valid frame: the oscillator stays silent until the first estimate` (D8 M13).
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): TRACK oscillator with PLL phase lock and lookahead`.

### Task 11: TRIGGER (T-TRIG-01…04)

**Files:**
- Create: `src/engines/OnsetDetector.h/.cpp`, `src/engines/Trigger.h/.cpp`, `tests/unit/test_trigger.cpp`, `tests/golden/trigger.sgg` — głos przy ustawieniach domyślnych, 3 uderzenia.

**Interfaces:**
- **Detektor onsetu:** `class OnsetDetector { void prepare(double fs); void set(double threshDb, double holdMs, double lockoutMs); std::optional<double> process(double x); void reset(); }` — zwraca ułamkowe przesunięcie onsetu w (−1, 0];
  - szybka obwiednia 0,2/15 ms;
  - histereza 3 dB, hold, lockout;
  - interpolacja przecięcia progu.
- **Silnik:** `class Trigger final : public Engine` — wzory SPEC §6.4:
  - faza w postaci zamkniętej (całka wykładniczego glissanda);
  - atak liniowy t_a = max(`trig_attack`, 0,5 ms), potem T60 (SPEC §6.4);
  - prędkość `clamp((peak/0,5)^vel, 0, 1,5)`, peak `x_b` w 20 ms po onsecie;
  - Follow: `e_rms` (20 ms) przez bieżące maksimum od onsetu (przyczynowo), z tolerancją 1 dB, potem tylko malejące;
  - Restart z crossfade 3 ms; Legato przenosi fazę i amplitudę;
  - Sub −2 = f/2;
  - klucz z `key` przy `keyMode == Trigger`;
  - Auto: f_end = f0/2 z PitchMpm w chwili wyzwolenia, ograniczone do 20–100 Hz;
  - w Mix/Print głos startuje w ułamkowym czasie onsetu na osi opóźnionej o `lookahead`.
- Domknięcie TRACK Reset: onset ustawia fazę na 0.

- [ ] **Step 1: Testy (czerwone)** `[trigger]`:
  - `T-TRIG-01 SIG-03: onset error <= 1 sample (Mix/Print) and <= 2 ms (Live), no double triggers, constant bias -0.486 +- 0.05 sample`;
  - `T-TRIG-02 hits 1..99 identical within -120 dB; block sizes {1, 7, 64, 512} give identical output`;
  - `T-TRIG-03 time-domain f(t) within 0.5 % and A(t) within 0.1 dB of the formulas`;
  - `T-TRIG-04 Restart click (max 2nd difference in +-1 ms) <= 1e-3; Legato carries phase and amplitude within 1e-12; attack 0 at a 90 deg start phase: max step <= 0.06`;
  - `Follow is causal: a prefix of the input gives the same prefix of the output (1e-12)`;
  - `TRACK Reset: phase 0 +- 1 deg at the onset`;
  - Review Focus: `hits closer than the lockout trigger once`, `overlapping retriggers stay finite without clicks`;
  - `golden voice within 1e-9`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): TRIGGER transient-triggered sub`.

### Task 12: HARMONIC (T-HARM-01/02)

**Files:**
- Create: `src/engines/Harmonic.h/.cpp`, `tests/unit/test_harmonic.cpp`, `tests/golden/harmonic.sgg`.

**Interfaces:**
- `class Harmonic final : public Engine`:
  - obwiednia: `SlidingMax` z oknem 1,5 okresu `range_lo` (Fixed: dolnej krawędzi pasma), potem 5/60 ms;
  - `x_n = clamp(x_b/max(e, −80 dBFS), −1, 1)`;
  - wielomiany Czebyszewa rekurencyjnie do K = `harm_max`;
  - wagi `w_k = amount·c_k/k`, `c_k = 1 ∓ b`;
  - HP 15 Hz Butterworth 2. rzędu;
  - `subFrequencyHz()` = f0 źródła (Boost follow używa wtedy `boost_freq`).

- [ ] **Step 1: Testy (czerwone)** `[harmonic]`:
  - `T-HARM-01 unit sine: amplitude of harmonic k equals w_k within 0.5 dB (K = 3/4/5, balance -100/0/100); DC <= -80 dBFS`;
  - `T-HARM-02 SIG-17 intermodulation share is reported (informational)`;
  - `-90 dBFS input gives <= -110 dBFS output`;
  - `golden within 1e-9`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): HARMONIC Chebyshev waveshaper with max-hold envelope`.

### Task 13: HYBRID i EngineHost — przełączanie, rozgrzewanie, opóźnienie E (T-HYB-01, T-ENG-01)

**Files:**
- Create: `src/engines/Hybrid.h/.cpp`, `src/engines/EngineHost.h/.cpp`, `tests/unit/test_engine_host.cpp`.

**Interfaces:**
- `enum class EngineType { Divide, Track, Trigger, Harmonic, Hybrid }`.
- `class EngineHost { void prepare(double fs, int maxBlock, Quality q); void setEngine(EngineType); void setControl(const EngineControl&); void process(std::span<const double> source, std::span<const double> key, std::span<double> out); double subFrequencyHz() const; double confidence() const; bool gateOpen() const; int latencySamples() const; }`:
  - wszystkie silniki przygotowane w `prepare`;
  - latencja E = `engineLookaheadSamples(q, fs)`, wspólna dla wszystkich silników;
  - przełączenie: crossfade równej mocy 30 ms; silnik wchodzący rozgrzewany 200 ms źródła z bufora pierścieniowego;
  - Hybrid = `hyb_divide·Divide + hyb_harmonic·Harmonic` ze wspólnym pasmem, aktywnością i bramką pewności.

- [ ] **Step 1: Testy (czerwone)** `[engine-host]`:
  - `T-HYB-01 Hybrid equals the weighted sum of independent engines (null <= -120 dB)`;
  - `T-ENG-01 switching every 0.5 s over all engines on SIG-04: no click (max 2nd difference in the crossfade <= the steady maximum + 1e-3), latency constant`;
  - `DIVIDE output is delayed by exactly E in Mix and Print`;
  - `TRACK switched in on a sustained note is confident within 4 ms of the switch`;
  - `re-prepare at a new sample rate clears every engine`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(engines): engine host with crossfaded switching and HYBRID`.

### Task 14: Drive/Saturate — kształty, ADAA, oversampling JUCE, Auto Gain (T-DRV-01…05, T-ALIAS-01)

**Files:**
- Create: `src/chain/DriveShapes.h`, `src/chain/AutoGainTable.h/.cpp`, `src/chain/Drive.h/.cpp`, `tests/unit/test_drive.cpp`, `tests/golden/drive.sgg` — poziomy harmonicznych wzorca 16× (5 charakterów × drive 25/50/100 %, 55 Hz, −12 dBFS; Fold także przy +6 dBFS) i wybrane G_c z Załącznika A.

**Interfaces:**
- **Kształty** (SPEC §7.3; `b_mode` Tube 0,35; bias addytywny):
  - `enum class DriveMode { Soft, Tube, Tape, Fold, Hard }`;
  - `double shape(DriveMode, double x, double bias)`;
  - `double antiderivative(DriveMode, double x, double bias)`.
- **Parametry:** `struct DriveParams { double drivePct; DriveMode mode; double balance; double toneHz; bool autoGain; double fSub1Hz; }`.
- **Tabela Auto Gain:** `class AutoGainTable`.
  - Liczona raz na proces (`std::call_once` przy pierwszym `prepare`).
  - Siatka: drive 0…100 co 2 %, balance −100…100 co 20, 5 trybów.
  - Każda komórka: |c_k|² dla k = 1…64 z 2048-punktowego DFT jednego okresu `y_α`.
  - `double compensationDb(DriveMode, double drivePct, double balance, double fSub1Hz, double toneHz, double alpha, double fs) const`:
    - interpolacja dwuliniowa |c_k|²;
    - `RMS_AC² = ½·Σ|c_k|²·|T(k·f_sub1)|²`, gdzie `T = (1 − α) + α·H_tone` (dokładnie dla dyskretnego LP 2. rzędu).
- **Stopień:** `class Drive { void prepare(double fs, int maxBlock, Quality q); void setParams(const DriveParams&); void process(std::span<double>); int latencySamples() const; }`:
  - pre-gain 30 dB·drive/100; odniesienie 12 dB; α = min(1, drive/5);
  - Fold (SPEC §7.3, R3): wejście nieliniowości `g·x_ref` mnożone przez min(1, 2,0/env), env = `SlidingMax` |x_ref| w oknie max(10 ms, 1,5/f_sub1), potem release 50 ms (wzrost natychmiastowy); do −6 dBFS bit w bit bez zmian;
  - ADAA1 z tolerancją 1e-5;
  - Tone `(1 − α)·y + α·LP(y)`; HP 5 Hz po stopniu;
  - `juce::dsp::Oversampling<double>`: Mix 2× `filterHalfBandPolyphaseIIR`, Print 4× `filterHalfBandFIREquiripple`, max quality — oba przygotowane w `prepare`;
  - przy drive = 0 ścieżka czystego opóźnienia o tę samą latencję (bit-exact), przejście z crossfade 20 ms;
  - latencja = ceil(latencja oversamplera). Wartości dla 44,1/48/96/192 kHz zapisać w dzienniku jako zmierzone L_os do SPEC §4.4.

- [ ] **Step 1: Testy (czerwone)** `[drive]`:
  - `T-DRV-01 harmonics >= -60 dB within 1 dB of the 16x reference (Live, Mix, Print)`;
  - `T-DRV-02 ADAA lowers non-harmonic power by >= 6 dB vs naive (Hard 100 %, 81 Hz, Live)`;
  - `T-DRV-03 Auto Gain within +-0.5 dB: all modes, drive 5-100 %, balance -100/0/100, Tone 60/500/2000 Hz at f_sub1 30/60/120 Hz`;
  - `T-DRV-04 DC after the 5 Hz high-pass <= -80 dBFS`;
  - `T-DRV-05 drive 0 is bit-exact (delayed by the latency); alpha continuity <= 1e-2 relative per 0.1 % step`;
  - `T-ALIAS-01 31/81/121 Hz, all five modes, drive 25/50/100 %, inputs -6/0/+6/+12 dBFS, 44.1 and 48 kHz: non-harmonic power below 500 Hz <= -80 dB (Live) and <= -90 dB (Mix, Print)` — wartości pełnopasmowe idą do dziennika;
  - `Fold up to -6 dBFS is bit-identical to the uncapped fold`;
  - `AutoGainTable is computed once and process() does not allocate`;
  - `Tube at balance -100 has b_eff 0.85`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): Drive/Saturate with ADAA, oversampling and Auto Gain`.

### Task 15: Kompresor i bramka (T-CMP-01…03, T-GATE-01)

**Files:**
- Create: `src/chain/LevelDetector.h`, `src/chain/Compressor.h/.cpp`, `src/chain/Gate.h/.cpp`, `tests/unit/test_dynamics.cpp`, `tests/golden/dynamics.sgg` — trajektorie GR z prototypu.

**Interfaces:**
- **Detektor:** `enum class Detector { Peak, Rms }`; `class LevelDetector { void prepare(double fs, int maxWindow); void set(Detector, double fSub1Hz); double processDb(double x); }`:
  - okno w = max(10 ms, 1,5/f_sub1), przy f_sub1 ≥ 20 Hz;
  - Peak (domyślny, SPEC §7.4, R4): hold |x| w oknie;
  - RMS: średnia kwadratowa w oknie, potem hold w tym samym oknie.
- **Kompresor:** `struct CompParams { bool on; double threshDb, ratio, kneeDb, attackMs, releaseMs; bool autoRelease; Detector detector; double makeupDb; bool autoMakeup; double fSub1Hz; }`; `class Compressor { void prepare(double fs, int maxBlock, Quality q); void setParams(const CompParams&); void process(std::span<double>); double gainReductionDb() const; int latencySamples() const; }`:
  - krzywa statyczna z kolanem; wygładzanie GR w dB;
  - przy detektorze RMS atak efektywny = max(`comp_attack`, 0,25·T_sub) (R4);
  - auto release = głębsza z dwóch gałęzi: (atak, 50 ms) i (150 ms, 500 ms);
  - auto makeup `0,5·|T|·(1 − 1/R)` [F3];
  - lookahead 5 ms w Print; crossfade on/off 10 ms.
- **Bramka:** `struct GateParams { bool on; double threshDb, rangeDb, holdMs, releaseMs, fSub1Hz; }`; `class Gate`:
  - peak hold w oknie w; histereza 3 dB;
  - zamyka po hold + okno; atak 2 ms; release;
  - wygładzanie w dB nie szybsze niż 0,25·T_sub.

- [ ] **Step 1: Testy (czerwone)** `[dynamics]`:
  - `T-CMP-01 static curve and knee within 0.1 dB`;
  - `T-CMP-02 THD at 6 dB GR <= -60 dB for RMS and Peak (30/40/60/100 Hz x attack 0.1-100 ms x release 10-1000 ms)`;
  - `T-CMP-02 onset: 40 Hz -6 dBFS after silence, threshold -30 dB, ratio 4 (GR 0 -> ~18 dB), energy above 100 Hz in the first 80 ms (Hann): Peak at attack 0.1/1/3 ms <= -50 dB; RMS at 1 ms <= -40 dB and equal to RMS at 6.25 ms`;
  - `T-CMP-03 attack and release 63 % times within 10 % with the detector window added to the release; auto release after 0.05/0.5/3 s of reduction = 64/498/514 ms +- 10 %`;
  - `T-GATE-01 threshold, hold, release and hysteresis within 10 %, no chatter at threshold +- 1 dB`;
  - `off is bit-exact`;
  - `golden GR trajectories within 1e-9`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): ripple-free compressor and gate`.

### Task 16: Limiter (T-LIM-01/02)

**Files:**
- Create: `src/chain/Limiter.h/.cpp`, `tests/unit/test_limiter.cpp`, `tests/golden/limiter.sgg`.

**Interfaces:**
- `struct LimiterParams { bool on; double ceilingDb, releaseMs; bool truePeak; }`.
- `class Limiter { void prepare(double fs, int maxBlock, Quality q); void setParams(const LimiterParams&); void process(std::span<double>); int latencySamples() const; double gainReductionDb() const; }`:
  - Live: `c·tanh(x/c)` z ADAA;
  - Mix/Print — SPEC §7.8: D = round(lookahead·fs); minimum wymaganego wzmocnienia w oknie D + 1 (`SlidingMax` na −g, po resecie wypełniony −1, czyli wzmocnieniem 1); release jednobiegunowy; FIR Hanna o długości D + 1; wejście opóźnione o D;
  - `lim_tp` (Print): wymagane wzmocnienie z próbek nadpróbkowanych 4× (FIR).

- [ ] **Step 1: Testy (czerwone)** `[limiter]`:
  - `T-LIM-01 four stress signals x Live/2 ms/5 ms: |y| <= ceiling + 1e-9`;
  - `T-LIM-01 true peak (Print, lim_tp): 4x-oversampled peak <= ceiling + 0.01 dB with the same interpolator`;
  - `T-LIM-02 40 Hz, 6 dB over, release 100 ms: THD Mix <= -35 dB, Print <= -50 dB`;
  - `latency is exactly round(lookahead fs)`;
  - `a 40 Hz sine below the ceiling passes unchanged from its first sample after reset (within 1e-15, delayed by D)`;
  - `off is bit-exact`;
  - `golden gain trajectory within 1e-9`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): lookahead limiter with a guaranteed ceiling`.

### Task 17: Boost i parametryczny EQ (T-EQ-01)

**Files:**
- Create: `src/chain/Boost.h/.cpp`, `src/chain/Eq.h/.cpp`, `tests/unit/test_eq.cpp`.

**Interfaces:**
- **Boost:** `enum class BoostType { Peak, LowShelf }`; `struct BoostParams { double gainDb; BoostType type; double freqHz; bool follow; double q; double fSub1Hz; bool engineHasPitch; }`; `class Boost { void prepare(double fs); void setParams(const BoostParams&); void process(std::span<double>); }`:
  - środek = f_sub1 przy follow (HARMONIC → `boost_freq`), wygładzany w log (F20);
  - 0 dB → tożsamość bit-exact;
  - zmiana typu z crossfade 20 ms.
- **EQ:** `enum class EqType { Bell, LowShelf, HighShelf }`; `struct EqBand { bool on; EqType type; double freqHz, gainDb, q; }`; `struct EqParams { std::array<EqBand, 5> bands; }`; `class Eq { void prepare(double fs); void setParams(const EqParams&); void process(std::span<double>); double curveDb(double fHz) const; }`:
  - pasma szeregowo 1 → 5;
  - pasmo off lub 0 dB → tożsamość bit-exact;
  - zmiana typu z crossfade 20 ms.

- [ ] **Step 1: Testy (czerwone)** `[eq]`:
  - `T-EQ-01 band responses match the Cytomic formulas within 0.05 dB; off or 0 dB is bit-exact`;
  - `Boost follows f_sub1, log-smoothed over 20 ms`;
  - `Boost 0 dB is bit-exact`;
  - `type change crossfades without clicks`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): frequency boost and 5-band parametric EQ`.

### Task 18: Filtry toru sub i LFE, min-phase (T-LFE-01, T-LFE-07)

**Files:**
- Create: `src/chain/SubFilters.h/.cpp`, `tests/unit/test_subfilters.cpp`.

**Interfaces:**
- `struct SubFilterParams { double hpHz; int hpSlope; double lpHz; int lpSlope; FilterFamily family; bool lfeMode; double lfeHpHz, lfeLpHz; int lfeLpSlope; bool linearPhase; }`.
- `class SubFilters { void prepare(double fs, int maxBlock, Quality q); void setParams(const SubFilterParams&); void process(std::span<double>); int latencySamples() const; double groupDelayMs(double fHz) const; }`:
  - HP + LP min-phase (`FilterCascade`); zmiana rodziny z crossfade 20 ms; Bessel ≤ 48 dB/okt;
  - tryb LFE: `lfe_hp` + pojedynczy LP `lfe_lp`/`lfe_lp_slope`, bez kaskady z `sub_lp`;
  - Linear Phase podłącza zadanie 19.

- [ ] **Step 1: Testy (czerwone)** `[subfilters]`:
  - `responses equal the cascade designs`;
  - `T-LFE-01 white noise: >= 14 dB at 1.5 fc for 24 dB/oct and attenuation below lfe_hp`;
  - `T-LFE-07 displayed group delay at 40 Hz within 2 % of the measured one`;
  - `LFE mode uses one low-pass, not a cascade with sub_lp`;
  - `family change crossfades without clicks`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): sub and LFE high/low-pass filters`.

### Task 19: Linear Phase LP i SPK-6 (T-FILT-06)

**Files:**
- Create: `src/dsp/FirDesign.h/.cpp`, `src/dsp/PartitionedConvolver.h/.cpp`, `src/chain/LinearPhaseLp.h/.cpp`, `tests/unit/test_linear_phase.cpp`, `tests/bench/bench_linear_phase.cpp`.
- Modify: `src/chain/SubFilters.*`.

**Interfaces:**
- **Projekt jądra:**
  - `int linearPhaseTaps(double fs)` = 4096·2^⌈log2(fs/48000)⌉ + 1;
  - `void designLinearPhaseKernel(const std::function<double(double)>& mag, double fs, std::span<double> kernel, double beta = 3.0)` — próbkowanie |H| na siatce ≥ 4N, opóźnienie (N − 1)/2, okno Kaisera, symetryzacja; jak `lfe.linear_phase_fir`;
  - `double kernelErrorDb(std::span<const double> kernel, mag, double fs)` — od 5 Hz do punktu −60 dB.
- **Konwolucja:** `class PartitionedConvolver { void prepare(int maxKernel, int partition = 256); void setKernel(std::span<const double>); void process(std::span<double>); int latencySamples() const; }` — `RealFft` z zadania 4; partycje jednolite P = 256; latencja P.
- **Stopień:** `class LinearPhaseLp { void prepare(double fs, int maxBlock); void requestDesign(const SubFilterParams&); void process(std::span<double>); int latencySamples() const; double errorDb() const; }`:
  - latencja (N_LIN − 1)/2 + 256;
  - jądro projektowane w wątku roboczym;
  - przekazanie przez wstępnie zaalokowane sloty i atomowy indeks;
  - podmiana z crossfade 40 ms.
- L_lin = (N_LIN − 1)/2 + 256 — SPEC 0.3 §4.4, §7.6.
- **Ruling do raportu Fazy 3:** automatyzacja LP w trybie Linear Phase nie jest deterministyczna, bo jądro podmienia się asynchronicznie.

- [ ] **Step 1: Testy (czerwone)** `[linear-phase]`:
  - `T-FILT-06 kernel symmetric exactly`;
  - `error <= 0.1 dB from 5 Hz to the -60 dB point for BW24 120 Hz, BW12 50 Hz, Bessel24 60 Hz and LR24 90 Hz at 48 kHz`;
  - `reported error equals the measured one within 0.01 dB (BW24 at 60 Hz, ~0.33 dB)`;
  - `delay is exactly (N_LIN - 1)/2 + 256`;
  - `kernel swap crossfades without clicks`;
  - `latency does not change with sub_lp_freq or slope`;
  - `convolver matches direct convolution within 1e-12`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4: Zielono i benchmark SPK-6:** stereo, 48 kHz, blok 128, Print + Linear Phase; mediana i p99 czasu względem czasu bloku → do dziennika (budżet całości sprawdza zadanie 29).
- [ ] **Step 5: Commit** `feat(chain): linear-phase low-pass for Print (SPK-6)`.

### Task 20: Narzędzia fazy — para Hilberta, rotacja, polaryzacja (T-FILT-05, T-PHS-01/02)

**Files:**
- Create: `src/chain/PhaseTool.h/.cpp`, `tests/unit/test_phase.cpp`, `tests/golden/phase.sgg` — odpowiedź impulsowa gałęzi A i B przy 48 kHz, 4096 próbek.

**Interfaces:**
- **Para Hilberta:** `class HilbertPair { void prepare(double fs); void process(double x, double& a, double& b); void reset(); double groupDelayMsA(double fHz) const; }` — bieguny z SPEC §7.7, sekcje `(a + z⁻¹)/(1 + a·z⁻¹)`.
- **Parametry:** `struct PhaseParams { bool invert; double rotationDeg; }`.
- **Stopień:** `class PhaseTool { void prepare(double fs); void setParams(const PhaseParams&); void process(std::span<double>); void chainA(std::span<const double> in, std::span<double> out); double groupDelayMs(double fHz) const; }`:
  - `y = cos θ·A + sin θ·B`;
  - θ wygładzane L20;
  - polaryzacja z crossfade 5 ms;
  - `chainA` daje sub po gałęzi A dla Auto-Align.

- [ ] **Step 1: Testy (czerwone)** `[phase]`:
  - `T-FILT-05 B leads A by 90 +- 0.2 deg over 12-600 Hz at 44.1/48/96 kHz; branch magnitudes 0 +- 0.001 dB`;
  - `T-PHS-01 the sum with an inverted copy is exactly 0`;
  - `T-PHS-02 rotation in 30-300 Hz: phase +- 0.2 deg, amplitude +- 0.02 dB`;
  - `theta 0 equals chain A (golden, 1e-12)`;
  - `chain A group delay 9.8/6.2/3.2 ms at 30/50/100 Hz +- 0.1 ms`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): Hilbert-pair phase rotation and polarity`.

### Task 21: SubChain — składanie toru, latencja, straż NaN (T-DRV-06, T-RT-03)

**Files:**
- Create: `src/chain/SubChain.h/.cpp`, `tests/unit/test_subchain.cpp`.

**Interfaces:**
- `struct SubChainParams { double levelGain; BoostParams boost; GateParams gate; DriveParams drive; bool drivePostComp; CompParams comp; EqParams eq; SubFilterParams filters; PhaseParams phase; LimiterParams limiter; }`.
- `class SubChain { void prepare(double fs, int maxBlock, Quality q); void setParams(const SubChainParams&); void process(std::span<double>); int latencySamples() const; void reset(); bool nanEvent() const; std::span<const double> afterChainA() const; }`:
  - kolejność stała (SPEC §7);
  - Drive Pre/Post-Comp z crossfade 20 ms;
  - latencja = Drive + kompresor + limiter + filtry, stała dla danego `quality` i `filt_phase`;
  - straż NaN/Inf: wyjście 0, reset stanów, flaga;
  - `afterChainA()` daje sub ostatniego bloku po gałęzi A — dla Auto-Align.

- [ ] **Step 1: Testy (czerwone)** `[subchain]`:
  - `neutral chain (every stage off or 0 dB) is a bit-exact delay`;
  - `latency is the sum of the stage latencies: Live 0, Mix L_os + 2 ms, Print L_os + 5 + 5 ms (+ linear phase)`;
  - `T-RT-03 NaN/Inf injections: finite output, states reset, recovery within 1 s`;
  - `T-DRV-06 LFE mode: Drive harmonics above lfe_lp attenuated by the slope +- 1 dB`;
  - `process does not allocate`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(chain): sub chain assembly with NaN guard`.

### Task 22: Rejestr parametrów z PARAMETERS.md i sanitise

**Files:**
- Create:
  - generator: `tools/gen_params.py`, `tools/tests/test_gen_params.py`;
  - kod: `src/params/ParamSpecs.inc` (generowany, w repo), `src/params/Parameters.h/.cpp`, `src/params/Sanitise.h/.cpp`;
  - testy: `tests/unit/test_params.cpp` (w `subgen_plugin_tests`).

**Interfaces:**
- **Generator:** `gen_params.py [--check]` czyta tabele PARAMETERS.md (rozwija `eqN_*` z tabelą domyślnych pasm) i zapisuje `ParamSpecs.inc` z wierszami `SUBGEN_PARAM(id, "Name", Type, min, max, def, Scale, Smoothing, automatable, "choice|…")`; `--check` sprawdza, czy plik jest aktualny.
- **Rejestr w C++:**
  - `enum class ParamId : int { bypass, quality, … }` (z `.inc`);
  - `juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()`:
    - version hint 1;
    - skala log przez `NormalisableRange` z funkcjami konwersji;
    - `withAutomatable(false)` dla parametrów N;
  - `class ParamView { float get(ParamId) const; }` — odczyt atomowy;
  - `double gainFromDb(double db, bool offAtMin)` — −60 → 0,0 dokładnie;
  - `struct ParamSnapshot { std::array<float, kNumParams> raw; }` — surowe wartości w kolejności `ParamId`, odczytywane raz na blok;
  - `void sanitise(ParamSnapshot&)` — relacje z PARAMETERS §1.

- [ ] **Step 1: Testy (czerwone).**
  - Python: `test_parses_141_ids`, `test_check_detects_a_stale_file`.
  - C++ `[params]`:
    - `layout has 141 parameters with unique ASCII ids of <= 24 chars and version hint 1`;
    - `defaults and ranges equal the registry`;
    - `quality, filt_phase and align_ms are not automatable`;
    - `-60 dB Off maps to gain 0.0 exactly`;
    - `sanitise enforces range_hi >= 1.2 range_lo, lfe_lp >= 2 lfe_hp, Bessel slopes <= 48, Linear Phase only in Print`.
- [ ] **Step 2:** uruchomić → ImportError w Pythonie, błąd kompilacji w C++.
- [ ] **Step 3: Implementacja**; ctest `gen_params_check`.
- [ ] **Step 4:** zielono; commit `feat(params): parameter registry generated from PARAMETERS.md`.

### Task 23: Routing — klasy kanałów, źródło S1/S2, router S7, sumowanie S8

**Files:**
- Create: `src/routing/ChannelMap.h/.cpp`, `src/routing/SourceMixer.h/.cpp`, `src/routing/Correlation.h`, `src/routing/Router.h/.cpp`, `src/routing/Summation.h/.cpp`, `tests/unit/test_routing.cpp`.

**Interfaces:**
- **Mapa kanałów** (tabela SPEC_IO_UI §1):
  - `enum class ChannelClass { Front, Center, Surround, Height, Lfe, Other }`, `enum class Side { Left, Right, Centre, None }`;
  - `struct ChannelInfo { ChannelClass cls; Side side; int bufferIndex; }`, `struct ChannelMap { std::array<ChannelInfo, 32> ch; int count; bool hasLfe; }`;
  - `ChannelMap buildChannelMap(const juce::AudioChannelSet&, std::span<const int> bufferIndices)`;
  - `bool isSupportedLayout(const juce::AudioChannelSet&)` i `bool isSupportedPair(const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)` — SPEC_IO_UI §2.1–2.2.
- **Typy parametrów routingu:**
  - `enum class SourceMode { Sum, Max, PerChannel, Auto }`, `enum class OutMode { Auto, Mono, Stereo, Surround, Lfe }`;
  - `struct SourceParams { std::array<double, 5> feedGain; double srcGain; SourceMode mode; KeyMode keyMode; double keyGain; }` — kolejność klas Front, Center, Surround, Height, LFE;
  - `struct RouterParams { OutMode mode; std::array<double, 5> classGain; std::array<double, 5> classDelayMs; std::array<bool, 5> classInvert; double width, lfeTrimGain, lfeBleedGain; }`;
  - `struct SumParams { bool mixMode; double dryGain, mix; bool equalPower, wetOnly; double outTrimGain; }`.
- **Źródło S1/S2:** `class SourceMixer { void prepare(double fs, int maxBlock, const ChannelMap&); void setParams(const SourceParams&); void process(const double* const* in, int n, const double* const* key, int keyCh, std::span<double> sL, std::span<double> sR); int numStreams() const; double correlation() const; }`:
  - wagi klas, średnie grup L i R;
  - tryby: Sum; Max (RMS 200 ms, histereza 1,5 dB, crossfade 30 ms); Per-Channel; Auto (ρ ±0,25 z płynnym przejściem);
  - sidechain: Detect lub Trigger.
- **Korelacja:** `class Correlation { void prepare(double fs); double process(double a, double b); }` — uśrednianie 300 ms, podłoga 1e-12.
- **Router S7:** `class Router { void prepare(double fs, int maxBlock, const ChannelMap& out); void setParams(const RouterParams&); void process(std::span<const double> s1, std::span<const double> s2, double* const* out, int n); OutMode resolved() const; }`:
  - Auto → LFE albo Mono;
  - wzmocnienia klas G20; opóźnienia ≤ 30 ms; polaryzacje z crossfade 5 ms;
  - szerokość, `lfe_trim`, `lfe_bleed`.
- **Sumowanie S8:** `class Summation { void prepare(double fs); void setParams(const SumParams&); void process(const double* const* dry, const double* const* sub, double* const* out, int numCh, int n); }`:
  - Add / Mix / Wet Only; zmiana trybu z crossfade 20 ms;
  - `out_trim`;
  - ścieżki bit-exact.

- [ ] **Step 1: Testy (czerwone)** `[routing]`:
  - `T-SRC-01 identical channels give unity +- 0.01 dB; mono passes; class weights apply`;
  - `T-SRC-02 SIG-08: Auto >= -6 dB re the in-phase case; Sum flags W-03 (rho < -0.5)`;
  - `T-SRC-03 Max switches with 1.5 dB hysteresis and a 30 ms crossfade without flicker`;
  - `T-SUR-01 class gains +- 0.01 dB, delays exact, polarity exact`;
  - `T-SUR-02 width 0 gives L = R, 100 independent, formula within 1e-9`;
  - `T-SUM-02 mix 0 is bit-exact dry, 100 is sub only, equal power a^2 + b^2 = 1 within 1e-12`;
  - `T-SUM-05 out_trim 0 dB is bit-exact`;
  - `T-IO-02 channel-order permutations (film, Logic, VST3, DTS) give identical per-class results`;
  - `Auto output mode resolves to LFE with an LFE channel, else Mono`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(routing): channel classes, source mixer, router and summation`.

### Task 24: Auto-Align (T-PHS-03)

**Files:**
- Create: `src/analysis/CaptureBuffer.h`, `src/analysis/AutoAlign.h/.cpp`, `tests/unit/test_autoalign.cpp`, `tests/golden/autoalign.sgg` — wyniki prototypu dla SIG-18 i 4 scenariuszy syntetycznych.

**Interfaces:**
- **Bufor:** `class CaptureBuffer { void prepare(double fs, double seconds = 8.0); void push(std::span<const double> dryBand, std::span<const double> subAfterA); bool snapshot(std::vector<double>& dry, std::vector<double>& sub) const; }` — SPSC, bez blokad po stronie audio.
- **Typy:** `enum class AlignObjective { Energy, Headroom }`; `struct AlignResult { bool clearOptimum; int polarity; double rotationDeg, gainDb, confidenceDb; }`.
- **Analiza:** `AlignResult autoAlign(std::span<const double> dryBand, std::span<const double> subAfterA, double fs, double rangeLoHz, double rangeHiHz, AlignObjective)`:
  - 24 obroty ψ co 15°; sygnał analityczny przez FFT;
  - pasmo nakładania [max(range_lo/2, 20), range_hi];
  - postać kanoniczna |θ| ≤ 90° + polaryzacja;
  - remisy w granicach 1e-3 dB → wygrywa wynik najbliższy tożsamości;
  - pewność = najlepszy − mediana; próg 0,5 dB.

- [ ] **Step 1: Testy (czerwone)** `[autoalign]`:
  - `T-PHS-03 48 synthetic optima found within 15 deg`;
  - `no overlap gives confidence < 0.5 dB (no clear optimum)`;
  - `silent capture: polarity +1, rotation 0, confidence 0 dB, no NaN` (D8 M6);
  - `SIG-18: the 15 deg grid is within 0.5 dB of a 1 deg search`;
  - `canonical form and the tie rule`;
  - `golden SIG-18 results (polarity, rotation)`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(analysis): Auto-Align on the sub after chain A`.

### Task 25: Procesor wtyczki — szyny, layouty, przetwarzanie S1–S8, latencja

**Files:**
- Modify: `src/plugin/PluginProcessor.h/.cpp`.
- Create: `src/plugin/Latency.h/.cpp`, `src/plugin/Warnings.h`, `tests/unit/test_processor.cpp`, `tests/unit/test_layouts.cpp`.

**Interfaces:**
- **Szyny:** main in/out (domyślnie stereo) + opcjonalny sidechain (mono lub stereo, domyślnie wyłączony); `isBusesLayoutSupported` przez `isSupportedLayout` i `isSupportedPair`.
- **`prepareToPlay`:** mapy kanałów; wszystkie bufory na maksimum (oba oversamplery, linie dry na największe L_total przy `align_ms` 40 ms); `ScopedNoDenormals` i reset stanów po > 5 s ciszy.
- **`processBlock`** (float i double, konwersja do wewnętrznych buforów double):
  - pętla poddbloków po 64 próbki na bezwzględnym liczniku próbek;
  - tor: S1 → S2 → `EngineHost`×strumienie → `SubChain`×strumienie → `Router` → `Summation` → `out_trim`;
  - dry przez `DelayLine` o L_total;
  - bypass z crossfade 10 ms.
- **Latencja:** `struct LatencyBreakdown { int os, look, lin, align, total; }`; `LatencyBreakdown computeLatency(Quality, bool linearPhase, double alignMs, double fs, int osLatency)`. Zmiana `quality`, `filt_phase` lub `align_ms` → `setLatencySamples` z wątku wiadomości (listener) oraz wyciszenie 40 ms w wątku audio.
- **Akcja Auto `align_ms`:** `double estimatedSubDelayMs() const` (SPEC §9.2: pasmo źródłowe przy częstotliwości źródła — DIVIDE, TRACK: 2·f_sub1, HARMONIC: f0, TRIGGER: pomijane — oraz filtry toru sub i gałąź A przy f_sub1) i `void applyAutoAlignMs()` (zaokrąglenie do 0,1 ms, ograniczenie 0–40 ms; T-ALN-01).
- **Ostrzeżenia:** `Warnings` — atomowy zestaw bitów W-01…W-12 z logiką SPEC_IO_UI §7.4 (bez UI).

- [ ] **Step 1: Testy (czerwone)** `[processor]`.
  - Sumowanie i bypass:
    - `T-SUM-01 sub Off: output equals the input delayed by L_total, bit-exact, float and double, every quality, layouts L1 and a sample of L2`;
    - `T-SUM-03 Wet Only + dry only = full mix (null <= -140 dB)`;
    - `T-SUM-04 bypass equals the delayed input bit-exact in steady state, without clicks`.
  - Latencja i niezmienniczość:
    - `T-LAT-01 impulse: dry exactly L_total, TRIGGER sub +- 1 sample of L_sub`;
    - `T-LAT-02 latency independent of engine, Drive, Comp, EQ and sub_lp with Linear Phase`;
    - `T-INV-01 block sizes {1, 7, 32, 64, 127, 128, 512, 4096, random} within -130 dB`;
    - `T-INV-02 44.1-192 kHz: frequency ratios and latency in ms +- 1 sample`;
    - `T-INV-04 two runs are bit-identical`;
    - `T-ALN-01 Auto align_ms equals the analytic sub-path group delay (SPEC §9.2 chain) +- 0.1 ms, clamped to 0-40 ms; default chain at f_sub1 30/40/50/80 Hz: 34.8/24.1/19.8/12.2 ms`.
  - Real-time:
    - `T-RT-01 no allocations in processBlock after prepare`;
    - `T-RT-02 denormal ramp: no NaN, CPU within +20 %`.
  - I/O i LFE:
    - `T-IO-01 SIG-15 on every supported layout: dry bit-exact, sub only where routed`;
    - `T-IO-03 x.0 -> x.1 pairs: the LFE carries the sub`;
    - `T-IO-04 leakage into unrouted channels <= -120 dB`;
    - `T-IO-05 ambisonic layout rejected`;
    - `T-LFE-02 LFE mode: only the LFE channel carries sub; Wet Only leakage <= -120 dB`;
    - `T-LFE-03 the LFE ceiling holds on stress signals`;
    - `T-LFE-04 bleed level +- 0.1 dB, band <= 120 Hz`;
    - `T-LFE-05 W-01 and W-05 conditions`.
  - Stabilność i ostrzeżenia:
    - `T-STAB-03 SIG-09/10/11/12 stable; silence after silence <= -120 dB after 1 s`;
    - `T-STAB-05 changing quality, filt_phase or align_ms while playing stays finite with a mute ramp`;
    - `T-UI-02 warning logic W-01..W-12 except W-11`.
  - Review Focus:
    - (1) `blocks larger than the prepared maximum are processed in chunks`;
    - (2) `re-prepare at a new sample rate and layout keeps parameters and re-reports latency`;
    - (3) `NaN only in the sidechain does not reach the output`;
    - (4) `1e4 random automation steps of every automatable parameter stay finite and click-bounded`;
    - (5) `out_mode LFE without an LFE channel behaves as Mono with W-01; sc_use Detect without the sidechain bus uses main with W-02; the mono layout works`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4:** zielono; commit `feat(plugin): processor with the S1-S8 signal flow, layouts and latency reporting`.

### Task 26: Stan, Auto-Align w tle, edytor zastępczy, pluginval i CI dla pełnej wtyczki

**Files:**
- Create: `src/plugin/StateIO.h/.cpp`, `tests/unit/test_state.cpp`.
- Modify: `src/plugin/PluginProcessor.cpp`.

**Interfaces:**
- **Stan:** XML `<SUBGEN stateVersion="1">` z elementami:
  - drzewo APVTS;
  - `<UI scale page abSlot theme/>`;
  - `<AB/>` — pusty do Fazy 4;
  - `<ALIGN pol rot conf/>`;
  - `<PRESET name category/>`.

  Stan binarny dla hosta = skompresowany XML.
- **Migracja:** `std::unique_ptr<juce::XmlElement> migrate(std::unique_ptr<juce::XmlElement>)` — łańcuch vN → vN+1, na razie tylko walidacja v1. Nieznane parametry są ignorowane, brakujące dostają wartości domyślne.
- **Auto-Align w tle:** `void requestAutoAlign()` uruchamia `autoAlign` na `juce::ThreadPool` (dane z `CaptureBuffer`). Wynik ustawia `sub_pol` i `sub_rot`, chyba że `align_lock` jest włączony; trafia też do `<ALIGN/>`.

- [ ] **Step 1: Testy (czerwone)** `[state]`:
  - `T-STATE-01 all 141 parameters round-trip bit-identically`;
  - `T-STATE-04 corrupt and unknown states (1000 fuzzed blobs) fall back to defaults without crashing`;
  - `T-INV-03 setNonRealtime(true) renders identically to real time`;
  - `requestAutoAlign sets polarity and rotation unless align_lock`;
  - `the editor opens and closes 100 times without leaks`.
- [ ] **Step 2:** uruchomić → błąd kompilacji.
- [ ] **Step 3:** implementacja.
- [ ] **Step 4: Walidacja.** Zielono, potem `scripts/validate.sh` (pluginval 10 dla VST3 i AU) lokalnie → PASS. Jeśli zgoda ze STOP 1 obowiązuje: push na `dev`, a CI musi być zielone na macOS i Windows (pluginval 10, sanitizery).
- [ ] **Step 5: Commit** `feat(plugin): versioned state, background Auto-Align, placeholder editor and full validation`.

### Task 27: SPK-1 (Cubase) i SPK-2 — test ręczny (STOP 2)

**Files:**
- Create: `tools/make_channel_id_session.py` (+ test), `tools/check_bounce.py` (+ test), `docs/checklists/SPK-2_cubase.md`.
- Pliki sesji: `renders/spk2/` (poza gitem).

- [ ] **Step 1: Generator sesji.** Skrypt generuje WAV-y 5.0, 5.1, 7.1.2 i 7.1.4 z tonami identyfikacyjnymi SIG-15 (1 kHz + 100·k Hz, −20 dBFS) i łożem 60 Hz o unikalnych poziomach klas. `check_bounce.py` czyta zgranie z Cubase i raportuje, które kanały niosą sub (pasmo < 120 Hz) i które ton. Testy: każdy kanał ma swój ton; analiza wykrywa sub w zadanym kanale.
- [ ] **Step 2: Checklista dla użytkownika:**
  - instancja VST3 na ścieżkach 5.1, 7.1.2 i 7.1.4;
  - czy Cubase oferuje parę 5.0 → 5.1;
  - `out_mode` LFE i Surround — zgranie i `check_bounce.py`;
  - PDC: latencja zgodna z zegarem sesji;
  - zapis i odczyt projektu;
  - AU w GarageBand (T-HOST-02).
- [ ] **Step 3: STOP 2.** Użytkownik wykonuje checklistę. Wyniki do dziennika: SPK-1 (Cubase ładuje VST3, GarageBand ładuje AU) i SPK-2. Przy negatywnym SPK-2 — reguła cięcia z SPEC_BUILD_RELEASE §12.
- [ ] **Step 4: Commit** `test: channel-identification session and the Cubase checklist (SPK-1, SPK-2)`.

### Task 28: SPK-4 — pakiety na runnerach

**Files:**
- Create: `installers/macos/distribution.xml`, `installers/macos/scripts/postinstall`, `installers/macos/uninstall.sh`, `installers/windows/installer.iss`, `scripts/package.sh`, `.github/workflows/spike_packaging.yml` (tylko `workflow_dispatch`).

- [ ] **Step 1: Pakiet lokalnie.** `scripts/package.sh` buduje niepodpisany `.pkg`: pkgbuild z VST3, AU i `uninstall.sh`; productbuild z `distribution.xml` (minimum macOS 11). Test skryptem: `pkgutil --payload-files` zawiera oczekiwane ścieżki z SPEC_BUILD_RELEASE §4.1. Nie instalować lokalnie — zapis do `/Library` zmienia system; instalacja tylko na runnerze.
- [ ] **Step 2: Workflow.**
  - macOS: `sudo installer -pkg … -target /`, sprawdzenie plików, `uninstall.sh`, sprawdzenie usunięcia.
  - Windows:
    - `ISCC installer.iss`;
    - instalacja `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`;
    - pliki w `C:\Program Files\Common Files\VST3\` i wpis odinstalowania w rejestrze;
    - pluginval na zainstalowanej kopii;
    - cicha deinstalacja i brak pozostałości.
- [ ] **Step 3: Uruchomienie** — `gh workflow run spike_packaging.yml --ref dev` (wymaga zgody ze STOP 1); wynik SPK-4 do dziennika.
- [ ] **Step 4: Commit** `build: packaging spike for the macOS pkg and Windows Inno Setup (SPK-4)`.

### Task 29: Benchmarki CPU (T-PERF-01/02, zamknięcie SPK-6)

**Files:**
- Create: `tests/bench/bench_processor.cpp` (target `subgen_bench`, poza ctest), `docs/TEST_REPORT.md` (sekcja Faza 3).

- [ ] **Step 1: Benchmark** konfiguracji z SPEC_BUILD_RELEASE §7: 48 kHz, blok 128, 1000 bloków po rozgrzewce; mediana i p99 względem czasu bloku (2,667 ms). Budżety:

  | Konfiguracja | Budżet CPU |
  |---|---|
  | Stereo, Divide, Live, bez Drive i Comp | ≤ 1,0 % |
  | Divide + Drive 2× + Comp + EQ (5) + Limiter, Mix | ≤ 3,0 % |
  | Track, wszystkie sekcje, Mix | ≤ 5,0 % |
  | Hybrid, Print 4× + Linear Phase | ≤ 12 % |
  | 7.1.4, tryb LFE, Mix | ≤ 4,0 % |
  | pamięć bez GUI (RSS po `prepare`) | ≤ 64 MB |
  | T-PERF-02 | p99 ≤ 3 × mediana |
- [ ] **Step 2: Wyniki** do `docs/TEST_REPORT.md` (wartość, wersja, data). SPK-6: Linear Phase mieści się w budżecie → zostaje w v1.0; inaczej reguła cięcia z §12.
- [ ] **Step 3: Commit** `test: CPU benchmarks and the first TEST_REPORT entries`.

### Task 30: Raport Fazy 3 i bramka (STOP 3)

**Files:**
- Create: `docs/PHASE3_REPORT.md`.
- Modify: `docs/DECISIONS.md` (sekcja F: proponowane ADR-030+), `CHANGELOG.md`, `docs/TEST_REPORT.md`.

- [ ] **Step 1: Raport** (po polsku):
  - tabela testów TEST_PLAN dotkniętych w Fazie 3: ID → wartość → PASS/FAIL;
  - spike'i SPK-1…6 z werdyktem;
  - zmierzone latencje: L_os z JUCE dla 44,1–192 kHz i L_total;
  - CPU; status CI z linkami do przebiegów;
  - Rulings i odchylenia od SPEC jako propozycje korekt: dwie osie czasu DIVIDE i bramka czytająca pewność znaną w chwili odczytu, odwzorowanie `dynamics` i `trk_speed`, reguła pasma PLL, tabela Auto Gain, `subgen_dsp` jako INTERFACE;
  - ryzyka i lista rzeczy niezweryfikowanych.
- [ ] **Step 2: Przegląd całej gałęzi** — niezależna sesja tylko do odczytu (jak D8), jeśli użytkownik się zgodzi; inaczej autoprzegląd z wpisem w dzienniku.
- [ ] **Step 3: Commit** `docs: phase 3 report`; aktualizacja pamięci projektu.
- [ ] **Step 4: STOP 3** — bramka Fazy 3: raport, spike'i, korekty SPEC, decyzje.

---

## Self-review (wykonane)

**Pokrycie SPEC — przypisanie sekcji do zadań:**

| Sekcja | Zadanie |
|---|---|
| SPEC §4.3–4.4 (poddbloki, latencja) | 7, 25 |
| §4.5 (oversampling, aliasing) | 14 |
| §4.6–4.8 (wątki, numeryka, reset, bypass) | 24–26 |
| §5.1–5.2 (S1, S2) | 23 |
| §5.3–5.4 (S3, S4) | 7 |
| §6.1–6.6 (silniki) | 7–13 |
| §7.1–7.8 (tor sub) | 14–21 |
| §8.A | 23 |
| §8.B | 20, 24, 26 |
| §8.C — szerokość | 23 |
| §8.C — korelometr i metering | Faza 4 |
| §9 | 23, 25 |
| SPEC_IO_UI §1–5 | 23, 25 |
| SPEC_IO_UI §9 (stan) | 26 |
| SPEC_IO_UI §6–8 (metering, UI, presety) | Faza 4 |
| SPEC_BUILD_RELEASE §1–3 | 1–3 |
| SPEC_BUILD_RELEASE §4 | spike w 28, produkcja w Fazie 6 |
| SPEC_BUILD_RELEASE §7 | 29 |
| SPEC_BUILD_RELEASE §9 | 1, 3, 19, 27–29 |
| SPEC_BUILD_RELEASE §11 | 2 |

**TEST_PLAN — testy poza Fazą 3:**
- Faza 4: T-UI-01 w pełnym zakresie, T-LFE-06, T-STATE-03, T-STATE-05.
- Faza 5: T-STAB-01/02/04, T-FMT-03, T-HOST, T-LIST.
- Po pierwszym wydaniu: T-STATE-02 (wymaga stanów z wcześniejszej wersji).
- Faza 6: T-INST.

**Spójność typów:**

| Typ lub klasa | Definicja | Użycie |
|---|---|---|
| `Quality` | 7 | 13–21, 25 |
| `EngineControl` | 7 | 8–13 |
| `NoteTracker` / `ConfidenceGate` | 7 | 8 (bramka także 10) |
| `FilterFamily` / `designFilter` | 5 | 7, 18, 19 |
| `SlidingMax` | 6 | 8, 12, 14, 15, 16 |
| `RealFft` | 4 | 19, 24 |
| `PitchMpm` | 9 | 10, 11 |
| `OnsetDetector` | 11 | domyka Reset z 10 |
| `SubFilterParams` | 18 | 19, 21 |
| `*Params` stopni | 14–20 | 21 |
| `HilbertPair` / `chainA` | 20 | 21, 24 |
| `ChannelMap` | 23 | 25 |
| `CaptureBuffer` / `autoAlign` | 24 | 26 |

**Review Focus:** pozycje 1–5 mają testy w zadaniach 25 (wszystkie) oraz 8 i 13 (pozycje 2–3).

**Po przeglądzie D8 (2026-10-21):** zadania 6–11, 14–16, 19, 24, 25 i 30 zaktualizowane do SPEC 0.3. Nowe testy C++ przenoszą testy prototypu z poprawek D8; zadanie 8 dostało przyczynowy wzorzec bramki.

**Proporcja:** plan zawiera sygnatury, nazwy testów z wartościami ze SPEC i decyzje, których wykonawca nie podejmie sam (latencje, siatka Auto Gain, format danych złotych, kolejność toru). Ciała funkcji są pominięte, bo prototyp `proto/` jest implementacją wzorcową.
