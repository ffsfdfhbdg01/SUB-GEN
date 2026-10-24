# SUB-GEN — SPEC (Faza 1): produkt i rdzeń DSP

Wersja dokumentu: 0.4 · Data: 2026-10-24 · Wersja produktu docelowa: 1.0.0 · Historia: 0.1 (2026-10-19) zatwierdzona na bramce Fazy 1; 0.2 — korekty z Fazy 2 zatwierdzone na bramce Fazy 2 ([PHASE2_REPORT.md](PHASE2_REPORT.md) §6: K-01…K-20, decyzje D1–D9; ADR-012…024); 0.3 — poprawki po niezależnym przeglądzie D8, rekomendacje R1–R5 przyjęte 2026-10-21 (PHASE2_REPORT §11; ADR-025…029); 0.4 — poprawka F-1 z Fazy 3 przyjęta 2026-10-24: bramka DIVIDE otwiera się po dwóch kolejnych pewnościach ≥ progu (PHASE2_REPORT §12; ADR-030)
Części specyfikacji: **SPEC.md** (ten plik: produkt, architektura, silniki, tor sub, faza, sumowanie) · [SPEC_IO_UI.md](SPEC_IO_UI.md) (kanały, routing, LFE, metering, UI, stan i presety) · [SPEC_BUILD_RELEASE.md](SPEC_BUILD_RELEASE.md) (build, CI, instalatory, release, budżety, plan Fazy 2, ryzyka) · [PARAMETERS.md](PARAMETERS.md) (rejestr parametrów) · [TEST_PLAN.md](TEST_PLAN.md) (testy i kryteria).
Kontekst: [PROMPT.md](PROMPT.md) (brief v2), [RESEARCH.md](RESEARCH.md), [DECISIONS.md](DECISIONS.md).

## 0. Konwencje czytania
- **MUST / SHOULD / COULD** — priorytet. Zakres v1.0 = wszystko, co nie jest oznaczone SHOULD/COULD.
- **[U]** decyzja użytkownika (DECISIONS B2) · **[W]** decyzja wykonawcy (do Twojego przeglądu) · **[P2]** (wersja 0.1) wartość do rozstrzygnięcia w Fazie 2 — w wersji 0.2 wszystkie są rozstrzygnięte i wpisane w tekst z odwołaniem do pomiaru (P2-NN) · **[F3]** wartość do dostrojenia w Fazie 3 (Faza 2 jej nie badała) · **[S]** wymaga spike'a w Fazie 3.
- Wartości zmierzone w Fazie 2 pochodzą z prototypu `proto/` ([PHASE2_REPORT.md](PHASE2_REPORT.md)); testy C++ (TEST_PLAN) sprawdzają je na implementacji. Pozostałe liczby są celami początkowymi.
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
Syntezator MIDI/instrument · AAX w release (cel CI) · CLAP · ambisonics · SPECTRAL/HQ · linear-phase PEQ i linear-phase HP (jest linear-phase dla filtra LP w Print, §7.6) · wielopasmowe źródło · polifoniczny pitch tracking · strojenie do tonacji (note-lock) · iOS/AUv3 · Linux (poza opcjonalnym jobem CI) · telemetria, sieć, aktywacja/licencjonowanie.

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
| quality | Oversampling (Drive, Harmonic) | L_look | L_os | L_lin | L_total przy 48 kHz (`align_ms` = 0) |
|---|---|---|---|---|---|
| Live | 1× (tylko ADAA) | 0 | 0 | 0 | **0 próbek** |
| Mix | 2× polyphase IIR | 47 ms (TRACK 45 + kompresor 0 + limiter 2) | ceil(`getLatencyInSamples()`) | 0 | ≈ 48,5 ms |
| Print | 4× FIR equiripple | 65 ms (TRACK 55 + kompresor 5 + limiter 5) | ceil(`getLatencyInSamples()`) | (N_LIN − 1)/2 + 256 przy Linear Phase (§7.6) | ≈ 66,5 ms; z Linear Phase ≈ 114,5 ms |
- Raportowane przez `setLatencySamples(L_total)`; stała dla danego `quality`, `filt_phase`, `align_ms` — nie zależy od silnika ani parametrów audio. Zmiana `quality`, `filt_phase`, `align_ms` wywołuje ponowne zgłoszenie latencji (parametry N).
- Dry jest opóźnione o **L_total**, sub o L_sub (naturalne opóźnienia toru); `align_ms` jest dodatkowym opóźnieniem dry kompensującym opóźnienie grupowe toru sub (§9.2).
- Oversampler Drive/Harmonic pracuje zawsze w trybach Mix/Print (przy `drive` = 0 sygnał przechodzi przez opóźnienie o tę samą latencję), aby latencja była stała.
- **L_look** (P2-03, P2-10, ADR-015): stopnie z wyprzedzeniem sumują się — silnik potrzebuje L ms przyszłości źródła, a kompresor i limiter swoich ms przyszłości suba. TRACK zna wysokość po 2 okresach nuty, więc 45/55 ms wyrównuje początek suba z dry dla nut ≥ 55 Hz (Mix) i ≥ 41,2 Hz (Print); niższe nuty TRACK startują z opóźnieniem. (Wartości 25/50 ms z wersji 0.1 wyrównywały tylko nuty ≥ 100/45 Hz.)
- **L_os:** rzeczywistą latencję oversamplerów JUCE odczytuje Faza 3 (`getLatencyInSamples()`). Prototyp (FIR Kaisera) daje 72 próbki — to górne ograniczenie użyte w kolumnie L_total (2328 / 3192 / 5496 próbek przy 48 kHz).

