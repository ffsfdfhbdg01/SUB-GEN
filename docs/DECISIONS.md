# SUB-GEN — DECISIONS

Data: 2026-10-19 · Status: Faza 0 zatwierdzona przez użytkownika (2026-10-19, „przyjmuję wszystkie rekomendacje”); wpisy poniżej przyjęte.

## A. Rejestr decyzji (ADR)

| ID | Decyzja | Status | Uzasadnienie (skrót) | Alternatywy |
|---|---|---|---|---|
| ADR-001 | JUCE 9.0.4 + CMake; standard C++20 (do potwierdzenia w Fazie 1) | Przyjęta (2026-10-19) | najnowsza stabilna wersja; wspiera VST3/AU/AAX i wszystkie layouty, jakich potrzebujemy; C++20 działa na clang 17 (CLT) i MSVC 2022 | C++23 (Pamplejuce) — odłożone do czasu spike'a na CLT |
| ADR-002 | Nazwa produktu inna niż „SubGen”; repo i nazwa robocza w kodzie: SUB-GEN | Przyjęta (2026-10-19): nazwa robocza SUB-GEN, nazwa produktu wybrana najpóźniej przed Fazą 6 | SSL SubGen (2023) jest komercyjnym produktem o tej nazwie | zostawić „SubGen” (ryzyko znakowe) |
| ADR-003 | Ścieżka licencyjna A (AGPLv3) | Przyjęta (2026-10-19): ścieżka A, AGPLv3 | repo publiczne; 0 kosztów; AAX SDK dostępny na GPLv3 | Ścieżka B (JUCE EULA: Starter/Indie/Pro; kod prywatny lub własna licencja) |
| ADR-004 | Windows x64 w v1.0; CI od Fazy 3 jako jedyna weryfikacja Windows | Przyjęta (2026-10-19) | wymóg użytkownika (instalatory na oba systemy); brak lokalnego Windows | Windows jako „beta” bez testów ręcznych |
| ADR-005 | Instalatory: macOS `.pkg` (pkgbuild/productbuild), Windows Inno Setup | Przyjęta (2026-10-19) | narzędzia dostępne lokalnie i na runnerach (Inno Setup 6.7.1 preinstalowany) | `.dmg` (problemy z drag-and-drop), WiX/NSIS |
| ADR-006 | Lokalne buildy na samym CLT + Ninja, bez pełnego Xcode | Przyjęta (2026-10-19), do potwierdzenia spike'iem w Fazie 3 | 17–21 GiB wolnego miejsca; CLT ma SDK z frameworkami audio | pełny Xcode (kilkanaście GB) |
| ADR-007 | Podpisywanie opcjonalne w CI (sekrety nie są wymagane do zielonego builda) | Przyjęta (2026-10-19) | brak kont Apple Developer / Azure; Pamplejuce domyślnie wymaga podpisu | podpis wymagany od początku |
| ADR-008 | AAX: cel budowany w CI, ale poza release do czasu uzyskania podpisu Avid | Przyjęta (2026-10-19) | produkcyjny Pro Tools wymaga podpisu; brak Pro Tools lokalnie | AAX w v1.0 niepodpisane (tylko Pro Tools Developer) |
| ADR-009 | Zakres silników v1.0: DIVIDE, TRACK, TRIGGER, HARMONIC, HYBRID; SPECTRAL/HQ w v1.1 | Przyjęta (2026-10-19) | ogranicza ryzyko; SPECTRAL ma największy koszt latencji i testów | wszystkie silniki w v1.0 |
| ADR-010 | Tryb LFE: LPF 120 Hz o umiarkowanym nachyleniu domyślnie (opcja stromego i linear-phase), metering z dwiema konwencjami poziomu (+10 dB odtwarzania: film / AC-3 / wymiana ITU; 0 dB: muzyka — korekta względem trzech konwencji z Fazy 0, patrz SPEC), brak automatycznej zmiany wzmocnienia | Przyjęta (2026-10-19) | ITU-R BS.775-4 Annex 7, ATSC A/52 (enkoder dodaje własny filtr) | sztywny stromy LPF |
| ADR-011 | Host referencyjny: Cubase 13 (VST3, 5.1/7.1.2/7.1.4) | Przyjęta (2026-10-19) | jedyny lokalny host | Reaper (dodatkowy, wymaga pobrania) |

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

