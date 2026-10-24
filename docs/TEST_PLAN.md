# SUB-GEN — TEST PLAN (Faza 1)

Wersja dokumentu: 0.4 (korekty z Fazy 2: PHASE2_REPORT §6; po przeglądzie D8: §11; F-1 z Fazy 3: §12, ADR-030) · Data: 2026-10-24 · Powiązane: [SPEC.md](SPEC.md), [SPEC_IO_UI.md](SPEC_IO_UI.md), [SPEC_BUILD_RELEASE.md](SPEC_BUILD_RELEASE.md), [PARAMETERS.md](PARAMETERS.md)
Wyniki będą trafiać do `docs/TEST_REPORT.md` (Faza 5) — każdy test z wynikiem, wersją i datą.

## 1. Zasady
- **Poziomy:** U = test jednostkowy (Catch2, biblioteka `subgen_dsp`) · I = integracyjny (headless host ładujący binarną wtyczkę) · V = walidator zewnętrzny · M = ręczny (użytkownik) · P = pomiar wydajności.
- **Gdzie:** L = lokalnie (macOS) · CI = GitHub Actions (macOS i Windows) · M = ręcznie. Windows jest weryfikowany wyłącznie w CI i ręcznie przez użytkownika.
- **Progi** oznaczone w wersji 0.1 jako [P2] zostały ustalone w Fazie 2; wartość zmierzona na prototypie jest podana jako „P2: …”. Każdy test podaje kryterium w postaci liczby, nie opisu.
- **Zasada dowodu:** wynik testu zapisywany z wartością zmierzoną, nie tylko PASS/FAIL. Brak testu = brak deklaracji jakości (antycel 5).
- **Determinizm:** testy dźwiękowe używają stałych ziaren generatorów; porównania „null” liczone w `double`.
- **Referencje:** SciPy (filtry), formuły analityczne (Czebyszew, obwiednie TRIGGER), implementacja wzorcowa Drive z oversamplingiem 16× (offline, Python).

## 2. Zestaw sygnałów testowych (generowany skryptem, bez plików binarnych w repo)
| ID | Sygnał |
|---|---|
| SIG-01 | sinusy 30, 40, 55, 80, 120, 200 Hz × poziomy −6, −18, −40 dBFS, 5 s |
| SIG-02 | sweep logarytmiczny 10–500 Hz, 20 s, −12 dBFS |
| SIG-03 | syntetyczna stopa: sinus ze spadkiem wysokości 150 → 45 Hz (τ = 30 ms), T60 = 300 ms; 100 uderzeń o poziomach −24…−3 dBFS co 500 ms |
| SIG-04 | „808”: sinus 45 Hz + 2. harmoniczna −8 dB, glissando 90 → 45 Hz w 80 ms, sustain 600 ms; nuty E1…B2 |
| SIG-05 | bas tipu gitarowego: piła przefiltrowana LP 400 Hz z obwiednią szarpnięcia, nuty 41–98 Hz |
| SIG-06 | akord: 55 + 69 + 82 Hz, równe poziomy |
| SIG-07 | szum różowy pasmowy 20–2000 Hz, −24 dBFS |
| SIG-08 | para antyfazowa: L = sinus 60 Hz, R = −L |
| SIG-09 | impuls pojedynczy (0,5) |
| SIG-10 | cisza cyfrowa 60 s |
| SIG-11 | stała +0,5 |
| SIG-12 | prostokąt pełnej skali 40 Hz |
| SIG-13 | wstrzyknięcia NaN/Inf (pojedyncze i seria) |
| SIG-14 | rampa denormalna (zanik wykładniczy do 1e-310) |
| SIG-15 | tony identyfikacyjne kanałów: każdy kanał ma unikalny sinus 1 kHz + 100·k Hz, −20 dBFS (testy routingu dry) oraz „łoże basowe”: 60 Hz o unikalnych poziomach w klasach kanałów (testy `feed_*`) |
| SIG-16 | szum różowy 20–120 Hz (−20 dBFS RMS) i szum różowy szerokopasmowy (testy LFE) |
| SIG-17 | dwa tony 50 + 70 Hz (IMD) |
| SIG-18 | kompozyt stopa + 808 (nakładanie energii dla Auto-Align) |
| SIG-19 | sinus 60 Hz z szumem białym; SNR 20, 10 i 3 dB liczone w paśmie źródłowym (po filtrze zakresu 30–120 Hz), bo silniki przetwarzają `x_b` |