### 4.5 Oversampling i aliasing
`juce::dsp::Oversampling<double>`: Mix — `filterHalfBandPolyphaseIIR`, Print — `filterHalfBandFIREquiripple` (wariant max quality). Oversampling obejmuje wyłącznie stopnie nieliniowe (Drive; wewnętrzne nieliniowości HARMONIC). ADAA 1. rzędu w Drive jest zawsze aktywne (także w Live).
- **Miara aliasingu (P2-06, ADR-017):** moc składowych nieharmonicznych względem mocy całości dla tonów 31/81/121 Hz (−6 dBFS). Tony 30/80/120 Hz przy 48 kHz są „ślepe”: fs/f0 jest całkowite, więc każdy alias trafia w harmoniczną.
- **Kryterium (D2):** w paśmie toru sub, tj. poniżej 500 Hz (górna granica `sub_lp_freq`): ≤ −80 dB (Live), ≤ −90 dB (Mix, Print), dla wejść do +12 dBFS (`sub_level` stoi przed Drive). P2 po R3 (wszystkie tryby, tony, 44,1 i 48 kHz, wejścia −6…+12 dBFS, drive 100 %): Live −90,2, Mix −93,2, Print −93,2 dB; przy wejściu −6 dBFS i 48 kHz: −129 / −143,5 / −163,6 dB.
- **Pełnopasmowo (informacyjnie):** Hard przy drive 25/50/100 % — Live −75,5/−68,7/−55,3, Mix −97,9/−90,7/−77,1, Print −117,9/−110,8/−95,3 dB; Fold nie aliasuje (≈ −230 dB). Ten aliasing odcina LP toru sub; ujawnia się tylko przy wysokim `sub_lp_freq`.
- Prototyp używał FIR Kaisera (pasmo do 0,45·fs, ≥ 100 dB od 0,55·fs) jako wzorca; filtry JUCE weryfikuje T-ALIAS-01 w Fazie 3.

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
- **Granice nut (R2, ADR-026):** początek nuty = narastające zbocze A albo atak: szybka obwiednia `x_b` (atak 0,2 ms, release 15 ms) wzrasta ≥ 12 dB ponad wolną (RMS 50 ms × √2) sprzed 5 ms, przy szybkiej obwiedni ≥ √2·`det_thresh` (szczyt sinusa o RMS równym progowi); blokada 50 ms łączy oba rodzaje. Każdy początek ma czas wykrycia (przyczynowy) i czas początku narastania: ostatnią chwilę przed wykryciem, gdy szybka obwiednia była w granicach 1 dB od odniesienia (dla Mix/Print, w zasięgu lookahead). Koniec nuty = opadające zbocze A. Uderzenie słabsze niż 12 dB ponad nakładającym się ogonem należy do tej samej nuty.

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
- **Obwiednia (`follow`):** Follow — amplituda wynika z `e_rms` źródła z czasami `env_attack`/`env_release`; Constant — stała amplituda (−12 dBFS peak) bramkowana aktywnością. `dynamics` = makro: wartości ujemne skracają `env_release` i dodają krótki akcent transjentu (ułamek `e_pk` − `e_rms` przez 5 ms), dodatnie wydłużają release i wygładzają atak [F3].
- **Przełączanie silników:** równomocny crossfade 30 ms; silnik wchodzący jest wcześniej „rozgrzewany” 200 ms buforowanego `x_b`. Latencja się nie zmienia.
- **Bramka pewności** (DIVIDE, TRACK): pewność c ∈ [0,1]; bramka otwiera się przy c ≥ `0,2 + 0,6·conf_gate/100` i zamyka, gdy c spadnie poniżej max(próg − 0,2; próg/2) (histereza, P2-02; przy `conf_gate` 0 % poniżej 0,1 — D8 I1); wzmocnienie bramki narasta w 5 ms i wygasa w 50 ms. W Mix/Print bramka czyta pewność z wyprzedzeniem E (45/55 ms), więc jest otwarta, gdy nuta się zaczyna (R1). Pewność tymczasowa (DIVIDE, §6.2) trzyma bramkę otwartą; gdy się kończy, pierwsza prawdziwa wartość musi osiągnąć próg otwarcia, inaczej bramka zamyka się w 10 ms. **DIVIDE (F-1, ADR-030):** poza pewnością tymczasową bramka otwiera się dopiero przy drugiej z kolei wartości c ≥ progu otwarcia. Wartości liczy się na osi czasu dzielnika: każda zmiana c lub jej tymczasowości to nowa wartość (powtórzona liczy się raz), wartość tymczasowa się liczy, wartość poniżej progu zeruje licznik; bramka czyta licznik z tym samym wyprzedzeniem co c. TRACK otwiera się na pierwszej wartości. Wskaźnik w UI.

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
**Polaryzacja początkowa (reguła; R2, ADR-026):** dzielnik działa tylko wewnątrz nuty (granice z §5.4) i poza nią jest zamrożony, więc szum, buczenie ani ogon między uderzeniami nie zmieniają s1/s2. Reset `s1 = s2 = +1`, `arm2_pending` wyzerowane, uzbrojenie skasowane, historia okresów wyczyszczona:
- po końcu nuty — przy najbliższym przejściu przez zero (ciągłość);
- na początku nuty w Mix/Print — przy ostatnim przejściu przez zero przed początkiem narastania (w zasięgu lookahead E; ciągłość);
- na początku nuty w Live — w chwili wykrycia, z rampą 0,5 ms (znaczy to tylko wewnątrz trwającej nuty, czyli przy nakładających się uderzeniach: ich pierwsze ~7 ms może mieć starą polaryzację).

