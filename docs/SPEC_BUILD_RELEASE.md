# SUB-GEN — SPEC (część 3): build, CI, instalatory, release, budżety, plan Fazy 2, ryzyka

Wersja dokumentu: 0.2 (korekty z Fazy 2) · Data: 2026-10-20 · Powiązane: [SPEC.md](SPEC.md), [SPEC_IO_UI.md](SPEC_IO_UI.md), [TEST_PLAN.md](TEST_PLAN.md), [RESEARCH.md](RESEARCH.md)

## 1. Układ repo
```
SUB-GEN/
  CMakeLists.txt  VERSION  LICENSE  THIRD_PARTY_LICENSES.md  README.md  CHANGELOG.md
  cmake/            ProductIdentity.cmake, CPM.cmake, helpery
  JUCE/             submoduł git @ 9.0.4
  src/              core · params · dsp · engines · chain · routing · analysis · plugin · ui
  tests/            Catch2: unit, integration, benchmarks, dane referencyjne (golden)
  tools/            host headless, generator sygnałów testowych, skrypty pomocnicze
  proto/            prototypy Pythona (Faza 2)
  installers/       macos/ (distribution.xml, resources, scripts), windows/ (installer.iss)
  .github/workflows/  build_and_test.yml  release.yml
  docs/
```
Gałęzie: `main` (stabilna, release), `dev` (praca). Commity małe, opisowe, po angielsku; konwencja `type: summary` (docs, feat, fix, test, build, ci, chore). Plik `.clang-format` w repo.

## 2. Build
- **CMake ≥ 3.25 + Ninja**; C++20; JUCE przez `add_subdirectory(JUCE)`; zależności przez CPM (Catch2). Wersja z `VERSION` wstrzykiwana do JUCE i instalatorów.
- **Cel wtyczki:** `juce_add_plugin` z `FORMATS VST3 AU Standalone` (AAX dodawany opcją `-DSUBGEN_AAX=ON`; JUCE 9 zawiera AAX SDK z bannerem GPLv3/komercyjną), `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE`/`BUNDLE_ID` z `cmake/ProductIdentity.cmake`, `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`, AU typ efekt. Dokładne opcje potwierdza spike SPK-1 w dokumentacji CMake JUCE.
- **Opcje kompilacji:** `-Wall -Wextra -Wpedantic -Werror` (MSVC `/W4 /WX`), flagi wysokiej optymalizacji dla Release (bez `-ffast-math`, aby zachować determinizm i obsługę NaN); `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`; Windows: statyczne CRT.
- **Biblioteka testowa:** `subgen_dsp` (statyczna: `dsp`, `engines`, `chain`, `routing`, `analysis`, `params`) linkowana przez wtyczkę i testy.
- **Lokalnie (macOS, CLT):** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` oraz `cmake --build build`; instalacja wtyczek do `~/Library/Audio/Plug-Ins/...` (opcja JUCE `COPY_PLUGIN_AFTER_BUILD`). Weryfikuje spike SPK-1.

## 3. CI (GitHub Actions)
Wzorzec: Pamplejuce (MIT) [RESEARCH §8]. Podstawowe zasady: minimalne uprawnienia (`permissions: contents: read`, `contents: write` tylko w jobie release); akcje przypięte do wersji głównej zweryfikowanej w Fazie 3; `concurrency` anulujące nadmiarowe przebiegi; brak sekretów w logach.

| Job | Runner | Zawartość |
|---|---|---|
| `build-macos` | `macos-15` (arm64) | Ninja, configure/build universal, `ctest`, kopia wtyczek do katalogów użytkownika, `auval -v` (AU), pluginval 1.0.4 `--strictness-level 10 --validate` (VST3), artefakty |
| `build-windows` | `windows-2025` (x64) | MSVC (`ilammy/msvc-dev-cmd`), Ninja, configure/build, `ctest`, pluginval strictness 10 (VST3), artefakty |
| `sanitizers` | macOS arm64 | ASan+UBSan, osobno TSan na `subgen_dsp` i testach integracyjnych |
| `rtsan` (SHOULD) | ubuntu z clang ≥ 20 | testy `subgen_dsp` z RealtimeSanitizer jako bramka real-time safety |
| `aax-build` (ręczny) | macos / windows | `-DSUBGEN_AAX=ON`, artefakt niepodpisany, poza release |
| `package` (tag `v*`) | macOS / Windows | pkg i exe wg §4, SHA-256 |
| `release` (tag `v*`) | ubuntu | pobranie artefaktów, `gh release create --draft` |
Podpisy: kroki podpisu/notaryzacji wykonują się tylko, gdy sekrety istnieją. Wzorzec: zmienna `HAS_*` na poziomie joba z wyrażenia `secrets.X != ''`, a warunek `if: env.HAS_* == 'true'`. Brak sekretów → pakiety niepodpisane i zielony build (ADR-007).
Przypięcie narzędzi: pluginval 1.0.4 pobierany po URL z wydania GitHub; `sccache`/`ccache` dla przyspieszenia; runnery publiczne — bez limitu minut.

## 4. Instalatory

### 4.1 macOS (`.pkg`)
- Pakiety składowe `pkgbuild`: VST3 → `/Library/Audio/Plug-Ins/VST3`, AU → `/Library/Audio/Plug-Ins/Components`, presety fabryczne → `/Library/Application Support/<Vendor>/<Product>/Presets/Factory`, skrypt `uninstall.sh` → `/Library/Application Support/<Vendor>/<Product>/`.
- `productbuild` z `distribution.xml` (wybór formatów, licencja AGPLv3, README, tytuł, minimalna wersja macOS 11); `postinstall`: `killall -9 AudioComponentRegistrar` (odświeżenie cache AU, ignorując błędy).
- Podpis i notaryzacja (opcjonalne): Developer ID Application (bundle'e, `--options=runtime --timestamp`) i Developer ID Installer (pkg), `notarytool submit --wait`, `stapler staple`. Brak kont → niepodpisany pkg; README: Ustawienia → Prywatność i ochrona → „Otwórz mimo to” (Sequoia).
- Weryfikacja: `pkgutil --check-signature`, `spctl --assess --type install` (gdy podpisany), `pkgutil --payload-files` (zawartość), instalacja na czystym koncie.

### 4.2 Windows (`.exe`, Inno Setup)
- `installer.iss`: `AppId` (GUID), `AppVersion` z `VERSION`, `PrivilegesRequired=admin`, instalacja 64-bitowa; `[Components]` — VST3 (domyślnie włączony), Presets; `[Files]`: cały folder bundla VST3 → `{commoncf64}\VST3\<Product>.vst3\`, presety → `{commonappdata}\<Vendor>\<Product>\Presets\Factory\`; `[UninstallDelete]` czyści dodane pliki; wpis w „Aplikacje i funkcje”; ciche instalacje `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.
- Podpis (opcjonalny): Azure Artifact Signing lub certyfikat; brak → SmartScreen, opis w README.
- Inno Setup 6.7.1 jest preinstalowany na obrazie Windows 2025 [RESEARCH §8]; wersję i dostępność potwierdza spike SPK-4.

