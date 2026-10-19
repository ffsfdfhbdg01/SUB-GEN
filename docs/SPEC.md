# SUB-GEN — SPEC (Faza 1): produkt i rdzeń DSP

Wersja dokumentu: 0.1 (do akceptacji) · Data: 2026-10-19 · Wersja produktu docelowa: 1.0.0
Części specyfikacji: **SPEC.md** (ten plik: produkt, architektura, silniki, tor sub, faza, sumowanie) · [SPEC_IO_UI.md](SPEC_IO_UI.md) (kanały, routing, LFE, metering, UI, stan i presety) · [SPEC_BUILD_RELEASE.md](SPEC_BUILD_RELEASE.md) (build, CI, instalatory, release, budżety, plan Fazy 2, ryzyka) · [PARAMETERS.md](PARAMETERS.md) (rejestr parametrów) · [TEST_PLAN.md](TEST_PLAN.md) (testy i kryteria).
Kontekst: [PROMPT.md](PROMPT.md) (brief v2), [RESEARCH.md](RESEARCH.md), [DECISIONS.md](DECISIONS.md).

## 0. Konwencje czytania
- **MUST / SHOULD / COULD** — priorytet. Zakres v1.0 = wszystko, co nie jest oznaczone SHOULD/COULD.
- **[U]** decyzja użytkownika (DECISIONS B2) · **[W]** decyzja wykonawcy (do Twojego przeglądu) · **[P2]** wartość lub algorytm do dostrojenia/rozstrzygnięcia pomiarem w Fazie 2 · **[S]** wymaga spike'a w Fazie 3.
- Liczby są celami początkowymi; ostateczne wartości zapisuje Faza 2 w `PHASE2_REPORT.md`.
- Identyfikatory w znakach `kodu` (np. `drive`) odnoszą się do rejestru [PARAMETERS.md](PARAMETERS.md). Stałe wewnętrzne mają nazwy UPPER_SNAKE i nie są parametrami.
- Korekty briefu v2 z RESEARCH §11 (K1–K8) są tu zastosowane. Dodatkowo: konwencje poziomu LFE są **dwie**, nie trzy (§10 SPEC_IO_UI), a „Base Frequency” ma jednolitą definicję (§6.1).

## 1. Cel, zakres, nie-cele

### 1.1 Po co
Sub-bas jest najtrudniejszą do uzyskania i najłatwiejszą do zepsucia częścią miksu. Istniejące narzędzia są albo ograniczone do mono/stereo, albo zamknięte i licencjonowane, albo nie łączą muzyki, sound designu i filmu z LFE w jednym narzędziu o mierzalnym, przewidywalnym DSP. SUB-GEN ma być jedną wtyczką od „beef do stopy” po „LFE w 7.1.4”, z pełną kontrolą fazy, uczciwym A/B i otwartym kodem.

### 1.2 Scenariusze (kryterium sukcesu)
| ID | Scenariusz | Typowe ustawienia | Sukces |
|---|---|---|---|
| UC1 | Beef do stopy (bus mono/stereo) | Divide lub Trigger, Add, quality Live/Mix | dodana waga 35–50 Hz bez rozmycia transjentu; polaryzacja subu identyczna uderzenie w uderzenie; brak trzasków |
| UC2 | Sub pod bas/808 | Track lub Divide, Source Range 40–120 Hz | sub podąża za glissandem, czysty sinus, brak błędów oktawowych, spójna faza z dry |
| UC3 | Dół busa/miksu | Divide lub Hybrid, szeroki zakres, kompresor | więcej głębi bez zamulenia; przy sub = Off wynik identyczny z wejściem |
| UC4 | SFX / impact | Trigger, pitch envelope, Drive | powtarzalny boom (deterministyczny), sterowany obwiednią i pitch-dropem |
| UC5 | Film: LFE w 5.1/7.1.2/7.1.4 | tryb LFE | sygnał tylko w kanale LFE, pasmo ≤ 120 Hz, metering z konwencją poziomu, ostrzeżenie o downmixie |
| UC6 | Aux wet-only | Wet Only | na wyjściu wyłącznie sub, latencja skompensowana |

### 1.3 Cele
G1 poprawność i przewidywalność DSP (wszystko mierzalne) · G2 jedna wtyczka dla muzyki, SFX i filmu · G3 kontrola fazy w trzech warstwach · G4 pełne layouty i LFE zgodne z ITU-R BS.775-4 / ATSC A/52 · G5 bezpieczeństwo: real-time safe, brak trzasków i NaN · G6 wydanie z instalatorami macOS i Windows w GitHub Release z CI · G7 przejrzysty UI z Drive/Saturate jako pokrętłem głównym · G8 otwartość (AGPLv3, pełna dokumentacja).

### 1.4 Nie-cele v1.0
Syntezator MIDI/instrument · AAX w release (cel CI) · CLAP · ambisonics · SPECTRAL/HQ · linear-phase PEQ (jest linear-phase dla filtrów HP/LP w Print, §7.6) · wielopasmowe źródło · polifoniczny pitch tracking · strojenie do tonacji (note-lock) · iOS/AUv3 · Linux (poza opcjonalnym jobem CI) · telemetria, sieć, aktywacja/licencjonowanie.

### 1.5 Antycele (czego nie wolno)
1. Modyfikować sygnał dry w konfiguracji domyślnej: przy `sub_level` = Off (Add) lub `mix` = 0 (Mix) wyjście jest identyczne z wejściem po kompensacji latencji.
2. Ukrywać automatyki: każda decyzja algorytmu (bramka pewności, wybór kanału, polaryzacja) ma wskaźnik w UI.
3. Nieuczciwe A/B: zmiana ustawienia nie może zmieniać głośności bez kompensacji (Auto Gain, gain-matched A/B).
4. Niedeterminizm: brak losowości w torze audio; ten sam sygnał i parametry dają ten sam wynik niezależnie od rozmiaru bloku.
5. Obietnice bez pomiaru: żadna deklaracja jakości bez testu w TEST_PLAN.