Pierwszy płat suba pokrywa się więc z pierwszym płatem źródła, uderzenie w uderzenie. P2-02: 20/20 i korelacja przebiegów 0,9995 przy ciszy, szumie −90…−60 dBFS i buczeniu 31 Hz −70 dBFS (Mix i Live); przy nakładających się ogonach (T60 1 s) natywny sub 20/20, a wyjście spójne między uderzeniami.
**Wygładzanie / `shape` (R5, ADR-029):** `y_out` = y przez filtr dolnoprzepustowy 4. rzędu Butterworth TPT o częstotliwości odcięcia `0,87·f_est·2^(5·shape)` dla Sub −1 i `0,87·(f_est/2)·2^(5·shape)` dla Sub −2 (shape 0…1; ograniczone do 0,45·fs), aktualizowanej co cykl z poślizgiem 5 ms. shape 0 = sinus, 100 % = odcięcie ×32, czyli praktycznie kształt natywny. Poziom składowej f/2 jest stały w funkcji `shape` (P2-02: ±0,05 dB; mieszanie torów z wersji 0.2 dawało −3,1 dB przy 50 %), faza względem dry zmienia się płynnie. Przed pierwszą estymatą nuty (od każdego resetu) `f_est` = `range_hi` (D8 M13). Sinus (shape 0) ma resztkową składową 3f/2 ≤ −18 dB względem Sub −1 (P2-02: −23,4 dB; przebieg natywny −4,4 dB).
**Amplituda:** Follow — natywna (amplituda x); Constant — `y / p · 10^(−12/20)`, gdzie `p` = przyczynowe maksimum kroczące |x_b| w oknie 1,25 okresu źródła (1,25·fs/f_est, ograniczone do [1,25·fs/`range_hi`, 1,25·fs/`range_lo`]; przed pierwszą estymatą 1,25·fs/`range_lo`). Wynik nie przekracza −12 dBFS i leży na tym poziomie w stanie ustalonym (follower z atakiem 1 ms z wersji 0.1 przestrzeliwał, P2-02).
**Pewność c (P2-02, ADR-013, ADR-025):** nieregularność nowego okresu względem liniowego trendu okresów `r_k = |T_k − (2·T_{k−1} − T_{k−2})| / T_{k−1}` (wymaga trzech okresów); `c = 1 − min(1, mediana(r z ostatnich 5 okresów) / 0,15)`. Od początku nuty do pierwszej reszty c = 1 tymczasowo (R1; bramka — §6.1). Płynne glissando (spadek wysokości stopy o 12–20 % na okres) jest regularne, polifonia i szum nie. Wyniki P2 (po D8 i F-1): pierwsze 40 ms stopy — 94 % (Live) / 95,5 % (Mix, Print) energii suba bez bramki (wersja 0.2: 0,5 %); stopa w 50–300 ms — bramka otwarta w 100 % (reguła z wersji 0.1: 0 %); SNR w paśmie 20/15/10/5/3 dB — otwarta 99/57/18/5/2 % czasu (przed F-1: 99/68/27/8/4 %); szum losowy 0 % (przed F-1: ≤ 1,5 %); akord po 100 ms −76 dB (Live) / −116 dB (Mix), a po domyślnym paśmie źródłowym 30–120 Hz −51 dB (Live) / −90 dB (Mix) (przed F-1: −10 dB).
**Latencja własna:** 0 próbek; opóźnienie grupowe filtrów zakresu `x_b` jest naturalne (kompensuje je `align_ms`, §9.2).
**Wariant (P2-02, ADR-012):** dzielnik jak wyżej — zerowa latencja, sub sprzężony z kształtem fali, błąd częstotliwości ~2·10⁻⁹ %, polaryzacja zgodna w 100/100 uderzeń. „Phase halving” z sygnału analitycznego (`y = A·cos(φ/2)`) daje czystsze widmo (3f/2 −123 dB) i ~3× mniejszy błąd częstotliwości w szumie, ale wymaga FIR Hilberta (latencja) — COULD jako opcja Print w v1.x.