## 3. Rejestr testów

### 3.1 Filtry i prymitywy
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-FILT-01 | SPEC §4.7, §7.6 | Butterworth/LR (rzędy 2–16), f_c ∈ {15, 22, 30, 60, 120, 500 Hz}, fs ∈ {44,1; 48; 96; 192 kHz}; odpowiedź amplitudowa i opóźnienie grupowe vs SciPy | błąd amplitudy ≤ 0,01 dB (5 Hz…0,45·fs); opóźnienie grupowe przy f_c ≤ 0,5% | U · CI |
| T-FILT-02 | SPEC §7.2 | kształty Cytomic (Bell, Low/High Shelf) vs formuły RBJ; zakres 10 Hz…20 kHz; zakres wzmocnień ±18 dB | błąd ≤ 0,05 dB; 0 dB = tożsamość (≤ 1e-12) | U · CI |
| T-FILT-03 | SPEC §4.7 | modulacja f_c 15…500 Hz (10 Hz audio-rate i losowo) | brak NaN; \|y\| ≤ 4·max\|x\|; stabilność | U · CI |
| T-FILT-04 | SPEC §7.6 | tablice Bessela vs `scipy.signal.bessel(norm='mag')`, rzędy 2, 4, 8 | błąd ≤ 0,01 dB; zmienność opóźnienia grupowego do f_c/2 ≤ 1 % (rzędy 4, 8), ≤ 2,5 % (rząd 2; P2: 2,02 %) | U · CI |
| T-FILT-05 | SPEC §7.7 | para Hilberta (4 sekcje na gałąź, bieguny SPEC §7.7): różnica faz i amplitud w paśmie 12–600 Hz | 90° ± 0,2° (P2: 0,133°); amplitudy gałęzi 0 dB ± 0,001 dB | U · CI |
| T-FILT-06 | SPEC §7.6 | Linear Phase (tylko LP): symetria jądra, odpowiedź względem \|H\| prototypu (nie \|H\|²), opóźnienie; N_LIN wg SPEC §7.6 | symetria dokładna; błąd ≤ 0,1 dB od 5 Hz do punktu −60 dB dla konfiguracji pokrytych przez N_LIN (m.in. BW 24 dB/okt 120 Hz; P2: 0,08 dB przy 4097 tapach, 48 kHz); błąd pokazywany w UI = zmierzony ± 0,01 dB; opóźnienie = (N_LIN − 1)/2 | U · CI |