## 2. Produkt, licencja, wydania
- **Nazwa:** SUB-GEN robocza [U]; nazwa produktu ≠ „SubGen” wybierana najpóźniej przed Fazą 6. Wszystkie identyfikatory zależne od nazwy (nazwa pliku, `BUNDLE_ID`, kody AU, ID instalatora) w jednym miejscu: `cmake/ProductIdentity.cmake` [W].
- **Licencja:** AGPLv3 [U]; JUCE na AGPLv3; VST3 SDK MIT; zależności zgodne z AGPLv3 (Catch2 BSL-1.0, CPM MIT); nagłówki SPDX w każdym pliku źródłowym; `LICENSE`, `THIRD_PARTY_LICENSES.md`. Publiczne repo spełnia obowiązek udostępnienia kodu. AAX SDK (GPLv3) dopiero przy włączeniu AAX.
- **Formaty v1.0 w release:** macOS — VST3 + AU (universal arm64+x86_64); Windows — VST3 (x64). Standalone buduje się do testów, nie wchodzi do instalatorów [W]. AAX: cel w CI, poza release [U]. CLAP: poza v1.0.
- **Wersjonowanie:** SemVer, plik `VERSION`, `CHANGELOG.md` (Keep a Changelog). Wersja wstrzykiwana do JUCE i do instalatorów.

## 3. Ograniczenia
| Obszar | Ograniczenie |
|---|---|
| Języki / narzędzia | C++20, CMake ≥ 3.25, JUCE 9.0.4 jako submoduł git przypięty do taga (JUCE 9 wymaga C++17; C++20 to nasz wybór [W]); Ninja |
| macOS | cel wdrożeniowy 11.0 (arm64 natywnie, x86_64) [W, S]; build lokalny na Command Line Tools |
| Windows | x64, Windows 10 21H2+; MSVC/clang-cl w CI; statyczne CRT, brak wymogu redystrybuowalnych [W, S] |
| Precyzja | wewnętrznie `double`; wejście/wyjście float32 i float64 (`supportsDoublePrecisionProcessing`) |
| Próbkowanie / bloki | 44,1–192 kHz; blok 1–8192 próbek, zmienny; zero założeń o rozmiarze bloku |
| Real-time | zero alokacji, blokad, I/O i wyjątków w wątku audio; wszystkie bufory w `prepareToPlay` |
| Prywatność | brak sieci i telemetrii |

## 4. Architektura

### 4.1 Diagram (strumienie audio)
```
 IN (main) ───────────────────────────────────────────────► [Dry delay L_total] ──────────────┐
   │                                                                                           │
   ├─► S1 Source mixer (Feed, src_gain) ─► S2 Detection (src_mode) ─► S3 Band-limit (Range)    │
   │                                              ▲                        │                    │
 SC (opcjonalnie) ──────────────────────────────┘                        ▼                    │
                                                              S4 Activity / envelope           │
                                                                       │                       │
                                                       S5 ENGINE (Divide·Track·Trigger·Harmonic·Hybrid)
                                                                       │ (1 lub 2 strumienie)  │
                                                       S6 SUB CHAIN: Level → Boost → Gate → Drive* → Comp → Drive* → EQ → Filters → Phase → Limiter
                                                                       │                       │
                                                       S7 Router (out_mode, klasy kanałów, delay, polaryzacja)
                                                                       │                       │
                                                                       └──────► S8 Summation ◄─┘ (Add / Mix / Wet Only) ─► out_trim ─► OUT
```
(* Drive stoi w jednym z dwóch miejsc: `drive_pos`.)

### 4.2 Moduły kodu (katalogi `src/`)
`core` (typy, stałe, narzędzia dB) · `params` (rejestr, layout APVTS, sanitise) · `dsp` (filtry TPT, wygładzanie, oversampling, opóźnienia, obwiednie, FFT) · `engines` (Divide, Track, Trigger, Harmonic, Hybrid) · `chain` (Boost, Gate, Drive, Compressor, Eq, Filters, PhaseTool, Limiter) · `routing` (klasyfikacja kanałów, SourceMixer, Router) · `analysis` (korelacja, Auto-Align, widmo, metry) · `plugin` (Processor, stan, presety) · `ui` (Editor, komponenty, LookAndFeel). Zależności tylko „w dół” według tej kolejności; `dsp`, `engines`, `chain`, `routing` nie zależą od JUCE GUI i kompilują się do statycznej biblioteki testowej.

### 4.3 Przetwarzanie bloku
1. `processBlock`: ScopedNoDenormals → odczyt parametrów (atomic) → `sanitise()` → obliczenie stanu sterującego (tabele wygładzania, współczynniki).
2. Konwersja wejścia do `double` (jeśli blok float) do wstępnie zaalokowanych buforów.
3. Pętla po poddblokach ≤ 64 próbek: aktualizacja współczynników sterujących co poddblok (filtry TPT są stabilne przy modulacji), wygładzanie wzmocnień co próbkę.
4. S1…S8 dla poddbloku; dry zapisuje się do linii opóźniającej równolegle.
5. Konwersja wyjścia do float, zapis do bufora hosta; metry i bufory analizy do FIFO (lock-free).
Latencja i zmiana jakości: §4.4. Brak wątku roboczego w torze audio.

