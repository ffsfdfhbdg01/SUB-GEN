# SUB-GEN — DECISIONS

Data: 2026-10-19 · Status: wszystkie wpisy „Proponowane”, dopóki użytkownik nie zatwierdzi Fazy 0.

## A. Rejestr decyzji (ADR)

| ID | Decyzja | Status | Uzasadnienie (skrót) | Alternatywy |
|---|---|---|---|---|
| ADR-001 | JUCE 9.0.4 + CMake; standard C++20 (do potwierdzenia w Fazie 1) | Proponowana | najnowsza stabilna wersja; wspiera VST3/AU/AAX i wszystkie layouty, jakich potrzebujemy; C++20 działa na clang 17 (CLT) i MSVC 2022 | C++23 (Pamplejuce) — odłożone do czasu spike'a na CLT |
| ADR-002 | Nazwa produktu inna niż „SubGen”; repo i nazwa robocza w kodzie: SUB-GEN | Proponowana — wymaga wyboru nazwy | SSL SubGen (2023) jest komercyjnym produktem o tej nazwie | zostawić „SubGen” (ryzyko znakowe) |
| ADR-003 | Ścieżka licencyjna A (AGPLv3) | **Otwarta — decyzja użytkownika** | repo publiczne; 0 kosztów; AAX SDK dostępny na GPLv3 | Ścieżka B (JUCE EULA: Starter/Indie/Pro; kod prywatny lub własna licencja) |
| ADR-004 | Windows x64 w v1.0; CI od Fazy 3 jako jedyna weryfikacja Windows | Proponowana | wymóg użytkownika (instalatory na oba systemy); brak lokalnego Windows | Windows jako „beta” bez testów ręcznych |
| ADR-005 | Instalatory: macOS `.pkg` (pkgbuild/productbuild), Windows Inno Setup | Proponowana | narzędzia dostępne lokalnie i na runnerach (Inno Setup 6.7.1 preinstalowany) | `.dmg` (problemy z drag-and-drop), WiX/NSIS |
| ADR-006 | Lokalne buildy na samym CLT + Ninja, bez pełnego Xcode | Proponowana — wymaga spike'a | 17–21 GiB wolnego miejsca; CLT ma SDK z frameworkami audio | pełny Xcode (kilkanaście GB) |
| ADR-007 | Podpisywanie opcjonalne w CI (sekrety nie są wymagane do zielonego builda) | Proponowana | brak kont Apple Developer / Azure; Pamplejuce domyślnie wymaga podpisu | podpis wymagany od początku |
| ADR-008 | AAX: cel budowany w CI, ale poza release do czasu uzyskania podpisu Avid | Proponowana | produkcyjny Pro Tools wymaga podpisu; brak Pro Tools lokalnie | AAX w v1.0 niepodpisane (tylko Pro Tools Developer) |
| ADR-009 | Zakres silników v1.0: DIVIDE, TRACK, TRIGGER, HARMONIC, HYBRID; SPECTRAL/HQ w v1.1 | Proponowana | ogranicza ryzyko; SPECTRAL ma największy koszt latencji i testów | wszystkie silniki w v1.0 |
| ADR-010 | Tryb LFE: LPF 120 Hz o umiarkowanym nachyleniu domyślnie (opcja stromego i linear-phase), metering z konwencjami poziomu, brak automatycznej zmiany wzmocnienia | Proponowana | ITU-R BS.775-4 Annex 7, ATSC A/52 (enkoder dodaje własny filtr) | sztywny stromy LPF |
| ADR-011 | Host referencyjny: Cubase 13 (VST3, 5.1/7.1.2/7.1.4) | Proponowana | jedyny lokalny host | Reaper (dodatkowy, wymaga pobrania) |

## B. Pytania do użytkownika (zbiorcza lista z rekomendacjami)