### 3.2 Silniki
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-DIV-01 | SPEC §6.2 | SIG-01 przez Divide (Add, wet only): częstotliwość Sub −1 i Sub −2 (FFT z interpolacją, po 5 cyklach) | f/2 i f/4 z błędem ≤ 0,1% | U · CI |
| T-DIV-02 | SPEC §6.2 | czystość: shape = 0, składowa 3f/2 względem Sub −1 | ≤ −18 dB (P2: −23,4 dB) | U · CI |
| T-DIV-03 | SPEC §6.2 | reguła polaryzacji na wyjściu domyślnym (shape 0, bramka), Mix i Live: SIG-03 (20 uderzeń) w ciszy, z szumem 30–120 Hz −90…−60 dBFS RMS i z buczeniem 31 Hz −70 dBFS; oraz nakładające się ogony (T60 1 s co 0,45 i 0,5 s) | oddzielone uderzenia: zgodność znaku pierwszego płatu 20/20, korelacja przebiegów ≥ 0,99; ogony: natywny sub 20/20, wyjście spójne między uderzeniami (P2: 20/20, 0,9995) | U · CI |
| T-DIV-04 | SPEC §6.2 | SIG-19: dodatkowe przełączenia i błąd okresu | błąd okresu < 2 % dla SNR ≥ 10 dB; przy SNR 3 dB bramka otwarta ≤ 10 % czasu (P2: 2 %; przed F-1: 4 %) | U · CI |
| T-DIV-05 | SPEC §6.1 | SIG-06 (akord, od ciszy, Live i Mix) surowy i po domyślnym paśmie źródłowym 30–120 Hz (F-1, ADR-030) oraz losowy szum 30–120 Hz (SIG-07 po filtrze zakresu): bramka pewności; także `conf_gate` 0 % i 100 % (szum po nucie) | akord: poziom suba ≤ źródło − 40 dB po 100 ms (`conf_gate` = 50; P2: surowy −76 dB Live, −116 dB Mix; po paśmie 30–120 Hz −51 dB Live, −90 dB Mix, przed F-1 −10 dB); szum: bramka otwarta ≤ 3 % czasu, poziom suba ≥ 12 dB poniżej szumu (P2: 0 %; przed F-1: 0–2 %, ≥ 15 dB); przy 0 % i 100 % bramka zamyka się w szumie | U · CI |
| T-DIV-06 | SPEC §6.2 | ciągłość przy przełączeniach (shape = 100): skok międzypróbkowy i widmo HF | skok ≤ 1 próbka kroku sygnału; energia > 2 kHz ≤ −60 dB względem szczytu | U · CI |
| T-DIV-08 | SPEC §6.1, §6.2 | początek nuty przez bramkę: SIG-03 przez filtr zakresu, Live, Mix, Print | energia suba w pierwszych 40 ms ≥ 50 % energii bez bramki (P2: 94 / 95,5 / 95,5 %; wersja 0.2: 0,5 %) | U · CI |
| T-DIV-09 | SPEC §6.2 | `shape` 0…100 % na sinusie 55 Hz | poziom składowej f/2 stały ± 0,5 dB (P2: 0,05 dB); shape 100 %: stosunek 3f/2 do f/2 w ± 1 dB od natywnego | U · CI |
| T-DIV-07 | SPEC §6.1 | Follow/Constant: poziom suba dla SIG-01 −40…−6 dBFS | Constant −12 dBFS ± 0,5 dB; Follow ∝ źródło | U · CI |
| T-TRK-01 | SPEC §6.3 | SIG-01, SIG-04, SIG-05: dokładność f0 i błędy oktawowe | błąd ≤ 0,5%; 0 błędów oktawowych | U · CI |
| T-TRK-02 | SPEC §6.3 | glissando 80 → 40 Hz w 200 ms: opóźnienie śledzenia | ≤ 50 ms przy W = 83,5 ms (`range_lo` = 30 Hz; P2: 43 ms ≈ W/2 + 1 ms) | U · CI |
| T-TRK-03 | SPEC §6.3 | wyrównanie początku suba z początkiem źródła w Live/Mix/Print (SIG-03, SIG-04) | Live: opóźnienie raportowane; Mix/Print: ±2 ms dla nut ≥ 55 Hz (Mix) i ≥ 41,2 Hz (Print) | U · CI |
| T-TRK-04 | SPEC §6.3 | `phase_gen`: Locked — błąd fazy 2·φ_sub − φ_in; Reset — faza w onsecie | Locked ≤ 15° rms w stanie ustalonym (P2: 1,35° czysty, 5,9° przy SNR 10 dB); Reset 0° ± 1° | U · CI |
| T-TRIG-01 | SPEC §6.4 | SIG-03: czas wykrycia onsetu, podwójne wyzwolenia | Mix/Print: ≤ 1 próbka; Live: ≤ 2 ms; 0 podwójnych | U · CI |
| T-TRIG-02 | SPEC §6.4 | determinizm: 100 identycznych uderzeń (porównanie uderzeń 1–99; uderzenie 0 nie ma ogona poprzednika); różne rozmiary bloków | różnica głosów ≤ −120 dB (P2: −211 dB) | U · CI |
| T-TRIG-03 | SPEC §6.4 | f(t) i A(t) vs wzory, mierzone w dziedzinie czasu: połowy okresu między przejściami przez zero (f) i próbki szczytowe (A) — sygnał analityczny myli się dla zanikającego chirpa o 3–8 % i 0,3–0,7 dB | błąd częstotliwości ≤ 0,5 %; amplitudy ≤ 0,1 dB (P2: 0,0002 % / 0,023 dB) | U · CI |
| T-TRIG-04 | SPEC §6.4 | Restart: maksymalna druga różnica w oknie ±1 ms wokół retriggera; Legato: przeniesienie fazy i amplitudy; atak 0 przy fazie 90° | Restart ≤ 1e-3 pełnej skali (P2: 4,3·10⁻⁴); Legato: faza i amplituda przeniesione dokładnie (≤ 1e-12); atak 0 / faza 90°: skok ≤ 0,06 pełnej skali (efektywny atak 0,5 ms) | U · CI |
| T-HARM-01 | SPEC §6.5 | sinus 1,0 → wagi harmonicznych vs `w_k`; DC po czyszczeniu | błąd ≤ 0,5 dB; DC ≤ −80 dBFS (P2: 0,017 dB; −248 dBFS) | U · CI |
| T-HARM-02 | SPEC §6.5 | SIG-17: produkty intermodulacyjne | raport (informacyjnie; P2: 72 % mocy wyjścia to intermodulacja, najsilniejsza linia f1 + f2) | U · L |
| T-HYB-01 | SPEC §6.6 | Hybrid = suma niezależnych silników | null ≤ −120 dB | U · CI |
| T-ENG-01 | SPEC §6.1 | przełączanie silników w trakcie sygnału | brak klików (skok ≤ próg), stała latencja | I · CI |