### 4.4 Latencja
```
L_sub  = L_os(quality) + L_look(quality) + L_lin(quality, filt_phase)
L_total = L_sub + round(align_ms · fs / 1000)
```
| quality | Oversampling (Drive, Harmonic) | L_look | L_os | L_lin | L_total (domyślnie) |
|---|---|---|---|---|---|
| Live | 1× (tylko ADAA) | 0 | 0 | 0 | **0 próbek** |
| Mix | 2× polyphase IIR | ≈ 25 ms [P2] | ceil(`getLatencyInSamples()`) | 0 | ≈ 25 ms |
| Print | 4× FIR equiripple | ≈ 50 ms [P2] | ceil(`getLatencyInSamples()`) | (N−1)/2 przy Linear Phase | ≈ 50 ms+ |
- Raportowane przez `setLatencySamples(L_total)`; stała dla danego `quality`, `filt_phase`, `align_ms` — nie zależy od silnika ani parametrów audio. Zmiana `quality`, `filt_phase`, `align_ms` wywołuje ponowne zgłoszenie latencji (parametry N).
- Dry jest opóźnione o **L_total**, sub o L_sub (naturalne opóźnienia toru); `align_ms` jest dodatkowym opóźnieniem dry kompensującym opóźnienie grupowe toru sub (§9.2).
- Oversampler Drive/Harmonic pracuje zawsze w trybach Mix/Print (przy `drive` = 0 sygnał przechodzi przez opóźnienie o tę samą latencję), aby latencja była stała.
- Wartości L_look i L_os potwierdza Faza 2 (kryteria: aliasing, onset aligned, THD).

### 4.5 Oversampling i aliasing
`juce::dsp::Oversampling<double>`: Mix — `filterHalfBandPolyphaseIIR`, Print — `filterHalfBandFIREquiripple`. Oversampling obejmuje wyłącznie stopnie nieliniowe (Drive; wewnętrzne nieliniowości HARMONIC). ADAA 1. rzędu w Drive jest zawsze aktywne (także w Live). Cel: aliasing ≤ −90 dB względem sygnału (TEST_PLAN T-ALIAS).

### 4.6 Wątki i komunikacja
| Wątek | Zadania | Komunikacja |
|---|---|---|
| Audio | całe DSP, zapis do FIFO metrów i buforów analizy | atomic dla parametrów, `AbstractFifo` (SPSC) dla metrów, potrójny bufor dla przebiegów |
| UI (message) | rysowanie, edycja parametrów, odczyt metrów (30–60 fps) | tylko odczyt FIFO |
| Analiza (tło) | Auto-Align, projekt jąder FIR, FFT widma | pobiera dane z kolejek audio; wynik przez atomic/trójbufor |
Reguły: żadnych `new/delete`, mutexów, `std::function` alokujących ani logowania w wątku audio; parametry czyta się z `std::atomic<float>*` z APVTS; wszystkie bufory ustalone w `prepareToPlay` na maksymalne wartości.

### 4.7 Polityka numeryczna
- Podwójna precyzja we wszystkich stopniach; filtry TPT (Cytomic SVF) i kaskady biquadów o strukturze transposed DF-II w `double`.
- `ScopedNoDenormals` + FTZ/DAZ; dodatkowo resetowanie stanów po ciszy > 5 s [W].
- Straż NaN/Inf: wejście źródłowe i wyjście toru sub są sprawdzane (`std::isfinite`); wejście nieskończone → 0 i flaga; wyjście nieskończone → wyzerowanie sub, reset stanów silnika i filtrów, dry nietknięte.
- Brak niestabilności filtrów przy `fs` ≤ 192 kHz i 15 Hz: weryfikacja względem SciPy (TEST_PLAN T-FILT).
- DC: HPF subsonic usuwa DC; po Drive asymetrycznym 1-biegunowy HP 5 Hz usuwa składową stałą przed kompresorem/bramką.

### 4.8 Reset, aktywacja, bypass
- `prepareToPlay` i `reset()` zerują wszystkie stany i linie opóźniające; zmiana sample rate lub layoutu wymusza `prepareToPlay`.
- Bypass: parametr `bypass` (crossfade 10 ms) między wyjściem przetworzonym a dry opóźnionym o L_total; latencja pozostaje stała.
- Cisza na wejściu: aktywność źródła (S4) wygasza silniki; bez NaN i bez trzasków.

## 5. Tor sygnałowy: kroki S1–S8

### 5.1 S1 Source mixer
- Kanały wejściowe klasyfikowane po `AudioChannelSet::getTypeOfChannel` na klasę (Front, Center, Surround, Height, LFE) i stronę (L/R/C) — tabela w SPEC_IO_UI §1. Nigdy po indeksie; bufor adresowany przez `getChannelIndexInProcessBlockBuffer`.
- Wagi `w_i` = 10^(feed_klasa/20) (−60 dB → 0). Dla grupy lewej (kanały lewe + Center) i prawej (prawe + Center):
  `S_L = Σ w_i·x_i / Σ w_i` (po kanałach grupy lewej), analogicznie `S_R`; jeśli mianownik = 0, strumień = 0. Layout mono: `S_L = S_R = x`. Średnia ważona zachowuje poziom dla identycznych kanałów.
- Następnie `src_gain`. Sidechain: `sc_use` = Detect → `S_L = S_R` = suma kanałów szyny klucza × `sc_gain`; Trigger → klucz zasila wyłącznie detektor TRIGGER (S5); Off → bez zmian.

### 5.2 S2 Detekcja (`src_mode`)
| Tryb | Wynik |
|---|---|
| Sum | jeden strumień `s = (S_L + S_R)/2` |
| Max | jeden strumień; wybierany jest kanał o większym RMS (okno 200 ms) z histerezą 1,5 dB i crossfade 30 ms |
| Per-Channel | dwa strumienie (`S_L`, `S_R`) — dwie niezależne instancje silnika |
| Auto | wg korelacji ρ (§8.A): Sum dla ρ ≥ +0,25; Max dla ρ ≤ −0,25; płynne przejście pomiędzy |
Wymiar strumieni (1 lub 2) determinuje liczbę instancji S5/S6.

### 5.3 S3 Pasmo źródłowe
`x_b = LP(range_hi, range_slope) ∘ HP(range_lo, range_slope)` — Butterworth TPT, zawsze minimum-phase (nie zależy od `filt_type`). Dla `base_mode` = Fixed efektywne pasmo zależy od silnika (§6.1).