### 6.3 TRACK — śledzenie wysokości + oscylator
- **Detektor f0 (P2-03, ADR-014):** NSDF/MPM na sygnale `x_b` zdecymowanym całkowitym współczynnikiem D = ⌊fs/2000⌋ do `fs_d` = fs/D (2000 Hz przy 48 kHz, 2004,5 Hz przy 44,1 kHz; filtr antyaliasingowy Butterworth 8. rzędu, 800 Hz); okno `W = ceil(2,5·fs_d / max(range_lo, 20 Hz))` (minimum 2 okresy, bo lag musi zmieścić się w W/2), hop 2 ms; szczyt kluczowy z progiem 0,9 i interpolacją paraboliczną; wynik `f0 = fs_d/τ`. **Ramki świadome onsetu:** początkowe próbki ramki poniżej 1 % jej szczytu są pomijane, a lag jest szukany do połowy pozostałej długości — wysokość jest znana po 2 okresach nuty (+ ≤ 3,6 ms). **Pewność:** `c = clip((jasność NSDF − 0,6)/0,4, 0, 1)` (szum pasmowy ma jasność 0,45–0,56, sinus 0,999; domyślne `conf_gate` 50 odpowiada jasności 0,8). Wynik P2: najgorsza mediana błędu 1,7 centa, 0 błędów oktawowych (SIG-01/04/05) przy porównaniu z dopasowanym opóźnieniem. Bez dopasowania, czyli tak, jak estymaty trafiają do oscylatora, na glissandach 808 ok. 0,3 % (Mix) / 0,6 % (Live) ramek odbiega o ponad 600 centów: to opóźnienie śledzenia, nie wybór złej oktawy (D8 M10). YIN odrzucony: 36 ramek z błędem oktawowym na SIG-05, bramka otwarta na złych oktawach przy niskim SNR.
- **Prior (błędy oktawowe):** estymata w oknie ±25 % od bieżącej jest przyjmowana od razu; wartość spoza okna — dopiero po 3 kolejnych ramkach zgodnych ze sobą (±25 %). Ta sama reguła dotyczy pierwszej wartości nuty, więc pojedyncza ramka odstająca nie rozpocznie śledzenia. Ramki z c < 0,5 są pomijane (trzymana jest ostatnia wartość); 5 takich ramek z rzędu zeruje stan. Ocena kandydatów f0/2 i 2·f0 z wersji 0.1 odrzucona: okno ±25 % wciągało kwinty i seksty.
- **Szybkość:** `trk_speed` dobiera wygładzanie f0 (stała 5…80 ms) i próg zmiany; `trk_glide` dodaje portamento (liniowe w log f).
- **Oscylator:** akumulator fazy, częstotliwości Sub −1 = f0/2, Sub −2 = f0/4 (lub `base_freq` i `base_freq`/2 w Fixed). Kształt: `(1 − shape)·sin φ + shape·tri(φ)` (trójkąt naiwny wystarczy: aliasing < −90 dB dla f ≤ 100 Hz, fs ≥ 44,1 kHz).
- **Faza (`phase_gen`):** Locked — `φ_sub = φ_PLL/2 + c0` (c0 ∈ {0, π} wybierane regułą polaryzacji jak w DIVIDE), gdzie φ_PLL to faza PLL 2. rzędu (ζ = 0,707) śledzącej źródło i startującej z f0 z MPM; pasmo pętli (−3 dB zamkniętej pętli) 5 Hz w stanie ustalonym (P2: 1,35° na czystym sygnale, 5,9° przy SNR 10 dB; faza z pary Hilberta: 13,3°), ≥ 20 Hz podczas glissand (reguła przełączania [F3]); Free — czysta akumulacja z f0; Reset — reset fazy do 0 przy zdarzeniu onset (detektor z §6.4) i dalej Free.
- **Amplituda:** Follow — `e_rms` źródła z `env_attack`/`env_release`; Constant — stała bramkowana aktywnością.
- **Latencja i lookahead:** Live — brak lookahead; wysokość znana po 2 okresach nuty, potem opóźnienie śledzenia ≈ W/2 + 1 ms (43 ms przy W = 83,5 ms, P2-03). Mix/Print — wysokość i pewność są czytane z wyprzedzeniem 45 ms (Mix) / 55 ms (Print), a amplituda na bieżąco (wyprzedzona amplituda zaczynałaby sub przed dry); początek suba pokrywa się z początkiem źródła dla nut ≥ 55 Hz (Mix) i ≥ 41,2 Hz (Print). TRACK nie nadąża za szybkimi spadkami wysokości (stopa, 808: estymata stoi na średniej z okna przez 100–150 ms) — do nich służą DIVIDE i TRIGGER (UC1, UC4); TRACK służy do linii basowych (UC2).

### 6.4 TRIGGER — sub syntetyczny wyzwalany transjentem
- **Detekcja onsetu:** obwiednia szybka `e_t` (atak 0,2 ms, release 15 ms) z `x_b` lub z klucza (`sc_use` = Trigger); zdarzenie, gdy `e_t` przekracza `trig_thresh` z histerezą 3 dB, po upływie `trig_lockout`; ponowne uzbrojenie po spadku `e_t` poniżej progu − 3 dB przez `trig_hold`. Czas zdarzenia z interpolacją liniową przecięcia (ułamek próbki); P2-04: stały bias −0,486 próbki (rozrzut 0,0067), latencja Live 0,18 ms, opcjonalna korekta +0,5 próbki [F3]. Ponowne uzbrojenie trwa ≈ 15 ms·ln(szczyt/próg) + `trig_hold` (stopa −6 dBFS: ~67 ms), więc przy głośnych uderzeniach to ono, a nie `trig_lockout`, ogranicza tempo wyzwoleń — podpowiedź w UI.
- **Głos:** `φ(t) = φ0 + 2π ∫ f(t) dt`, `f(t) = f_end + (f_start − f_end)·exp(−t/τ_p)` z `f_start = trig_f_start`, `τ_p = trig_pitch_time`, `φ0 = trig_phase`. Sub −2 = ta sama obwiednia i f/2.
- **Obwiednia:** `a(t) = t/t_a` dla t < t_a (`t_a = max(trig_attack, 0,5 ms)` — atak 0 przy fazie innej niż 0°/180° dawałby skok o pełną amplitudę, D8 M5), potem `10^(−3·(t − t_a)/T60)` z `T60 = trig_decay`. Prędkość: `V = clamp((peak/0,5)^(trig_vel/100), 0, 1,5)` (peak — szczyt `x_b` w 20 ms po onsecie; `trig_vel` = 0 → V = 1).
- **Follow/Constant (P2-04):** Follow dodatkowo mnoży obwiednię przez znormalizowany poziom źródła: `e_rms` (20 ms) podzielone przez swoje bieżące maksimum od onsetu (przyczynowo, D8 M4), z tolerancją 1 dB na tętnienia RMS, potem tylko malejące. Follow kształtuje więc ogon (sub kończy się wraz ze źródłem), a nie zmienia ataku ani poziomu; Constant — czyste A(t).
- **Retrigger (`trig_retrigger`):** Restart — nowy głos od fazy φ0 z 3 ms crossfade ze starego (P2: klik ≤ 4,3·10⁻⁴); Legato — faza ciągła, pitch od `trig_f_start`, atak obwiedni startuje od bieżącej amplitudy głosu (bez skoku do 0; zostaje załamanie zbocza ≤ 0,008 pełnej skali).
- **Determinizm:** przy tym samym wejściu każdy głos jest identyczny (faza startowa stała) — wymóg testu T-TRIG-02.
- **Lookahead:** Mix/Print opóźniają dry o L_look, więc głos startuje dokładnie na transjencie dry; Live startuje w próbce wykrycia.