### 3.3 Tor sub
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-DRV-01 | SPEC §7.3 | widmo harmonicznych per charakter i poziom vs implementacja wzorcowa 16× | błąd ≤ 1 dB dla harmonicznych ≥ −60 dB | U · CI |
| T-DRV-02 | SPEC §7.3 | ADAA1 vs naiwne: Hard przy drive = 100% | moc nieharmoniczna (81 Hz) niższa o ≥ 6 dB (informacyjnie; P2: 7,8–9,7 dB; K-21) | U · L |
| T-DRV-03 | SPEC §7.3 | Auto Gain: RMS sinusa referencyjnego dla drive 0…100, wszystkie charaktery i bias; z Tone 60 / 500 / 2000 Hz przy `f_sub1` ∈ {30, 60, 120 Hz} (D4) | odchyłka ≤ ±0,5 dB (P2 bez Tone: 0,036 dB) | U · CI |
| T-DRV-04 | SPEC §7.3 | DC po Drive asymetrycznym i po filtrze 5 Hz | ≤ −80 dBFS | U · CI |
| T-DRV-05 | SPEC §7.3 | `drive` = 0 tożsamość; ciągłość wtapiania α | bit-exact (double); ciągłość α: zmiana wyjścia proporcjonalna do kroku parametru, ≤ 1·10⁻² względnie na krok 0,1 % `drive` (P2: 4,2–7,2·10⁻³, liniowo z krokiem) | U · CI |
| T-DRV-06 | SPEC §7.3, SPEC_IO_UI §4 | tryb LFE: harmoniczne powyżej `lfe_lp_freq` | tłumienie zgodne z nachyleniem LPF | U · CI |
| T-ALIAS-01 | SPEC §4.5 | tony 31, 81, 121 Hz (30/80/120 Hz przy 48 kHz są „ślepe”) przez Drive we wszystkich trybach przy drive 25/50/100 %, wejścia −6, 0, +6, +12 dBFS (`sub_level` stoi przed Drive), fs 44,1 i 48 kHz; moc składowych nieharmonicznych względem całości | w paśmie toru sub (< 500 Hz): ≤ −90 dB (Mix, Print), ≤ −80 dB (Live) (P2 po R3: −93,2 / −93,2 / −90,2 dB); pełnopasmowo informacyjnie (P2, Hard 100 %, −6 dBFS: Live −55, Mix −77, Print −95 dB) | U · CI |
| T-CMP-01 | SPEC §7.4 | krzywa statyczna vs wzór, ciągłość kolana | ≤ 0,1 dB | U · CI |
| T-CMP-02 | SPEC §7.4 | THD dodane przez kompresor dla sinusów 30–100 Hz przy GR 6 dB | ≤ −60 dB dla detektorów RMS i Peak z SPEC §7.4 przy dowolnych czasach (atak 0,1–100 ms, release 10–1000 ms; P2: −124 dB); na onsecie (ton 40 Hz, GR 0 → 18 dB, energia > 100 Hz w pierwszych 80 ms): Peak przy ataku ≤ 3 ms ≤ −50 dB, RMS ≤ −40 dB (P2: −50,4 / −42,1 dB) | U · CI |
| T-CMP-03 | SPEC §7.4 | dokładność ataku/release (czas do 63 %; okno detektora dodaje się do release) | ≤ 10% | U · CI |
| T-GATE-01 | SPEC §7.5 | próg, hold, release, histereza | dokładność 10%; brak trzepotania na progu | U · CI |
| T-LIM-01 | SPEC §7.8 | szczyt nie przekracza sufitu (sygnały stresowe, SIG-12) | \|y\| ≤ sufit + 1e-9 (P2: 3·10⁻¹⁶); true-peak ≤ sufit (Print) | U · CI |
| T-LIM-02 | SPEC §7.8 | THD przy 40 Hz, wejściu sufit + 6 dB i release 100 ms | Mix ≤ −35 dB, Print ≤ −50 dB; Live informacyjnie (P2: −38,4 / −51,8 / −15,2 dB) | U · L |
| T-EQ-01 | SPEC §7.2 | pasma PEQ: odpowiedź, tożsamość przy `eqN_on` off lub 0 dB | ≤ 0,05 dB | U · CI |
| T-PHS-01 | SPEC §7.7 | polaryzacja | suma z kopią odwróconą = 0 dokładnie | U · CI |
| T-PHS-02 | SPEC §7.7 | rotacja faz θ w 30–300 Hz | ± 0,2°; amplituda ± 0,02 dB (P2: 0,133° / 0,010 dB) | U · CI |
| T-PHS-03 | SPEC §8.B | Auto-Align na SIG-18 oraz scenariuszach syntetycznych ze znanym optimum (sub po gałęzi A; wynik kanoniczny \|θ\| ≤ 90° + polaryzacja) | wybór w granicach ± 15° od optimum (P2: 7,5°); brak optimum → niska pewność | U · L |
| T-SRC-01 | SPEC §5.1 | średnia ważona źródła: identyczne kanały dają jedność; mono bez zmian; wagi klas z SIG-15 | ± 0,01 dB | U · CI |
| T-SRC-02 | SPEC §5.2, §8.A | SIG-08 w trybie Auto vs Sum | Auto: sub ≥ −6 dB względem wariantu współfazowego; Sum: ostrzeżenie W-03 | U · CI |
| T-SRC-03 | SPEC §5.2 | Max: przełączanie z histerezą 1,5 dB i crossfade | brak klików; brak migotania | U · CI |