### 4.3 Nazewnictwo i sumy
`<Product>-<version>-macOS.pkg`, `<Product>-<version>-Windows.exe`, `SHA256SUMS.txt` (SHA-256 wszystkich plików). Archiwum źródeł generuje GitHub.

## 5. Proces wydania
1. Kandydat na wydanie na `dev` → wszystkie testy automatyczne zielone (TEST_PLAN) → lista kontrolna ręczna (T-HOST, T-INST).
2. Aktualizacja `VERSION`, `CHANGELOG.md`; merge do `main` po akceptacji użytkownika.
3. Tag `vX.Y.Z` → CI buduje i pakuje → **draft** Release z instalatorami, sumami i notatkami wydania.
4. Weryfikacja draftu: pobranie artefaktów, zgodność SHA-256, instalacja na czystych środowiskach, `pluginval`/`auval` na zainstalowanych kopiach.
5. Publikacja draftu **tylko po jawnej zgodzie użytkownika**.
6. README: instrukcje instalacji/odinstalowania, obejście ostrzeżeń systemowych, wymagania, licencja, link do dokumentacji.

## 6. Zgodność licencyjna (AGPLv3)
- Licencja projektu: AGPLv3 (plik `LICENSE`), nagłówki SPDX w plikach źródłowych, `THIRD_PARTY_LICENSES.md` (JUCE AGPLv3, VST3 MIT, Catch2 BSL-1.0, CPM MIT, Pamplejuce MIT jeśli kod skopiowany, zależności narzędzi).
- Instalator i README zawierają informację o licencji i adresie źródeł (publiczne repo).
- Nazwa produktu nie narusza znaków cudzych (ADR-002); nazwa „SubGen” jest zajęta przez SSL.
- AAX: dopiero po decyzji o włączeniu; AAX SDK pochodzi z modułu JUCE (GPLv3); podpis PACE wymaga narzędzi od Avid [RESEARCH §8].