## B2. Odpowiedzi użytkownika (2026-10-19)
Użytkownik przyjął wszystkie rekomendacje z listy pytań 1–9, z nazwą roboczą SUB-GEN: licencja AGPLv3 (ścieżka A); v1.0 bez AAX w release (AAX jako cel CI); bez podpisów systemowych na start; zakres silników v1.0 bez SPECTRAL/HQ; zgoda na instalacje wymienione w pytaniu 9 (po zatwierdzeniu SPEC, w Fazie 2–3), bez pełnego Xcode.
Uwaga do pytania 5 (pushowanie): rekomendacja zakładała, że użytkownik przejrzy `docs/` przed pierwszym pushem; nie zostało to potwierdzone, więc **pierwszy push `main` jest odroczony do bramki Fazy 1**, gdzie zapytam o to jednym zdaniem. Do tego czasu nic nie jest wypychane.

## C. Zarys Fazy 1 — zrealizowany 2026-10-19 w SPEC.md, SPEC_IO_UI.md, SPEC_BUILD_RELEASE.md, PARAMETERS.md i TEST_PLAN.md (oczekuje na akceptację)
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

## D. Faza 2 (2026-10-20): wyniki prototypu i proponowane ADR

Raport: `docs/PHASE2_REPORT.md`. Wszystkie wpisy zostały **zaakceptowane na bramce Fazy 2 (2026-10-20)** razem z rekomendacjami D1–D9 i naniesione w SPEC 0.2. Doprecyzowania przy nanoszeniu: ADR-018 — sposób liczenia G_c z Tone (widmo okresu w wątku UI, ważenie odpowiedzią Tone przy k·f_sub1 w wątku audio); ADR-020 — stała długość jądra N_LIN (≈ 42,7 ms przy 48 kHz), bo latencja nie może zależeć od `sub_lp_freq`.

| ID | Decyzja | Status | Uzasadnienie (skrót) | Alternatywy |
|---|---|---|---|---|
| ADR-012 | DIVIDE: licznik (odwracanie znaku) + tor wygładzający jako podstawa; „phase halving” najwyżej jako wariant Print | Zaakceptowana | zerowa latencja, sub sprzężony z falą, błąd częstotliwości ~2·10⁻⁹ % | phase halving (czystsze widmo, ale wymaga FIR Hilberta) |
| ADR-013 | DIVIDE: pewność z trendu okresów (mediana 5) + histereza bramki zamiast reguły z SPEC §6.2 | Zaakceptowana | reguła ze SPEC zamyka bramkę na każdej stopie; trend: stopa otwarta w 100 % (w 50–300 ms), szum losowy ≤ 1,5 %; uzupełniona przez ADR-025 i ADR-026 | reguła ze SPEC |
| ADR-014 | TRACK: MPM na ramkach świadomych onsetu, skalibrowana pewność, prior „trzymaj + 3 ramki”; YIN odrzucony; PLL tylko do zamka fazy | Zaakceptowana | MPM: 1,7 centa, 0 błędów oktawowych (przy dopasowanym opóźnieniu); YIN: 38 klatek z błędem oktawowym na SIG-05 (liczby po D8, przyczynowy dekymator) | YIN, PLL jako detektor |
| ADR-015 | L_look Live/Mix/Print = 0/47/65 ms | Zaakceptowana (D3) | TRACK potrzebuje 2 okresów; 25/50 ms wyrównuje tylko nuty ≥ 100/45 Hz | 25/50 ms ze SPEC |
| ADR-016 | HARMONIC: obwiednia max-hold 1,5 okresu `range_lo` + 5/60 ms | Zaakceptowana | obwiednia ze SPEC daje 13,6 dB błędu wag; max-hold 0,017 dB | peak follower ze SPEC |
| ADR-017 | Drive: aliasing jako składowe nieharmoniczne (tony 31/81/121 Hz); kryterium po LP toru sub | Zaakceptowana (D2) | Hard 100 %: Live −55 / Mix −77 / Print −95 dB pełnopasmowo, −129 dB poniżej 500 Hz (−6 dBFS, 48 kHz); uzupełniona przez ADR-027 (wejścia do +12 dBFS) | progi pełnopasmowe; 2× także w Live |
| ADR-018 | Auto Gain na RMS(AC) mieszanki α, z Tone przy f_sub1 | Zaakceptowana (D4) | ±0,04 dB bez Tone; z Tone 500 Hz bez kompensacji do −8,8 dB | bez Tone |
| ADR-019 | Para Hilberta 4 sekcje/gałąź; Auto-Align analizuje sub po gałęzi A; kanonicznie \|θ\| ≤ 90° z polaryzacją | Zaakceptowana | 0,133°, ±0,010 dB; θ = 0 = gałąź A (do 9,8 ms opóźnienia) | 3 sekcje (0,86°, ±0,065 dB) |
| ADR-020 | Linear Phase: cel \|H\|, Kaiser β = 3, nieparzyste N; v1.0 tylko dla LP, stała długość N_LIN (≈ 42,7 ms przy 48 kHz) | Zaakceptowana (D5) | HP 20 Hz kosztuje ~159 ms przy 48 kHz; sam LP 39 ms | HP+LP; całość w v1.1 |
| ADR-021 | LFE: domyślnie Butterworth 24 dB/okt przy 120 Hz | Zaakceptowana | 14,25 dB przy 1,5·fc, −0,17 dB przy 80 Hz, 3,65 ms przy 40 Hz | 12/48 dB/okt, LR, Bessel |
| ADR-022 | Kompresor: detektory bez tętnień (RMS: okno + hold; Peak: hold); reguła tętnień usunięta; auto release 50/500 ms | Zaakceptowana (D7) | THD −124 dB przy dowolnych czasach; reguła nie pomaga, a na onsecie bywa szkodliwa (dla Peak); uzupełniona przez ADR-028 | reguła jako podłoga |
| ADR-023 | Limiter lookahead: min-hold → release → wygładzanie Hanna (gwarancja sufitu); granice THD T-LIM-02: Mix ≤ −35 dB, Print ≤ −50 dB | Zaakceptowana | przekroczenie 3·10⁻¹⁶ na sygnałach stresowych | — |
| ADR-024 | `align_ms` 0–40 ms + akcja Auto | Zaakceptowana (D6) | naturalne opóźnienie toru sub (domyślny łańcuch): 24,1 ms przy 40 Hz, 34,8 ms przy 30 Hz | 0–20 ms |