### 3.4 Sumowanie, latencja, niezmienniczość
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-SUM-01 | SPEC §1.5, §9.1 | `sub_level` Off (Add), wszystkie `quality` i layouty (L1+L2) | wyjście = wejście opóźnione o L_total, bit-exact (float32 i float64) | I · CI |
| T-SUM-02 | SPEC §9.1 | Mix: `mix` = 0 → dry bit-exact; 100 → tylko sub; Equal Power a² + b² = 1 | ± 1e-12 | U · CI |
| T-SUM-03 | SPEC §9.1 | Wet Only + Dry only = pełny mix | null ≤ −140 dB | I · CI |
| T-SUM-04 | SPEC §4.8 | Bypass = wejście opóźnione o L_total; crossfade 10 ms | bit-exact w stanie ustalonym; brak klików | I · CI |
| T-SUM-05 | SPEC §9.1 | `out_trim` = 0 dB | bit-exact | U · CI |
| T-LAT-01 | SPEC §4.4 | impuls (SIG-09): opóźnienie dry; początek suba | dry = L_total dokładnie; sub ± 1 próbka względem L_sub (dla zdarzeń Trigger) | I · CI |
| T-LAT-02 | SPEC §4.4 | stałość latencji względem silnika, Drive, Comp, EQ oraz `sub_lp_freq`/`sub_lp_slope` przy Linear Phase | bez zmian | I · CI |
| T-ALN-01 | SPEC §9.2 | akcja Auto dla `f_sub1` ∈ {30, 40, 50, 80 Hz} i kilku ustawień pasma, filtrów i rotacji | `align_ms` = analityczne opóźnienie grupowe toru sub (pasmo źródłowe przy częstotliwości źródła + filtry toru sub + gałąź A, SPEC §9.2) ± 0,1 ms, ograniczone do 0–40 ms | U · CI |
| T-INV-01 | SPEC §4.3 | rozmiary bloków {1, 7, 32, 64, 127, 128, 512, 4096, losowe} | różnica ≤ −130 dB (cel: bit-exact bez ścieżek FFT) | I · CI |
| T-INV-02 | SPEC §3 | fs ∈ {44,1; 48; 88,2; 96; 176,4; 192 kHz} | stosunki częstotliwości i faz spójne; latencja w ms ± 1 próbka | I · CI |
| T-INV-03 | SPEC §3 | render offline (`setNonRealtime`) vs czas rzeczywisty | identyczny wynik | I · CI |
| T-INV-04 | SPEC §1.5 | dwa uruchomienia | identyczne bit-w-bit | I · CI |