### 6.5 HARMONIC — wysokość wirtualna
- **Wejście:** `x_b` (Auto: `range_*`; Fixed: ±½ oktawy wokół `base_freq`).
- **Normalizacja (P2-05, ADR-016):** `x_n = clamp(x_b / max(e_pk_s, E_FLOOR), −1, 1)`, gdzie `e_pk_s` = przyczynowe maksimum kroczące |x_b| w oknie 1,5 okresu `range_lo` (Fixed: dolnej krawędzi pasma), wygładzone potem obwiednią atak 5 ms / release 60 ms; `E_FLOOR` = −80 dBFS. Sam peak follower 5/60 ms z wersji 0.1 leżał 1,05 dB pod amplitudą i falował 0,2–0,7 dB, co psuło wagi o 13,6 dB; max-hold daje 0,017 dB. Koszt: obwiednia spóźnia się na spadkach poziomu o okno (50 ms przy 30 Hz) plus release.
- **Nieliniowość (waveshaping Czebyszewa):** `y_h = e_pk_s · Σ_{k=2..K} w_k·T_k(x_n)`, `T_0 = 1`, `T_1 = x`, `T_{k+1} = 2x·T_k − T_{k−1}`; dla sinusa o amplitudzie 1 `T_k(cos θ) = cos kθ`, więc amplitudy harmonicznych są dokładnie `w_k`. `K` = `harm_max` (3, 4, 5). Wagi: `w_k = (harm_amount/100) · c_k / k`, `c_k = (1 − b)` dla parzystych, `(1 + b)` dla nieparzystych, `b = harm_balance/100`, `c_k ≥ 0`.
- **Czyszczenie:** HP 15 Hz Butterworth 2. rzędu (usuwa DC od T_2; P2-05: DC −248 dBFS), LP przy `sub_lp_freq` w torze sub. Jako że tor jest pasmowo ograniczony (≤ 500 Hz × rząd 5), aliasing nie występuje; oversampling wyłącznie dla zgodności latencji.
- **Wyjście:** `sub1_level`·`y_h`; `sub2_level` nie dotyczy. Harmoniczne dodawane są do dry; podstawowa źródła nie jest tłumiona.
- **Ograniczenie:** dokładność wag harmonicznych dotyczy sygnałów zbliżonych do sinusa; dla sygnałów złożonych pojawia się intermodulacja (P2-05: dla tonów 50 + 70 Hz 72 % mocy wyjścia to intermodulacja; opis w UI, bramka pewności).

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
- **Wejście:** `x_ref = x · 10^(12/20)` (poziom odniesienia −12 dBFS = ±1,0; `DRIVE_REF_DB` = 12 dB, P2-06).
- **Pre-gain:** `g = 10^((30·drive/100)/20)`. `drive` = 0 → tożsamość (omijanie). Wtapianie nieliniowości: `α = min(1, drive/5)` (drive w procentach); `y = (1 − α)·x_ref + α·f_c(g·x_ref)`, potem skalowanie z powrotem (`/10^(12/20)`).
- **Charaktery** (`drive_mode`) — funkcja f i antypochodna F₁ (do ADAA):
| Tryb | f(x) | F₁(x) |
|---|---|---|
| Soft | tanh x | ln cosh x (stabilnie: `\|x\| + log1p(e^(−2\|x\|)) − ln 2`) |
| Tube | tanh(x + b) − tanh b, b = 0,35 | ln cosh(x + b) − x·tanh b |
| Tape | (2/π)·atan(πx/2) | (2/π)·x·atan(πx/2) − (2/π²)·ln(1 + (πx/2)²) |
| Fold | sin(πx/2) | −(2/π)·cos(πx/2) |
| Hard | clamp(x, −1, 1) | x²/2 dla \|x\| ≤ 1; \|x\| − ½ dla \|x\| > 1 |
- **Bias (`drive_balance`):** efektywne `b_eff = b_mode·(1 − max(0, balance)/100) + 0,5·max(0, −balance)/100`, gdzie `b_mode` = 0,35 dla Tube i 0 dla pozostałych (Tube = Soft z biasem 0,35); składniki się dodają (Tube przy balance −100 → b_eff = 0,85); tryb z biasem ma postać `f_b(x) = f(x + b_eff) − f(b_eff)` oraz `F₁_b(x) = F₁(x + b_eff) − x·f(b_eff)`. Wartości dodatnie `drive_balance` dążą do symetrii (nieparzyste), ujemne dodają asymetrię (parzyste).
- **ADAA 1. rzędu:** `y[n] = (F₁(u[n]) − F₁(u[n−1])) / (u[n] − u[n−1])` dla |Δu| > 10⁻⁵; w przeciwnym razie `f((u[n] + u[n−1])/2)`. Opóźnienie ADAA 0,5 próbki jest wliczone w kompensację dry (dla Mix/Print) lub pomijalne (Live).
- **Fold — ograniczenie głębokości (R3, ADR-027):** szerokość pasma Fold rośnie z nachyleniem `g·x_ref` w przejściach przez zero, więc gorące wejście (`sub_level` do +18 dB przed Drive) aliasowało w paśmie suba (−37 dB w Live przy +6 dBFS). Gdy obwiednia wejścia (hold |x_ref| w oknie max(10 ms, 1,5/f_sub1), potem release 50 ms) przekracza szczyt odniesienia −6 dBFS (x_ref = 2,0), wejście nieliniowości Fold jest skalowane w dół do tej wartości. Do −6 dBFS wynik jest bit w bit bez zmian; powyżej Fold zachowuje głębokość z −6 dBFS.
- **Auto Gain (`drive_auto`; P2-06, ADR-018, D4):** `G_c = RMS(r) / RMS_AC(T(y_α(r)))`, gdzie r = sinus referencyjny −12 dBFS peak przy bieżącej częstotliwości Sub −1 (`f_sub1`), `y_α` — wyjście nieliniowości po wtapianiu α (z biasem), RMS_AC — bez składowej stałej (usuwa ją HP 5 Hz), T — tor Tone razem z cross-fade'em (`(1 − α) + α·H_tone`). Realizacja [W]: wątek UI przy zmianie `drive`, `drive_mode`, `drive_balance` liczy widmo jednego okresu y_α (2048 punktów) → amplitudy harmonicznych |c_k| i przekazuje je atomowo; wątek audio co poddblok liczy `RMS_AC² = ½·Σ_{k≥1} |c_k|²·|T(k·f_sub1)|²` (T — dokładna odpowiedź dyskretnego filtra) i wygładza G_c (G20). Cel: odchyłka RMS ≤ ±0,5 dB względem drive = 0, także z Tone (T-DRV-03). P2 bez Tone: 0,036 dB; bez kompensacji Tone 500 Hz obniża poziom Fold/Hard nawet o 8,8 dB. Wartości G_c bez Tone (do testów): PHASE2_REPORT, Załącznik A.
- **Ton:** po nieliniowości LP 2. rzędu przy `drive_tone`; wyjście `(1 − α)·y + α·LP(y)` (to samo α co wtapianie), więc przy `drive` → 0 filtr znika płynnie.
- **Pozycja:** `drive_pos` Pre-Comp/Post-Comp (przełączanie z crossfade 20 ms).
- **LFE:** Drive stoi przed filtrami (w tym LPF LFE), więc harmoniczne ponad pasmem LFE są odcinane.
- **Psychoakustyka:** nasycenie dodaje harmoniczne (2f, 3f…), które pozwalają usłyszeć sub na małych głośnikach (wysokość wirtualna) — wskaźnik „harmoniczne w paśmie 80–300 Hz” w UI jest SHOULD.