### 5.4 S4 Aktywność i obwiednia
- Obwiednie z `x_b`: `e_pk` (peak, atak 1 ms, release 50 ms) oraz `e_rms` (RMS, stała 20 ms).
- Aktywność A = `e_rms` (dB) przekracza `det_thresh`; histereza 3 dB, hold 20 ms. Przy A = 0 silniki wyciszają wyjście z release 20 ms.

### 5.5 S5–S8
S5 silniki (§6) → S6 tor sub (§7) → S7 router (SPEC_IO_UI §3) → S8 sumowanie (§9).

## 6. Silniki

### 6.1 Zasady wspólne
- **Definicja „Base Frequency”** (`base_freq`, `base_mode`) [W]: częstotliwość **Sub −1** dla DIVIDE, TRACK i TRIGGER; podstawowa źródła do „zwirtualizowania” dla HARMONIC. W trybie Auto wartość jest śledzona z sygnału (HARMONIC: f0 źródła); w trybie Fixed jest ustawiana ręcznie:
  - DIVIDE Fixed: pasmo źródłowe = pasmo środkowe 2·`base_freq` ±½ oktawy (BPF 24 dB/okt), `range_*` ignorowane; Sub −1 = `base_freq`, Sub −2 = `base_freq`/2.
  - TRACK Fixed: oscylator na stałej częstotliwości `base_freq` (dron bramkowany obwiednią źródła), bez śledzenia.
  - TRIGGER: `base_freq` = częstotliwość końcowa f_end; Auto → f_end = f0/2 śledzone w momencie wyzwolenia, ograniczone do 20–100 Hz.
  - HARMONIC Fixed: pasmo środkowe `base_freq` ±½ oktawy.
- **Poziomy:** wyjście silnika = `sub1_level`·Sub1 + `sub2_level`·Sub2 (HARMONIC: `sub1_level`·harmoniczne). `shape` morfuje sinus → kształt natywny.
- **Obwiednia (`follow`):** Follow — amplituda wynika z `e_rms` źródła z czasami `env_attack`/`env_release`; Constant — stała amplituda (−12 dBFS peak) bramkowana aktywnością. `dynamics` = makro: wartości ujemne skracają `env_release` i dodają krótki akcent transjentu (ułamek `e_pk` − `e_rms` przez 5 ms), dodatnie wydłużają release i wygładzają atak [P2].
- **Przełączanie silników:** równomocny crossfade 30 ms; silnik wchodzący jest wcześniej „rozgrzewany” 200 ms buforowanego `x_b`. Latencja się nie zmienia.
- **Bramka pewności** (DIVIDE, TRACK): pewność c ∈ [0,1]; poniżej progu `0,2 + 0,6·conf_gate/100` sub wygasa w 50 ms. Wskaźnik w UI.

### 6.2 DIVIDE — dzielnik oktawowy
Opis: iloczyn sygnału pasmowego ze znakiem przełączanym w przejściach przez zero daje przebieg o połowie częstotliwości z zachowaniem kształtu fali (jak dbx 120A i „inwersja fali” u SSL) [RESEARCH §4, §6].

**Stan:** `armed_up`, `armed_dn` (bool), `s1`, `s2` ∈ {±1}, `t_prev` (czas ostatniego narastającego przejścia, ułamkowy), `f_est`, `c`.
**Próg uzbrajania:** `θ = max(0,05·e_pk, 10^(−90/20))`. W Live `θ = max(0,03·e_pk, …)` (mniejszy, bo brak lookahead).
**Pętla na próbkę n (x = x_b):**
```
if x[n] < -θ:  armed_up = true
if x[n] >  +θ:  armed_dn = true
if armed_up and x[n-1] < 0 and x[n] >= 0:              // narastające przejście przez zero
    frac = -x[n-1] / (x[n] - x[n-1]);  t = (n-1) + frac
    T = t - t_prev;  t_prev = t
    armed_up = false;  s1 = -s1
    if s1 == +1: arm2_pending = true                    // co drugi cykl
    update f_est (mediana z 3 ostatnich T → 1-biegunowy filtr 5 Hz), update c
if armed_dn and x[n-1] > 0 and x[n] <= 0:              // opadające przejście przez zero
    armed_dn = false
    if arm2_pending: s2 = -s2; arm2_pending = false     // Sub2: flip co drugie opadające przejście
y1[n] = x[n] * s1
y2[n] = y1[n] * s2
```
Uzasadnienie ciągłości: przełączenia następują dokładnie w przejściach x przez zero, więc `y1` i `y2` są ciągłe w momentach przełączeń (nieciągłość ≤ |x[n]| przy pierwszej próbce po przejściu). `y1` ma okres 2T (f/2), `y2` okres 4T (f/4).
**Polaryzacja początkowa (reguła):** przy przejściu ciszy → aktywność `s1 = +1`, `s2 = +1`, uzbrojenie według stanu x — pierwszy płat suba pokrywa się z pierwszym płatem źródła, więc polaryzacja jest spójna uderzenie w uderzenie.
**Wygładzanie / `shape`:** `y_smooth` = filtr dolnoprzepustowy 4. rzędu Butterworth TPT o częstotliwości odcięcia `0,87·f_est` dla Sub −1 i `0,87·f_est/2` dla Sub −2, aktualizowanej co cykl z poślizgiem 5 ms; `y_out = shape·y_native + (1 − shape)·y_smooth` (ułamki 0…1). Sinus (shape 0) ma resztkowe harmoniczne 3f/2 ≲ −18 dB względem Sub −1 [P2].
**Amplituda:** Follow — natywna (amplituda x); Constant — `y/e_pk · 10^(−12/20)` z wygładzeniem `e_pk`.
**Pewność c:** `c = 1 − min(1, |T_k − T_{k−1}| / (0,15·T_{k−1}))`, wygładzane 3 cyklami; dodatkowo c = 0, gdy w oknie 1 s ≥ 3 niedopuszczalne skoki okresu (polifonia/szum).
**Latencja własna:** 0 próbek; opóźnienie grupowe filtrów zakresu `x_b` jest naturalne (kompensuje je `align_ms`, §9.2).
**Warianty do porównania w Fazie 2 [P2]:** (a) dzielnik jak wyżej; (b) „phase halving” z sygnału analitycznego (Hilbert) — `y = A·cos(φ/2)`; kryteria: błąd częstotliwości, THD, odporność na szum, jitter przy przejściach, zgodność fazy z reguła polaryzacji.