### 3.5 Real-time, stabilność
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-RT-01 | SPEC §4.6 | hook alokacji/blokad w teście; ASan/UBSan/TSan; RTSan (jeśli dostępny) | 0 alokacji/blokad w `processBlock`; 0 błędów | U/I · CI |
| T-RT-02 | SPEC §4.7 | SIG-14 (denormale) | czas CPU ≤ +20% względem normalnego; brak NaN | U · CI |
| T-RT-03 | SPEC §4.7 | SIG-13 (NaN/Inf) | sub skończony, stany zresetowane; dry nietknięte | U · CI |
| T-STAB-01 | SPEC §4.6 | ≥ 10⁶ losowych zmian wszystkich parametrów A na losowych sygnałach | brak NaN/Inf; skok ≤ progu dB/ms; brak wzrostu pamięci | I · CI |
| T-STAB-02 | SPEC §4.7 | kombinacje skrajnych wartości (pokrycie parami) przy pełnej skali | wyjście skończone; limiter honoruje sufit | I · CI |
| T-STAB-03 | SPEC §4.8 | SIG-09, 10, 11, 12 | stabilność; cisza po ciszy ≤ −120 dB po 1 s | I · CI |
| T-STAB-04 | SPEC §4.4 | soak 8 h materiału (offline) przed wydaniem | brak wzrostu pamięci i dryfu CPU | I · L |
| T-STAB-05 | SPEC §4.4 | zmiana `quality`, `filt_phase`, `align_ms` w trakcie | brak NaN; wyciszenie/rampa wg SPEC | I · CI |