### 7.4 Kompresor
- **Stopień:** `comp_on` Off → tożsamość (crossfade 10 ms). **Detektor bez tętnień (`comp_detect`; P2-09, ADR-022):** okno `w = max(10 ms, 1,5·T_sub)`, gdzie `T_sub = 1/f_sub1`, a `f_sub1` to częstotliwość Sub −1 (z silnika lub `base_freq`, ograniczona do ≥ 20 Hz). Peak (domyślny, R4, ADR-028) — hold |x| w oknie w; RMS — średnia kwadratowa w oknie w, a po niej **hold** (maksimum) w tym samym oknie, z minimalnym atakiem 0,25·T_sub. Dla każdego ustalonego tonu powyżej 1/w poziom detektora nie tętni. Bez hold średnia z okna jest dokładna tylko przy f_sub1 (THD −52…−58 dB przy 36–60 Hz dla f_sub1 = 40 Hz); hold kosztuje 0,22–0,30 dB dodatkowej redukcji poza f_sub1. Wynik w dB.
- **Bez reguły tętnień (D7), z jednym wyjątkiem (R4):** `comp_attack` i `comp_release` działają bez podłogi, poza minimalnym atakiem 0,25·T_sub detektora RMS. Na onsecie (ton 40 Hz, GR 0 → 18 dB, energia > 100 Hz w pierwszych 80 ms) RMS rozpryskuje najbardziej: −39 dB przy ataku 1 ms wobec −56 dB dla Peak; z minimalnym atakiem −42 dB (P2-09). P2-09: z detektorami bez tętnień THD przy GR 6 dB wynosi −124 dB dla dowolnych czasów (atak 0,1–100 ms, release 10–1000 ms, 30–100 Hz). Reguła z wersji 0.1 pomagała tylko detektorom z tętnieniami (RMS jednobiegunowy −44 → −57 dB) i na onsecie bywała szkodliwa (Peak: −57 → −46 dB).
- **Krzywa statyczna** (L — poziom wejścia w dB, T = `comp_thresh`, R = `comp_ratio`, W = `comp_knee`): dla `2(L−T) < −W`: wyjście L; dla `2|L−T| ≤ W`: `L + (1/R − 1)(L − T + W/2)²/(2W)`; dla `2(L−T) > W`: `T + (L−T)/R`. Redukcja GR = wyjście − L.
- **Wygładzanie:** GR w dB, jednobiegunowe z różnymi stałymi dla ataku i release; okno detektora dodaje się do czasu release (np. 50 ms + 15 ms). `comp_auto_rel`: GR = głębsza z dwóch gałęzi — szybkiej (atak `comp_attack`, release 50 ms) i wolnej (atak 150 ms, release 500 ms); P2: release po 0,05 / 0,5 / 3 s redukcji = 64 / 498 / 514 ms.
- **Makeup:** `comp_makeup` oraz, przy `comp_auto_mu`, `0,5·|T|·(1 − 1/R)` dB [F3].
- **Lookahead:** 0 (Live, Mix); 5 ms (Print), wliczone w L_look (§4.4).

### 7.5 Bramka/ekspander
`gate_on` Off → tożsamość (crossfade 10 ms). Detektor: peak hold w oknie `max(10 ms, 1,5·T_sub)` z toru po Boost (jak w §7.4). Bramka otwiera się przy `gate_thresh` i zamyka, gdy obwiednia jest poniżej progu − 3 dB dłużej niż `gate_hold` (okno detektora dodaje się do hold); release `gate_release`; głębokość `gate_range` (ekspander w dół). Atak 2 ms (stałe); w Mix/Print lookahead L_look zapobiega ucinaniu początku nuty. Wzmocnienie wygładzane w dB, nie szybciej niż 0,25·T_sub. P2-09: hold i release z dokładnością 0,05 %.