### 6.3 TRACK — śledzenie wysokości + oscylator
- **Detektor f0:** NSDF/MPM na sygnale `x_b` zdecymowanym do `fs_d` ≈ 2 kHz (filtr antyaliasingowy 8. rzędu); okno `W = ceil(2,5·fs_d / max(range_lo, 20 Hz))`, hop 2 ms; szczyt kluczowy z progiem 0,9 i interpolacją paraboliczną; wynik `f0 = fs_d/τ`. Alternatywy do rozstrzygnięcia w Fazie 2: YIN, PLL [P2].
- **Błędy oktawowe:** kandydaty f0, 2·f0, f0/2 oceniani wg ciągłości względem poprzedniej wartości (okno ±25%); skok oktawowy akceptowany po 3 kolejnych zgodnych ramkach.
- **Szybkość:** `trk_speed` dobiera wygładzanie f0 (stała 5…80 ms) i próg zmiany; `trk_glide` dodaje portamento (liniowe w log f).
- **Oscylator:** akumulator fazy, częstotliwości Sub −1 = f0/2, Sub −2 = f0/4 (lub `base_freq` i `base_freq`/2 w Fixed). Kształt: `(1 − shape)·sin φ + shape·tri(φ)` (trójkąt naiwny wystarczy: aliasing < −90 dB dla f ≤ 100 Hz, fs ≥ 44,1 kHz).
- **Faza (`phase_gen`):** Locked — `φ_sub = φ_in/2 + c0` (c0 ∈ {0, π} wybierane regułą polaryzacji jak w DIVIDE), φ_in z sygnału analitycznego pary Hilberta; Free — czysta akumulacja z f0; Reset — reset fazy do 0 przy zdarzeniu onset (detektor z §6.4) i dalej Free. Alternatywa Locked: PLL o paśmie pętli ~5 Hz [P2].
- **Amplituda:** Follow — `e_rms` źródła z `env_attack`/`env_release`; Constant — stała bramkowana aktywnością.
- **Latencja i lookahead:** Live — brak lookahead, estymata f0 opóźniona o ~W/2; Mix/Print — wyjście silnika korzysta z estymat obliczonych L_look do przodu, więc początek suba pokrywa się z początkiem źródła.

### 6.4 TRIGGER — sub syntetyczny wyzwalany transjentem
- **Detekcja onsetu:** obwiednia szybka `e_t` (atak 0,2 ms, release 15 ms) z `x_b` lub z klucza (`sc_use` = Trigger); zdarzenie, gdy `e_t` przekracza `trig_thresh` z histerezą 3 dB, po upływie `trig_lockout`; ponowne uzbrojenie po spadku `e_t` poniżej progu − 3 dB przez `trig_hold`. Czas zdarzenia z interpolacją liniową przecięcia (ułamek próbki).
- **Głos:** `φ(t) = φ0 + 2π ∫ f(t) dt`, `f(t) = f_end + (f_start − f_end)·exp(−t/τ_p)` z `f_start = trig_f_start`, `τ_p = trig_pitch_time`, `φ0 = trig_phase`. Sub −2 = ta sama obwiednia i f/2.
- **Obwiednia:** `a(t) = t/t_a` dla t < t_a (`t_a = trig_attack`), potem `10^(−3·(t − t_a)/T60)` z `T60 = trig_decay`. Prędkość: `V = clamp((peak/0,5)^(trig_vel/100), 0, 1,5)` (peak — szczyt `x_b` w 20 ms po onsecie; `trig_vel` = 0 → V = 1).
- **Follow/Constant:** Follow dodatkowo mnoży obwiednię przez wygładzone `e_rms` źródła (sub kończy się wraz ze źródłem); Constant — czyste A(t).
- **Retrigger (`trig_retrigger`):** Restart — nowy głos od fazy φ0 z 3 ms crossfade ze starego; Legato — faza ciągła, obwiednia i pitch od początku.
- **Determinizm:** przy tym samym wejściu każdy głos jest identyczny (faza startowa stała) — wymóg testu T-TRIG-02.
- **Lookahead:** Mix/Print opóźniają dry o L_look, więc głos startuje dokładnie na transjencie dry; Live startuje w próbce wykrycia.

### 6.5 HARMONIC — wysokość wirtualna
- **Wejście:** `x_b` (Auto: `range_*`; Fixed: ±½ oktawy wokół `base_freq`).
- **Normalizacja:** `x_n = clamp(x_b / max(e_pk_s, E_FLOOR), −1, 1)`, gdzie `e_pk_s` to peak wygładzony (atak 5 ms, release 60 ms, nie krótszy niż 1,5 okresu najniższej częstotliwości pasma); `E_FLOOR` = −80 dBFS.
- **Nieliniowość (waveshaping Czebyszewa):** `y_h = e_pk_s · Σ_{k=2..K} w_k·T_k(x_n)`, `T_0 = 1`, `T_1 = x`, `T_{k+1} = 2x·T_k − T_{k−1}`; dla sinusa o amplitudzie 1 `T_k(cos θ) = cos kθ`, więc amplitudy harmonicznych są dokładnie `w_k`. `K` = `harm_max` (3, 4, 5). Wagi: `w_k = (harm_amount/100) · c_k / k`, `c_k = (1 − b)` dla parzystych, `(1 + b)` dla nieparzystych, `b = harm_balance/100`, `c_k ≥ 0`.
- **Czyszczenie:** HP 15 Hz (usuwa DC od T_2), LP przy `sub_lp_freq` w torze sub. Jako że tor jest pasmowo ograniczony (≤ 500 Hz × rząd 5), aliasing nie występuje; oversampling wyłącznie dla zgodności latencji.
- **Wyjście:** `sub1_level`·`y_h`; `sub2_level` nie dotyczy. Harmoniczne dodawane są do dry; podstawowa źródła nie jest tłumiona.
- **Ograniczenie:** dokładność wag harmonicznych dotyczy sygnałów zbliżonych do sinusa; dla sygnałów złożonych pojawia się intermodulacja (opis w UI, bramka pewności).