## E. Poprawki po niezależnym przeglądzie D8 (2026-10-21)

Przegląd: `.superpowers/sdd/2026-10-19-subgen-phase2-prototype/final-review-independent.md` (poza gitem); wyniki: PHASE2_REPORT §11. Użytkownik przyjął rekomendacje R1–R5 (2026-10-21); naniesione w SPEC 0.3.

| ID | Decyzja | Status | Uzasadnienie (skrót) | Alternatywy |
|---|---|---|---|---|
| ADR-025 | DIVIDE (R1): od początku nuty pewność tymczasowo 1, aż trzy okresy dadzą pierwszą resztę trendu; w Mix/Print bramka czyta pewność z wyprzedzeniem E; niepotwierdzone otwarcie zamyka się w 10 ms | Zaakceptowana | bramka wycinała pierwsze ~40 ms każdej stopy (0,5 % energii suba); teraz 94–96 % | tylko lookahead (nie pomaga w Live); bez bramki na onsecie |
| ADR-026 | DIVIDE (R2): granice nut z aktywności S4 i ataku 12 dB; dzielnik zamrożony poza nutą; reset +1/+1 przy przejściu przez zero (Mix/Print przed początkiem narastania, po końcu nuty), w Live rampa 0,5 ms | Zaakceptowana | reguła przerwy działała tylko przy cyfrowej ciszy (szum −90 dBFS: 12/20); teraz 20/20 przy szumie i buczeniu | sama aktywność S4 (nie łapie nakładających się uderzeń) |
| ADR-027 | Drive Fold (R3): głębokość ograniczona do tej przy wejściu −6 dBFS; kryterium aliasingu (D2) dla wejść do +12 dBFS | Zaakceptowana | Fold przy +6 dBFS: −37 dB w paśmie suba (Live); teraz ≤ −90 dB dla wszystkich trybów | 2× w Live (latencja); ograniczenie samego argumentu (nie zmniejsza pasma) |
| ADR-028 | Kompresor (R4): domyślny detektor Peak; RMS z minimalnym atakiem 0,25·T_sub | Zaakceptowana | rozprysk na onsecie: RMS −39 dB, Peak −56 dB; RMS z minimalnym atakiem −42 dB | RMS domyślny; reguła tętnień dla wszystkich detektorów |
| ADR-029 | DIVIDE `shape` (R5): przesuw odcięcia filtra wygładzającego (×32 przy 100 %) zamiast mieszania torów | Zaakceptowana | mieszanie torów przesuniętych o 91° dawało −3,1 dB przy 50 %; teraz ±0,05 dB | wyrównanie fazy toru natywnego all-passem |