### 7.6 Filtry HP/LP (subsonic i dolna granica)
- **Parametry:** HP — `sub_hp_freq`, `sub_hp_slope`; LP — `sub_lp_freq`, `sub_lp_slope`; typ — `filt_type`; fazowość — `filt_phase`.
- **Typy:** Butterworth (rząd = nachylenie/6 dB), Linkwitz-Riley (kaskada dwóch Butterworthów o połowie rzędu; LR2 = 2×BW1), Bessel (rzędy 2, 4, 8; nachylenia 12/24/48). Nachylenia 12/24/48/96 dB/okt; Bessel nie oferuje 96.
- **Struktura:** kaskada sekcji TPT SVF, `double`. Q sekcji Butterwortha rzędu n: `Q_i = 1/(2·sin((2i − 1)·π/(2n)))`, i = 1…⌊n/2⌋, plus sekcja 1. rzędu dla n nieparzystego (forma z cos jest poprawna tylko dla n parzystych, P2-01): n = 2: 0,7071; n = 4: 0,5412, 1,3066; n = 8: 0,5098, 0,6013, 0,8999, 2,5629. Współczynniki Bessela z tabel, zweryfikowane względem `scipy.signal.bessel(norm='mag')` (P2-01: 2,3·10⁻⁷ dB); zmienność opóźnienia grupowego Bessela do f_c/2: rząd 2 — 2,02 %, rzędy 4 i 8 — ≤ 0,01 %.
- **Linear Phase (tylko Print, tylko LP; D5, ADR-020):** HP pozostaje minimum-phase (Linear Phase HP → v1.1). Jądro FIR ma **amplitudę |H|** prototypu minimum-phase LP — nie |H|², które podwaja nachylenie i daje −6 dB w narożniku. Okno Kaisera β = 3 (β ≈ 8 z wersji 0.1 nie trzyma 0,1 dB). Długość jest stała dla danego fs: `N_LIN = 4096·2^⌈log2(fs/48 kHz)⌉ + 1` (4097 przy 44,1/48 kHz, 8193 przy 88,2/96 kHz, 16385 przy 176,4/192 kHz). N_LIN jest nieparzyste, więc opóźnienie jądra (N_LIN − 1)/2 jest całkowite (≈ 42,7 ms przy 48 kHz, 46,4 ms przy 44,1 kHz); jednolita konwolucja FFT dodaje partycję 256 próbek [S], więc L_lin = (N_LIN − 1)/2 + 256 (≈ 48 ms przy 48 kHz) i nie zależy od ustawień filtra, jak wymaga §4.4. Dokładność ≤ 0,1 dB (od 5 Hz do punktu −60 dB) dla LP, którym N_LIN wystarcza — przy 48 kHz m.in. Butterworth 24 dB/okt ≥ 110 Hz (domyślne LP i LFE: 120–140 Hz), LR 24 ≥ 90 Hz, Bessel 24 ≥ 60 Hz, 12 dB/okt ≥ 50 Hz. Dla niższych lub bardziej stromych LP jądro jest przycinane oknem (np. BW 24 przy 60 Hz: 0,33 dB, BW 48 przy 80 Hz: 0,91 dB; P2-08), a błąd liczony przy projekcie jądra pokazuje UI. Domyślne LP (BW 24 przy 120 i 140 Hz) mieszczą się w 0,082 dB przy wszystkich fs. Konwolucja FFT z partycjonowaniem; jądro liczone w wątku tła, podmieniane z crossfade 40 ms [S]. Jeśli SPK-6 wykaże problem z CPU — degradacja do v1.1 (SPEC_BUILD_RELEASE §12).
- **Tryb LFE:** zamiast `sub_hp_*` i `sub_lp_*` obowiązują `lfe_hp_freq`, `lfe_lp_freq`, `lfe_lp_slope` (pojedynczy LPF, bez kaskady — wg ITU-R BS.775-4 Annex 7). Domyślnie Butterworth 24 dB/okt przy 120 Hz (SPEC_IO_UI §4, P2-08).

### 7.7 Narzędzia fazy
- **Polaryzacja** `sub_pol`: mnożnik ±1 (crossfade 5 ms).
- **Rotacja fazy** `sub_rot` θ (P2-07, ADR-019): para all-pass (sieć Hilberta) — gałęzie A i B, każda z 4 sekcji 1. rzędu `(p − s)/(p + s)` (cyfrowo `(a + z⁻¹)/(1 + a·z⁻¹)`, `a = (g − 1)/(g + 1)`, `g = p/(2·fs)`); bieguny p/2π: A — 4,047 · 30,298 · 119,010 · 514,848 Hz; B — 13,985 · 60,499 · 237,641 · 1778,969 Hz. B wyprzedza A o 90° w paśmie 12–600 Hz z błędem ≤ 0,133° (identycznie przy 44,1/48/96 kHz). `y = cos θ·A + sin θ·B` (równoważnie `cos θ·a − sin θ·b` z b opóźnionym o 90°; dodatnie θ = wyprzedzenie fazy). Amplituda: `|y| = √(1 − sin 2θ·sin ε)` dla błędu ε, więc ±0,05 dB wymaga ε ≤ 0,66°; projekt daje ±0,010 dB. **θ = 0 to gałąź A, nie tożsamość:** faza +71 / +27 / −38 / −141° przy 30 / 45 / 80 / 200 Hz, opóźnienie grupowe 9,8 / 6,2 / 3,2 ms przy 30 / 50 / 100 Hz — wliczone w opóźnienie toru sub kompensowane przez `align_ms` (§9.2). Jest to dopuszczalne, bo gałąź A należy do toru sub, nie do dry.
- **Auto-Align:** §8.B.