1. **Nazwa produktu.** Rekomendacja: wybierz nazwę ≠ „SubGen”; ja w Fazie 1 sprawdzę kolizje dla 3–5 kandydatów. Domyślnie: nazwa robocza SUB-GEN do czasu wyboru (najpóźniej przed Fazą 6).
2. **Licencja.** Rekomendacja: ścieżka A (AGPLv3, open-source) — zero kosztów, pasuje do publicznego repo, pozwala na AAX. Alternatywa: ścieżka B (JUCE Starter/Indie), jeśli chcesz zachować możliwość zamkniętej sprzedaży.
3. **AAX / Pro Tools.** Rekomendacja: v1.0 = VST3 + AU (macOS) i VST3 (Windows); AAX budowany w CI, ale dołączony do release dopiero, gdy uzyskasz podpis od Avid (audiosdk@avid.com). Czy Pro Tools jest dla Ciebie warunkiem v1.0?
4. **Podpisy.** Rekomendacja: v1.0 bez podpisu (macOS: instrukcja „Otwórz mimo to”; Windows: SmartScreen), podpisy dodamy, gdy założysz konta (Apple Developer 99 USD/rok; Azure Artifact Signing 9,99 USD/mies., o ile kwalifikujesz się).
5. **Pushowanie do publicznego repo.** Rekomendacja: po Twoim OK wypycham `main` z samymi dokumentami i licencją (to ustawia domyślną gałąź), potem pracuję na `dev` i wypycham po każdej fazie (CI Windows jest jedyną weryfikacją); `main` aktualizuję merge'em po Twoim OK; release jako draft. Przed pierwszym pushem przejrzysz `docs/` (zawierają informacje o Twoim lokalnym środowisku, które mogę wyciąć).
6. **Testy ręczne na Windows.** Czy masz dostęp do Windowsa (maszyna, VM) i hosta (np. Reaper)? Rekomendacja: CI (pluginval) + Twoje testy przed publikacją; bez tego Windows oznaczamy „beta”.
7. **Hosty i layouty.** Rekomendacja: Cubase 13 jako host referencyjny; layouty 5.1, 7.1.2 i 7.1.4 testowane lokalnie; pozostałe z `AudioChannelSet` objęte testami offline.
8. **Zakres silników v1.0.** Rekomendacja: DIVIDE, TRACK, TRIGGER, HARMONIC, HYBRID; SPECTRAL/HQ w v1.1.
9. **Zgoda na instalacje (Faza 2–3).** Ninja (~0,3 MB), venv Pythona na Sam-dysk z scipy/matplotlib/soundfile (~400–600 MB), pluginval (~25 MB), JUCE jako submoduł git. Rekomendacja: zgoda; pełnego Xcode nie instalujemy.

Wartości domyślne, jeśli odpowiesz tylko „OK”: rekomendacje powyżej, z nazwą roboczą SUB-GEN.

## C. Zarys Fazy 1 (specyfikacja i architektura) — po akceptacji Fazy 0
Dokument `docs/SPEC.md` (i tylko dokumenty, bez kodu produktu):
1. Cele, antycele i granice v1.0 (MUST/SHOULD/COULD z uzasadnieniem).
2. Schemat sygnałowy i tabela parametrów: ID, nazwa, zakres, jednostka, skala, wartość domyślna, wygładzanie, automatyzacja.
3. Specyfikacja każdego silnika: algorytm, parametry, ograniczenia, pseudokod, latencja, przypadki brzegowe.
4. Tor sub: filtry, EQ, Drive/Saturate (charaktery, ADAA, oversampling, kompensacja), kompresor, gate, limiter.
5. Korelacja fazowa (3 warstwy), tryb LFE (z korektami K3), macierz routingu.
6. Macierz I/O i layoutów per format (VST3/AU/AAX) z ograniczeniami oraz testy kolejności kanałów.
7. Plan testów: mapowanie wymagań → testy → progi; zestaw sygnałów; harness headless; walidatory.
8. Plan CI i instalatorów (macOS + Windows), strategia wersjonowania i wydań.
9. Budżet CPU i latencji; plan Fazy 2 (prototyp Python/SciPy) z listą pomiarów.
10. Rejestr ryzyk i plan spike'ów (build na CLT, Cubase 13 + 7.1.4, pluginval).
Przed oddaniem SPEC przeczytam go oczami wykonawcy; niezależny przegląd przez subagenta tylko, jeśli sobie tego życzysz.