### 6.6 HYBRID
`y = hyb_divide·(DIVIDE: Sub −1 / Sub −2 wg poziomów) + hyb_harmonic·(HARMONIC)`; ułamki 0…1 z `hyb_divide`, `hyb_harmonic`; wspólne pasmo źródłowe, aktywność i bramka pewności.

## 7. Tor sub

Kolejność **stała**: Level → Boost → Gate → Drive* → Compressor → Drive* → EQ → Filters → Phase tool → Limiter → (do routera). Każdy stopień jest czystą tożsamością w stanie neutralnym (0 dB, Off).

### 7.1 Level
`sub_level` (−60 = Off → wzmocnienie dokładnie 0). Gain multiplikatywnie wygładzany. Jest to poziom **wejściowy** toru sub: razem z poziomem źródła wyznacza próg nasycenia (Drive) i kompresji; poziom końcowy suba ustawiają wzmocnienia klas routingu (`route_front`, `route_center`, …) oraz w trybie LFE `lfe_trim` (decyzja [W] nr 4 w §10).

### 7.2 Boost (Frequency Boost) i parametryczny EQ
Filtr Cytomic SVF: Peak lub Low Shelf, `boost_gain` 0…+18 dB, `boost_q`. Środek = `boost_freq` lub, przy `boost_follow`, bieżąca częstotliwość Sub −1 (z silnika; dla HARMONIC → `boost_freq`). Zmiana środka wygładzana w skali log; 0 dB → tożsamość (m1 = 0).
Definicja filtrów Cytomic ( A = 10^(gain/40), g = tan(π f/fs) ): Bell — `k = 1/(Q·A)`, m0 = 1, m1 = k(A²−1), m2 = 0 · Low shelf — `g = tan(π f/fs)/√A`, k = 1/Q, m0 = 1, m1 = k(A−1), m2 = A²−1 · High shelf — `g = tan(π f/fs)·√A`, k = 1/Q, m0 = A², m1 = k(1−A)·A, m2 = 1−A². Wyjście `y = m0·v0 + m1·v1 + m2·v2`. Test: dopasowanie odpowiedzi amplitudowej do referencji RBJ w tolerancji 0,05 dB (T-FILT-02).

**Parametryczny EQ (5 pasm, `eqN_*`, N = 1…5):** pasma pracują szeregowo w kolejności 1→5, po kompresorze i Drive (Post-Comp), przed filtrami. Kształt wg `eqN_type` (Bell, Low Shelf, High Shelf) z definicji powyżej; `eqN_freq` 15…1000 Hz, `eqN_gain` ±18 dB, `eqN_q` (dla półek: nachylenie). `eqN_on` = Off albo `eqN_gain` = 0 dB → pasmo jest tożsamością (m1 = m2 = 0, bit-exact). Zmiana typu pasma: crossfade 20 ms. Łączna krzywa (suma dB pasm) jest rysowana w analizatorze. Test: T-EQ-01.

### 7.3 Drive/Saturate (pokrętło główne)
- **Wejście:** `x_ref = x · 10^(12/20)` (poziom odniesienia −12 dBFS = ±1,0) [P2: `DRIVE_REF_DB`].
- **Pre-gain:** `g = 10^((30·drive/100)/20)`. `drive` = 0 → tożsamość (omijanie). Wtapianie nieliniowości: `α = min(1, drive/5)` (drive w procentach); `y = (1 − α)·x_ref + α·f_c(g·x_ref)`, potem skalowanie z powrotem (`/10^(12/20)`).
- **Charaktery** (`drive_mode`) — funkcja f i antypochodna F₁ (do ADAA):
| Tryb | f(x) | F₁(x) |
|---|---|---|
| Soft | tanh x | ln cosh x (stabilnie: `|x| + log1p(e^(−2|x|)) − ln 2`) |
| Tube | tanh(x + b) − tanh b, b = 0,35 | ln cosh(x + b) − x·tanh b |
| Tape | (2/π)·atan(πx/2) | (2/π)·x·atan(πx/2) − (2/π²)·ln(1 + (πx/2)²) |
| Fold | sin(πx/2) | −(2/π)·cos(πx/2) |
| Hard | clamp(x, −1, 1) | x²/2 dla |x| ≤ 1; |x| − ½ dla |x| > 1 |
- **Bias (`drive_balance`):** efektywne `b_eff = b_mode·(1 − max(0, balance)/100) + 0,5·max(0, −balance)/100`; tryb z biasem ma postać `f_b(x) = f(x + b_eff) − f(b_eff)` oraz `F₁_b(x) = F₁(x + b_eff) − x·f(b_eff)`. Wartości dodatnie `drive_balance` dążą do symetrii (nieparzyste), ujemne dodają asymetrię (parzyste).
- **ADAA 1. rzędu:** `y[n] = (F₁(u[n]) − F₁(u[n−1])) / (u[n] − u[n−1])` dla |Δu| > 10⁻⁵; w przeciwnym razie `f((u[n] + u[n−1])/2)`. Opóźnienie ADAA 0,5 próbki jest wliczone w kompensację dry (dla Mix/Print) lub pomijalne (Live).
- **Auto Gain (`drive_auto`):** wzmocnienie kompensujące `G_c(mode, drive, balance) = RMS(r)/RMS(f_c(g·r))`, gdzie r = sinus 0,25 amplitudy (−12 dBFS peak) o 1 okresie próbkowanym 2048 punktami; liczone przy zmianie parametru w wątku UI i przekazywane atomowo. Cel: odchyłka RMS ≤ ±0,5 dB względem drive = 0 dla sinusa referencyjnego (T-DRV-03).
- **Ton:** po nieliniowości LP 2. rzędu przy `drive_tone`; cross-fade z wyjściem bez filtra przy `drive` → 0.
- **Pozycja:** `drive_pos` Pre-Comp/Post-Comp (przełączanie z crossfade 20 ms).
- **LFE:** Drive stoi przed filtrami (w tym LPF LFE), więc harmoniczne ponad pasmem LFE są odcinane.
- **Psychoakustyka:** nasycenie dodaje harmoniczne (2f, 3f…), które pozwalają usłyszeć sub na małych głośnikach (wysokość wirtualna) — wskaźnik „harmoniczne w paśmie 80–300 Hz” w UI jest SHOULD.