### 7.8 Limiter
- **Stopień:** `lim_on` Off → omijanie (crossfade 10 ms).
- **Live:** miękki ogranicznik bez lookahead: `y = c·tanh(x/c)` (ADAA 1. rzędu, F₁ = c²·ln cosh(x/c)), gdzie c to sufit w skali liniowej; gwarantuje |y| < c.
- **Mix/Print (P2-09, ADR-023):** limiter peak z lookahead 2 ms (Mix) / 5 ms (Print). Konstrukcja z gwarancją sufitu: D = round(lookahead·fs); wymagane wzmocnienie `g_req[n] = min(1, c/|x[n]|)`; minimum `g_req` w oknie D + 1 próbek; release jednobiegunowy `lim_release` (spadek natychmiastowy, powrót wykładniczy); wygładzanie dodatnim FIR Hanna o długości D + 1 (suma współczynników 1); wyjście = x opóźnione o dokładnie D próbek × wygładzone wzmocnienie. Każdy współczynnik FIR widzi wartość obejmującą opóźnioną próbkę, więc |y| ≤ c wynika z konstrukcji (P2: przekroczenie 3·10⁻¹⁶). THD przy 40 Hz, wejściu sufit + 6 dB i release 100 ms: Mix ≤ −35 dB, Print ≤ −50 dB (P2: −38,4 / −51,8 dB; Live −15,2 dB). `lim_tp` (Print) liczy peak na sygnale 4× oversamplowanym. Sufit `lim_ceiling` (w trybie LFE `lfe_ceiling`).
- Limiter działa wyłącznie na torze sub; dry nie jest limitowane.

## 8. Korelacja fazowa — trzy warstwy

### 8.A Wejście (detekcja)
Korelacja ρ między `S_L` i `S_R` w paśmie źródłowym: `ρ = ⟨S_L·S_R⟩ / √(⟨S_L²⟩⟨S_R²⟩)`, średnie jednobiegunowe 300 ms, mianownik z podłogą 10⁻¹². Tryby `src_mode` wg §5.2. Wskaźnik ρ w UI; ostrzeżenie W-03, gdy ρ < −0,5 przy Sum.

### 8.B Relacja sub ↔ dry
- **Polaryzacja/rotacja:** §7.7. **Reguła polaryzacji początkowej:** §6.2 i §6.3.
- **Align:** §9.2.
- **Auto-Align (wątek tła; P2-07, ADR-019):** bufory pierścieniowe (do 8 s) pasma dry (sygnał S1 przepuszczony przez [`range_lo`/2, `range_hi`]) i wyjścia toru sub **po gałęzi A stopnia rotacji (θ = 0)**, przed polaryzacją. Analiza suba sprzed rotacji nie zgadza się z tym, co zrobi stopień (na SIG-18 różnica do ~240°). Przeszukiwane są 24 różne obroty ψ = 0°…345° co 15°, bo pary (p, θ) i (−p, θ + 180°) dają to samo z: `z = d + Re{a·e^{jψ}}` (a — sygnał analityczny suba). Kryterium: `align_obj` = Energy → maksymalna energia z w paśmie nakładania [max(range_lo/2, 20 Hz), range_hi]; Headroom → minimalny szczyt |z|. Wynik w postaci kanonicznej: |θ*| ≤ 90° i polaryzacja p* (zamiast θ = ±180°). Kandydaci w granicach 10⁻³ dB od najlepszego to remis — wygrywa najbliższy tożsamości (brak danych → p = +1, θ = 0°). Pewność = przewaga najlepszego nad medianą: ≥ 0,5 dB, w przeciwnym razie „brak wyraźnego optimum”. P2: 48 syntetycznych optimów trafionych w 7,5°; siatka 15° traci ≤ 0,12 dB względem 1°. `align_lock` blokuje zmiany; przycisk Auto jest akcją jednorazową (nie parametrem).
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
`align_ms` (0–40 ms; D6, ADR-024) dodaje opóźnienie dry (linia o rozdzielczości 1 próbki) ponad L_sub, kompensując naturalne opóźnienie grupowe toru sub (pasmo źródłowe, filtry LPF/HPF, gałąź A stopnia rotacji) tak, aby sub i dry były zgodne w czasie w okolicy częstotliwości suba. P2-10 (domyślny łańcuch; D8 M2): opóźnienie toru sub = 34,8 / 24,1 / 19,8 / 12,2 ms przy f_sub1 30 / 40 / 50 / 80 Hz — zakres 0–20 ms z wersji 0.1 nie wystarczał poniżej ~50 Hz. Opóźnienie liczone analitycznie: pasmo źródłowe przy częstotliwości źródła (DIVIDE, TRACK: 2·f_sub1; HARMONIC: f0 źródła; TRIGGER: pomijane, bo sub jest syntetyczny) + filtry toru sub (HP i LP albo filtry LFE) przy f_sub1 + gałąź A rotacji przy f_sub1. UI pokazuje to opóźnienie przy bieżącej `f_sub1` oraz akcję **Auto** (przycisk, nie parametr; MUST), która ustawia `align_ms` na to opóźnienie, zaokrąglone do 0,1 ms i ograniczone do 0–40 ms (T-ALN-01). Zmiana `align_ms` zmienia L_total (parametr N).

### 9.3 Zachowania brzegowe
- Zmiana `sum_mode`: crossfade 20 ms.
- Nieskończoność/NaN w `u`: zerowane przed sumą (§4.7).
- Clipping wyjścia: nie ma automatycznego limitera na sumie (antycel 1); wskaźnik szczytu ostrzega (W-06).

## 10. Decyzje wykonawcy [W] do przeglądu
1. Latencja zależna tylko od `quality`, `filt_phase`, `align_ms` (nie od silnika) — przewidywalność kosztem zbędnego lookahead w Divide/Mix.
2. Domyślny `quality` = Mix (≈ 48,5 ms przy 48 kHz), `engine` = Divide.
3. Definicja „Base Frequency” jako częstotliwości Sub −1.
4. Poziom (`sub_level`) na początku toru; Drive operuje na poziomach bezwzględnych (odniesienie −12 dBFS), co daje przewidywalny próg nasycenia.
5. Pięć charakterów Drive: Soft, Tube, Tape, Fold, Hard. Nazwy per źródło (Bass/Kick/Pre, jak u SSL) to presety, nie tryby.
6. Linear Phase tylko dla LP w Print (D5); HP i PEQ minimum-phase.
7. Standalone nie wchodzi do instalatorów.
8. C++20, JUCE 9.0.4 przypięte do taga.