## 7. Budżety wydajności (cele początkowe; Apple Silicon, 48 kHz, blok 128, jeden rdzeń)
| Konfiguracja | CPU |
|---|---|
| Stereo, Divide, Live, bez Drive/Comp | ≤ 1,0% |
| Stereo, Divide + Drive (2×) + Comp + EQ(5) + Limiter, Mix | ≤ 3,0% |
| Stereo, Track, wszystkie sekcje, Mix | ≤ 5,0% |
| Stereo, Hybrid, Print (4×, Linear Phase) | ≤ 12% (użycie offline) |
| 7.1.4, tryb LFE, Mix | ≤ 4,0% |
| Pamięć (bez GUI) | ≤ 64 MB |
| GUI otwarte (bezczynne) | ≤ 1,5% |
Pomiar: benchmark Catch2 na buforach JUCE, 1000 bloków, mediana i p99 względem czasu trwania bloku; raport w TEST_REPORT.

## 8. Plan Fazy 2 — prototyp DSP offline (Python)
**Status:** wykonany 2026-10-20 — wyniki w [PHASE2_REPORT.md](PHASE2_REPORT.md), korekty naniesione w SPEC 0.2.
Środowisko: venv na Sam-dysk (numpy, scipy, matplotlib, soundfile), po zgodzie. Kod w `proto/`, wyniki w `docs/PHASE2_REPORT.md` i `docs/img/`. Pliki WAV do odsłuchu w `renders/` (poza repo; `.gitignore`).
| ID | Pomiar | Kryterium wyjścia |
|---|---|---|
| P2-01 | Filtry Butterworth/LR/Bessel i kształty Cytomic względem SciPy/RBJ | błąd amplitudy ≤ 0,01 dB (BW/LR), ≤ 0,05 dB (Cytomic); stabilność przy fs ≤ 192 kHz, 15 Hz |
| P2-02 | DIVIDE: dokładność f/2 i f/4 (30–200 Hz), THD, jitter, fałszywe wyzwolenia w szumie, polifonia + bramka pewności; wariant phase-halving | błąd < 0,1%; wybór wariantu |
| P2-03 | TRACK: NSDF/MPM vs YIN vs PLL — dokładność, błędy oktawowe, opóźnienie vs okno | wybór detektora i L_look |
| P2-04 | TRIGGER: powtarzalność (null między uderzeniami), obwiednie, faza startowa | różnica uderzeń ≤ −120 dB |
| P2-05 | HARMONIC: wagi harmonicznych vs amplituda, IMD dwutonowe, strategie normalizacji | dokładność wag ≤ 0,5 dB (sinus) |
| P2-06 | Drive: ADAA1 vs naiwne vs OS 2×/4×; widma harmonicznych per charakter; tablice Auto Gain; DC | aliasing ≤ −90 dB; RMS ±0,5 dB |
| P2-07 | Faza: para Hilberta (rząd vs błąd), rotacja, Auto-Align (obie funkcje celu) | błąd ≤ 1°; sensowny wybór polaryzacji dla sygnałów testowych |
| P2-08 | LFE: opóźnienie grupowe i pasmo dla nachyleń 12–96, BW vs LR vs Bessel; FIR liniowofazowy (długość vs narożnik) | wybór domyślnego nachylenia; budżet latencji Linear Phase |
| P2-09 | Kompresor: THD vs czasy dla 30–100 Hz; reguła tętnień; limiter (lookahead 2/5 ms) | wartości reguł |
| P2-10 | Budżet latencji (L_look, L_os) i potwierdzenie stałej latencji | wartości do §4.4 SPEC |
| P2-11 | Zestaw plików odsłuchowych: kick/808/bas/impact/LFE (A/B z i bez suba, gain-matched) | do Twojego odsłuchu |

## 9. Spike'i Fazy 3
| ID | Pytanie | Kryterium |
|---|---|---|
| SPK-1 | Czy CLT + CMake + Ninja buduje VST3/AU universal z JUCE 9.0.4? | artefakty ładują się w Cubase 13 i przechodzą `auval`; w razie niepowodzenia — Xcode wyłącznie w CI |
| SPK-2 | Czy Cubase 13 akceptuje VST3 z in == out dla 5.1/7.1.2/7.1.4 i parą 5.0→5.1? | poprawne kanały (test tonów identyfikacyjnych) |
| SPK-3 | Czy `pluginval` strictness 10 przechodzi na pustej wtyczce w CI macOS/Windows? | PASS |
| SPK-4 | Czy pkg i Inno Setup budują się na runnerach i instalują/odinstalowują poprawnie? | instalacja cicha + deinstalacja; sprawdzenie plików |
| SPK-5 | Czy MSVC ze statycznym CRT daje VST3 bez redystrybuowalnych? | uruchomienie na czystym runnerze |
| SPK-6 | Czy FIR liniowofazowy LP (N_LIN wg SPEC §7.6, partycjonowana konwolucja) mieści się w budżecie CPU? | zgodnie z §7; inaczej Linear Phase → v1.1 |