### 7.4 Kompresor
- **Stopień:** `comp_on` Off → tożsamość (crossfade 10 ms). **Detektor:** `comp_detect` — Peak lub RMS. RMS: średnia kwadratowa z oknem `max(10 ms, 1,5·T_sub)`, gdzie `T_sub = 1/f_sub1`, a `f_sub1` to częstotliwość Sub −1 (z silnika lub `base_freq`). Wynik w dB.
- **Reguła tętnień:** efektywny atak = `max(comp_attack, 0,25·T_sub)`, efektywny release = `max(comp_release, 1,0·T_sub)` (zapobiega modulacji amplitudowej, gdy stałe czasowe są porównywalne z okresem sygnału) [P2].
- **Krzywa statyczna** (L — poziom wejścia w dB, T = `comp_thresh`, R = `comp_ratio`, W = `comp_knee`): dla `2(L−T) < −W`: wyjście L; dla `2|L−T| ≤ W`: `L + (1/R − 1)(L − T + W/2)²/(2W)`; dla `2(L−T) > W`: `T + (L−T)/R`. Redukcja GR = wyjście − L.
- **Wygładzanie:** GR w dB, jednobiegunowe z różnymi stałymi dla ataku i release; `comp_auto_rel` mieszany release 50/500 ms zależnie od głębokości i czasu trwania GR [P2].
- **Makeup:** `comp_makeup` oraz, przy `comp_auto_mu`, `0,5·|T|·(1 − 1/R)` dB [P2].
- **Lookahead:** 0 (Live); do 5 ms (Mix/Print), wliczone w L_look.

### 7.5 Bramka/ekspander
`gate_on` Off → tożsamość (crossfade 10 ms). Detektor obwiedni z toru po Boost; próg `gate_thresh` z histerezą 3 dB; hold `gate_hold`; release `gate_release`; głębokość `gate_range` (ekspander w dół). Atak 2 ms (stałe); w Mix/Print lookahead L_look zapobiega ucinaniu początku nuty. Wygładzanie wzmocnienia ≥ 0,25·T_sub.

### 7.6 Filtry HP/LP (subsonic i dolna granica)
- **Parametry:** HP — `sub_hp_freq`, `sub_hp_slope`; LP — `sub_lp_freq`, `sub_lp_slope`; typ — `filt_type`; fazowość — `filt_phase`.
- **Typy:** Butterworth (rząd = nachylenie/6 dB), Linkwitz-Riley (kaskada dwóch Butterworthów o połowie rzędu; LR2 = 2×BW1), Bessel (rzędy 2, 4, 8; nachylenia 12/24/48). Nachylenia 12/24/48/96 dB/okt; Bessel nie oferuje 96.
- **Struktura:** kaskada sekcji TPT SVF (Q według rzędu Butterworth: n = 2: 0,7071; n = 4: 0,5412, 1,3066; n = 8: 0,5098, 0,6013, 0,8999, 2,5629), `double`. Współczynniki Bessela z tabel [P2], zweryfikowane względem `scipy.signal.bessel(norm='mag')`.
- **Linear Phase (tylko Print):** jądro FIR z zerofazowej odpowiedzi |H|² prototypu minimum-phase, okno Kaiser (β ≈ 8), długość N = najbliższa potęga 2 ≥ 6·fs/f_corner (limit 32768 próbek), konwolucja FFT z partycjonowaniem; latencja (N−1)/2 w L_lin; jądro liczone w wątku tła, podmieniane z crossfade 40 ms [S]. Jeśli spike wykaże problem — degradacja do v1.1 (SPEC_BUILD_RELEASE §12).
- **Tryb LFE:** zamiast `sub_hp_*` i `sub_lp_*` obowiązują `lfe_hp_freq`, `lfe_lp_freq`, `lfe_lp_slope` (pojedynczy LPF, bez kaskady — wg ITU-R BS.775-4 Annex 7).

### 7.7 Narzędzia fazy
- **Polaryzacja** `sub_pol`: mnożnik ±1 (crossfade 5 ms).
- **Rotacja fazy** `sub_rot` θ: para all-pass (sieć Hilberta) `a(t), b(t)` o różnicy fazy 90° w paśmie 12–600 Hz (błąd ≤ 1°, rząd dobrany w Fazie 2 [P2]); `y = cos θ·a − sin θ·b`. θ = 0 → wyjście = `a` (all-pass o płaskiej amplitudzie; to jedyny stopień wprowadzający przesunięcie fazy względem dry przy θ = 0 — dopuszczalne, bo `a` jest częścią toru sub, nie dry).
- **Auto-Align:** §8.B.