### 3.6 I/O, surround, LFE
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-IO-01 | SPEC_IO_UI §2.1 | wszystkie layouty (L1+L2) z SIG-15 | dry w każdym kanale bit-exact; sub tylko tam, gdzie przewiduje tryb | I · CI |
| T-IO-02 | SPEC_IO_UI §1 | permutacje kolejności kanałów (Film, Logic, VST3, DTS) | wynik per klasa identyczny | I · CI |
| T-IO-03 | SPEC_IO_UI §2.2 | pary x.0 → x.1 | LFE zawiera sub; reszta dry | I · CI |
| T-IO-04 | SPEC_IO_UI §3 | przecieki między kanałami (kanały bez routingu) | ≤ −120 dB | I · CI |
| T-IO-05 | SPEC_IO_UI §2.2 | layout nieobsługiwany (ambisonic) | czyste odrzucenie, przepuszczanie, W-12 | I · CI |
| T-IO-06 | SPEC §5.1 | sidechain: mono/stereo, włączony/wyłączony, `sc_use` Detect/Trigger | poprawne źródło; W-02 | I · CI |
| T-LFE-01 | SPEC_IO_UI §4 | szum szerokopasmowy (SIG-16) → LFE: tłumienie powyżej 1,5·`lfe_lp_freq` i poniżej `lfe_hp_freq` | zgodne z nachyleniem (np. 24 dB/okt → ≥ 14 dB przy 1,5×) | I · CI |
| T-LFE-02 | SPEC_IO_UI §4 | tryb LFE: aktywny tylko LFE; Wet Only | przecieki ≤ −120 dB | I · CI |
| T-LFE-03 | SPEC_IO_UI §4 | sufit LFE na sygnałach stresowych | szczyt ≤ `lfe_ceiling` | U · CI |
| T-LFE-04 | SPEC_IO_UI §4 pkt 7 | bleed | poziom ± 0,1 dB; pasmo ≤ 120 Hz | U · CI |
| T-LFE-05 | SPEC_IO_UI §7.4 | logika W-01 i W-05 (testowalna bez UI) | oczekiwane stany | U · CI |
| T-LFE-06 | SPEC_IO_UI §6 | metering LFE: RMS/peak sinusa i szumu 20–120 Hz | ± 0,1 dB | U · CI |
| T-LFE-07 | SPEC_IO_UI §4 | opóźnienie grupowe LPF przy 40 Hz: wyświetlane vs zmierzone | ± 2% | U · CI |
| T-SUR-01 | SPEC_IO_UI §3 | tryb Surround: wzmocnienia klas, opóźnienia, polaryzacje | wzmocnienie ± 0,01 dB; opóźnienie dokładne; polaryzacja dokładna | U · CI |
| T-SUR-02 | SPEC_IO_UI §3 | `sub_width` = 0 → L = R; 100 → niezależne; wzór | zgodne ± 1e-9 | U · CI |

### 3.7 Stan i presety
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-STATE-01 | SPEC_IO_UI §9 | zapis/odczyt wszystkich 141 parametrów, stan UI, A/B | identyczne bit-w-bit (parametry) | I · CI |
| T-STATE-02 | PARAMETERS §5 | ładowanie złotych stanów z poprzednich wersji w nowszej | parametry zachowane lub neutralne; render identyczny | I · CI |
| T-STATE-03 | SPEC_IO_UI §8 | 32 presety fabryczne: wczytanie, render na SIG-03/04/05 | brak NaN; szczyt ≤ 0 dBFS; poziom sensowny | I · CI |
| T-STATE-04 | SPEC_IO_UI §9 | uszkodzony i nieznany stan (fuzz) | domyślne wartości, brak awarii | I · CI |
| T-STATE-05 | SPEC_IO_UI §8 | Undo/Redo | poprawna kolejność i wartości | U · CI |

### 3.8 Formaty, walidatory, instalatory
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-FMT-01 | SPEC_BUILD_RELEASE §3 | pluginval 1.0.4 `--strictness-level 10 --validate` na VST3 | PASS (macOS i Windows) | V · CI |
| T-FMT-02 | SPEC_BUILD_RELEASE §3 | `auval -v` dla AU | PASS | V · CI/L |
| T-FMT-03 | SPEC_BUILD_RELEASE §3 | validator Steinberga dla VST3 | PASS [S] | V · CI |
| T-FMT-04 | SPEC §2 | AAX Validator (po włączeniu AAX) | wersja 1.1 | V · M |
| T-FMT-05 | SPEC §3 | `lipo -info` (macOS): arm64 + x86_64; Windows: x64 | zgodne | V · CI |
| T-FMT-06 | SPEC §3 | zależności dynamiczne (`otool -L`, `dumpbin /dependents`); statyczne CRT | brak zbędnych | V · CI |
| T-INST-01 | SPEC_BUILD_RELEASE §4.1 | macOS pkg: zawartość i ścieżki; instalacja na czystym koncie; `uninstall.sh` | wszystko instalowane i usuwane | M/CI |
| T-INST-02 | SPEC_BUILD_RELEASE §4.2 | Windows exe: cicha instalacja na czystym runnerze, pluginval zainstalowanej kopii, cicha dezinstalacja, wpis rejestru | PASS, brak pozostałości | CI |
| T-INST-03 | SPEC_BUILD_RELEASE §4.3 | zgodność SHA-256 z artefaktami | równe | CI |
| T-INST-04 | SPEC_BUILD_RELEASE §5 | próba workflow release (`workflow_dispatch`/tag testowy) → draft z kompletem plików | draft kompletny; nic nie opublikowane | CI |