## 10. Rejestr ryzyk
| ID | Ryzyko | Prawd. | Wpływ | Mitigacja |
|---|---|---|---|---|
| R1 | Build lokalny na samym CLT nie działa | śr. | śr. | SPK-1; Xcode tylko w CI |
| R2 | Brak Windows lokalnie → regresje niewidoczne | wys. | wys. | CI od Fazy 3, pluginval, testy Windows ręczne przez użytkownika; Windows „beta” bez testów |
| R3 | Opóźnienie detekcji f0 psuje onset w Live | wys. | śr. | Live dokumentowany jako „kompromis”; Mix/Print z lookahead; DIVIDE bez opóźnienia |
| R4 | Nietrafiona definicja Base Frequency | śr. | śr. | przegląd w Fazie 1; prosty UI (nazwy kontekstowe) |
| R5 | Licencja/AAX — nieporozumienia | śr. | wys. | ścieżka A; AAX poza v1.0; przegląd oryginalnych tekstów przed release |
| R6 | Kolizja nazwy (SSL SubGen) | wys. | wys. | zmiana nazwy produktu (ADR-002) |
| R7 | Gatekeeper/SmartScreen zniechęcają użytkowników | wys. | śr. | README, ewentualnie podpisy później |
| R8 | Auto-Align daje mylące wyniki | śr. | niska | kryterium pewności, `align_lock`, ręczne rotacja/polaryzacja zawsze dostępne |
| R9 | Linear-phase za kosztowny | śr. | niska | spike SPK-6; downgrade do v1.1 |
| R10 | Miejsce na dysku | śr. | śr. | zgoda przed pobraniem; venv/zależności na Sam-dysk |
| R11 | Patenty na generatory subharmoniczne | niska | wys. | stosowane techniki są stare (dbx 1970s); brak analizy FTO — do rozważenia przy dystrybucji komercyjnej |
| R12 | Niestabilność hostów przy zmianie latencji | śr. | śr. | latencja tylko z parametrów N; komunikat W-07; testy w Cubase |

## 11. Spójność dokumentów (kontrola Fazy 1)
Skrypt jednorazowy (w katalogu roboczym sesji) sprawdza, że: (a) rejestr zawiera dokładnie 141 unikalnych ID (po rozwinięciu wzorca `eqN_*`); (b) każde ID w backtickach w SPEC/SPEC_IO_UI/TEST_PLAN należy do rejestru lub do dopuszczonej listy nie-parametrów; (c) każdy ID testu cytowany w dokumentach istnieje w TEST_PLAN. Trwała wersja `tools/check_docs.py` — Faza 3.

## 12. Mapa drogowa
| Wersja | Zakres |
|---|---|
| **1.0** | wszystko MUST z SPEC (silniki Divide/Track/Trigger/Harmonic/Hybrid; tor sub; trzy warstwy fazy; tryby wyjścia i LFE; Windows + macOS; instalatory) |
| 1.1 | SPECTRAL/HQ, Linear Phase PEQ i Linear Phase HP (D5); całe Linear Phase, jeśli SPK-6 negatywne, CLAP (`clap-juce-extensions`), AAX w release (po podpisie), macierz routingu per kanał, wielopasmowe źródło |
| 1.x | Note-lock, MIDI-trigger, M/S, podgląd „small speaker” i bass management, Auto-Align „Learn”, analizator z wagą ISO 226 |
Reguły cięcia zakresu: jeśli SPK-6 negatywny → Linear Phase przechodzi do 1.1; jeśli P2-03 nie spełni kryteriów → TRACK oznaczony „beta” w v1.0 (Divide/Trigger/Harmonic pozostają); jeśli SPK-2 negatywny dla 5.0→5.1 → warianty x.0→x.1 tylko tam, gdzie host je akceptuje.

## 13. Otwarte kwestie do Twojej decyzji (poza bramką Fazy 1)
1. **Nazwa producenta/autora** (potrzebna do ID bundla `com.<vendor>.<product>`, kodu producenta AU, ścieżek instalacji i instalatora). Domyślnie placeholder `Vendor`, do podmiany przed Fazą 6.
2. **Język instrukcji:** README po angielsku (repo publiczne) + instrukcja po polsku — domyślnie obie.
3. **Makiety UI** w Fazie 4 z podglądem w przeglądarce — tak/nie.