### 7.8 Limiter
- **Stopień:** `lim_on` Off → omijanie (crossfade 10 ms).
- **Live:** miękki ogranicznik bez lookahead: `y = c·tanh(x/c)` (ADAA 1. rzędu, F₁ = c²·ln cosh(x/c)), gdzie c to sufit w skali liniowej; gwarantuje |y| < c.
- **Mix/Print:** limiter peak z lookahead 2 ms (Mix) / 5 ms (Print): `g_target = min(1, c/peak_w)` z oknem min, wygładzanie oknem Hanna, release `lim_release`; `lim_tp` (Print) liczy peak na sygnale 4× oversamplowanym. Sufit `lim_ceiling` (w trybie LFE `lfe_ceiling`).
- Limiter działa wyłącznie na torze sub; dry nie jest limitowane.

## 8. Korelacja fazowa — trzy warstwy

### 8.A Wejście (detekcja)
Korelacja ρ między `S_L` i `S_R` w paśmie źródłowym: `ρ = ⟨S_L·S_R⟩ / √(⟨S_L²⟩⟨S_R²⟩)`, średnie jednobiegunowe 300 ms, mianownik z podłogą 10⁻¹². Tryby `src_mode` wg §5.2. Wskaźnik ρ w UI; ostrzeżenie W-03, gdy ρ < −0,5 przy Sum.

### 8.B Relacja sub ↔ dry
- **Polaryzacja/rotacja:** §7.7. **Reguła polaryzacji początkowej:** §6.2 i §6.3.
- **Align:** §9.2.
- **Auto-Align (wątek tła):** bufory pierścieniowe (do 8 s) pasma dry (sygnał S1 przepuszczony przez [`range_lo`/2, `range_hi`]) i wyjścia toru sub przed polaryzacją/rotacją. Dla p ∈ {+1, −1} i θ ∈ {−180°…+180°, krok 15°} liczone jest `z = d + p·Re{a·e^{jθ}}` (a — sygnał analityczny suba); kryterium: `align_obj` = Energy → maksymalna energia z w paśmie nakładania [max(range_lo/2, 20 Hz), range_hi]; Headroom → minimalny szczyt |z|. Wynik (p*, θ*) i pewność (przewaga nad medianą ≥ 0,5 dB, w przeciwnym razie „brak wyraźnego optimum”). `align_lock` blokuje zmiany; przycisk Auto jest akcją jednorazową (nie parametrem).
- **Uwaga fizyczna:** Sub (f/2) i dry (f) mają różne częstotliwości; optimum istnieje tylko, gdy dry zawiera energię w paśmie nakładania (np. korpus stopy, 808 z mocną 2. harmoniczną) — stąd kryterium pewności.

### 8.C Wyjście
Korelometr: współczynnik korelacji L/R wyjścia w paśmie 20–150 Hz (jednobiegunowe 300 ms) oraz wskaźnik zgodności mono: `10·log10(E_{(L+R)/2} / ((E_L + E_R)/2))` w tym paśmie. `sub_width` (0 = mono) tylko dla trybów Stereo/Surround z dwoma strumieniami.

## 9. Sumowanie, Align, Wet Only, Bypass

### 9.1 Wzory
Niech `d` — dry opóźnione o L_total (na każdym kanale), `u` — sygnał sub dostarczony kanałowi przez router (S7).
- **Add:** `out = dry_gain·d + u`, `dry_gain = 10^(dry_level/20)` (−60 → 0).
- **Mix:** `out = a(m)·d + b(m)·u`; Equal Power: `a = cos(π·m/2)`, `b = sin(π·m/2)`; Linear: `a = 1 − m`, `b = m`; `m = mix/100`; dla m ≥ 1 − 10⁻¹² wymusić a = 0, b = 1; dla m = 0 a = 1, b = 0 dokładnie.
- **Wet Only:** wyjście = `u` (współczynnik dry = 0, z wygładzeniem 10 ms); latencja bez zmian; kanały bez przypisanego suba milczą.
- **Bit-exact:** gdy `u` ≡ 0 i współczynniki dry = 1,0, wynik = `d` bez błędu zaokrąglenia (mnożenie przez 1,0 i dodanie 0,0).
- `out_trim` stosowane na końcu.

### 9.2 Align
`align_ms` dodaje opóźnienie dry (linia o rozdzielczości 1 próbki) ponad L_sub, kompensując opóźnienie grupowe toru sub (filtry zakresu, LPF/HPF) tak, aby sub i dry były zgodne w czasie w okolicy częstotliwości suba. UI pokazuje szacowane opóźnienie grupowe suba przy bieżącej częstotliwości Sub −1 (liczone analitycznie z filtrów) oraz przycisk „Match” ustawiający `align_ms` (SHOULD). Zmiana `align_ms` zmienia L_total (parametr N).

### 9.3 Zachowania brzegowe
- Zmiana `sum_mode`: crossfade 20 ms.
- Nieskończoność/NaN w `u`: zerowane przed sumą (§4.7).
- Clipping wyjścia: nie ma automatycznego limitera na sumie (antycel 1); wskaźnik szczytu ostrzega (W-06).

## 10. Decyzje wykonawcy [W] do przeglądu
1. Latencja zależna tylko od `quality`, `filt_phase`, `align_ms` (nie od silnika) — przewidywalność kosztem zbędnego lookahead w Divide/Mix.
2. Domyślny `quality` = Mix (≈ 25 ms), `engine` = Divide.
3. Definicja „Base Frequency” jako częstotliwości Sub −1.
4. Poziom (`sub_level`) na początku toru; Drive operuje na poziomach bezwzględnych (odniesienie −12 dBFS), co daje przewidywalny próg nasycenia.
5. Pięć charakterów Drive: Soft, Tube, Tape, Fold, Hard. Nazwy per źródło (Bass/Kick/Pre, jak u SSL) to presety, nie tryby.
6. Linear Phase tylko dla HP/LP w Print; PEQ minimum-phase.
7. Standalone nie wchodzi do instalatorów.
8. C++20, JUCE 9.0.4 przypięte do taga.