### 3.9 Hosty (ręczne, `T-HOST`)
| ID | Zakres | Kryterium |
|---|---|---|
| T-HOST-01 | Cubase 13 (VST3): instancjonowanie dla stereo, 5.1, 7.1.2, 7.1.4; automatyzacja 10 parametrów; PDC (latencja zgodna z zegarem sesji); bounce offline vs czas rzeczywisty; zapis/odczyt projektu; wiele instancji; bypass; sidechain; para 5.0 → 5.1 | wszystko zgodne z oczekiwaniami; zrzuty/notatki w TEST_REPORT |
| T-HOST-02 | AU (macOS): GarageBand (jeśli ładuje) lub inny host AU — instancjonowanie, preset, automatyzacja | działa |
| T-HOST-03 | Windows: dowolny host VST3 (Reaper lub inny) — instancjonowanie, 5.1, instalator | działa (opis w TEST_REPORT) |
| T-HOST-04 | Odsłuch (T-LIST): zestaw A/B z wyrównaną głośnością | notatki użytkownika |

### 3.10 UI i wydajność
| ID | Wymaganie | Metoda | Kryterium | Poziom · gdzie |
|---|---|---|---|---|
| T-UI-01 | SPEC_IO_UI §7 | otwieranie/zamykanie edytora 1000×, skrajne rozmiary okna | brak wycieków, brak wpływu na wątek audio (czas callbacku ≤ +5%) | I · CI/L |
| T-UI-02 | SPEC_IO_UI §7.4 | wszystkie ostrzeżenia W-01…W-12 pojawiają się według warunków (logika testowalna) | zgodnie z tabelą | U · CI |
| T-PERF-01 | SPEC_BUILD_RELEASE §7 | benchmark konfiguracji z tabeli budżetów | w budżecie (mediana i p99) | P · L/CI |
| T-PERF-02 | SPEC_BUILD_RELEASE §7 | skoki CPU | p99 ≤ 3 × mediana | P · L |
| T-LIST-01 | SPEC §1.2 | pliki odsłuchowe: kick/808/bas/impact/LFE; bez i z subem, wyrównana głośność | Twoje notatki w TEST_REPORT | M · M |

## 4. Kryteria zakończenia faz („Definition of Done”)
| Faza | Warunek |
|---|---|
| 2 (prototyp) | P2-01…P2-11 wykonane; `PHASE2_REPORT.md` z wartościami do SPEC; pliki odsłuchowe gotowe — **spełnione 2026-10-20** |
| 3 (rdzeń) | testy U dla `dsp`, `engines`, `chain`, `routing` zielone; CI zielone na macOS i Windows; pluginval 10 PASS; spike'i SPK-1…SPK-6 zamknięte |
| 4 (UI) | T-UI zielone; wszystkie parametry w UI; makiety zatwierdzone (jeśli wybrano); presety fabryczne załadowane |
| 5 (walidacja) | wszystkie testy z §3 (poza T-FMT-04) zielone; soak 8 h; T-HOST-01…03 wykonane; T-LIST wykonane |
| 6 (wydanie) | T-INST-01…04 PASS; draft Release kompletny; publikacja po Twojej zgodzie |

## 5. Narzędzia
Catch2 (U), headless host JUCE (I; ładuje binarne VST3/AU przez `AudioPluginFormatManager`), Python (`proto/`: generator sygnałów, wartości referencyjne, analiza), pluginval, `auval`, validator VST3, ASan/UBSan/TSan, RealtimeSanitizer (SHOULD), `lipo`, `otool`, `pkgutil`, `dumpbin`, Inno Setup, PowerShell.
