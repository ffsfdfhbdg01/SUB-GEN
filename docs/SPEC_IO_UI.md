# SUB-GEN — SPEC (część 2): kanały, routing, LFE, metering, UI, stan i presety

Wersja dokumentu: 0.1 (do akceptacji) · Data: 2026-10-19 · Powiązane: [SPEC.md](SPEC.md), [PARAMETERS.md](PARAMETERS.md), [TEST_PLAN.md](TEST_PLAN.md)

## 1. Klasyfikacja kanałów
Wszystkie kanały klasyfikuje się po `AudioChannelSet::getTypeOfChannel` (nigdy po indeksie). Nazwy typów pochodzą z JUCE 9.0.4 (`AudioChannelSet::ChannelType`); dokumentacja JUCE zaznacza, że nazewnictwo kanałów bocznych/tylnych różni się między AAX, VST3 i AU — klasyfikacja do pięciu klas czyni te różnice nieistotnymi.

| Klasa (parametry) | Typy kanałów JUCE | Strona |
|---|---|---|
| **Front** (`feed_front`, `route_front*`) | `left`, `leftCentre`, `wideLeft` · `right`, `rightCentre`, `wideRight` | L · R |
| **Center** (`feed_center`, `route_center*`) | `centre` (także jedyny kanał layoutu mono) | C |
| **Surround** (`feed_surround`, `route_surround*`) | `leftSurround`, `leftSurroundSide`, `leftSurroundRear` · `rightSurround`, `rightSurroundSide`, `rightSurroundRear` · `centreSurround` | L · R · C |
| **Height** (`feed_height`, `route_height*`) | `topFrontLeft`, `topRearLeft`, `topSideLeft` · `topFrontRight`, `topRearRight`, `topSideRight` · `topFrontCentre`, `topRearCentre`, `topMiddle` | L · R · C |
| **LFE** (`feed_lfe`, `route_lfe*`, `lfe_*`) | `LFE` | – |
Nieobsługiwane w v1.0: `LFE2` (kanał przepuszczany bez zmian), kanały ambisonic (`isBusesLayoutSupported` odrzuca taki layout), kanały `discrete`/`unknown`.

## 2. Layouty i konfiguracje I/O

### 2.1 Obsługiwane layouty
Wszystkie fabryki `AudioChannelSet` z klasami z §1: mono, stereo, LCR, LRS, LCRS, quadraphonic, 5.0, 5.1, 6.0, 6.1, 6.0 Music, 6.1 Music, 7.0, 7.1, 7.0 SDDS, 7.1 SDDS, 5.0.2, 5.1.2, 5.0.4, 5.1.4, 7.0.2, 7.1.2, 7.0.4, 7.1.4, 7.0.6, 7.1.6, 9.0.4, 9.1.4, 9.0.6, 9.1.6 (także warianty ITU), pentagonal, hexagonal, octagonal.
**Poziomy testów:** L1 — automatyczne offline + ręcznie w Cubase 13: stereo, 5.1, 7.1.2, 7.1.4. L2 — wyłącznie automatyczne offline: wszystkie pozostałe.

### 2.2 Szyny i konfiguracje
- **Main in/out:** domyślnie ten sam layout (in == out).
- **MUST — warianty „dokładające LFE” (x.0 → x.1):** 5.0→5.1, 6.0→6.1, 7.0→7.1, 5.0.2→5.1.2, 5.0.4→5.1.4, 7.0.2→7.1.2, 7.0.4→7.1.4, 7.0.6→7.1.6, 9.0.4→9.1.4, 9.0.6→9.1.6 (jak LoAir 5.0→5.1). Wejście bez LFE, wyjście z kanałem LFE zasilanym z toru sub; pozostałe kanały to dry. Wsparcie po stronie hosta [S].
- **SHOULD — warianty wet-only z innym wyjściem:** mono→stereo, mono→5.1, mono→7.1 (sub z jednej ścieżki na szynę surround).
- **Sidechain:** opcjonalna szyna wejściowa (mono lub stereo), domyślnie wyłączona; jedynie dla `sc_use` ≠ Off.
- **`isBusesLayoutSupported`:** akceptuje in == out dla layoutów z §2.1, pary z §2.2 oraz dowolną opcjonalną szynę sidechain; w razie wątpliwości zwraca false i host dobiera in == out.
- **Zmiana layoutu w sesji:** `prepareToPlay` od nowa; router przebudowany; parametry zachowane. Layout bez LFE przy `out_mode` = LFE → W-01 i zachowanie jak Mono.

### 2.3 Mapowanie w czasie rzeczywistym
W `prepareToPlay` budowane są tablice: dla każdego kanału wejścia i wyjścia: (klasa, strona, indeks w buforze bloku przez `getChannelIndexInProcessBlockBuffer`). W `processBlock` żadnych wyszukiwań ani alokacji.

## 3. Wyjście: tryby (`out_mode`)
`Auto` rozwiązuje się do **LFE**, gdy wyjściowy layout ma kanał LFE, inaczej do **Mono**. Tabela trybów po rozwiązaniu:

| Tryb | Aktywne klasy i wzmocnienia | Strumienie sub | Zachowanie |
|---|---|---|---|
| Mono | Front `route_front`, Center `route_center`; pozostałe klasy 0 | 1 (suma) | ten sam mono-sub dodawany do kanałów Front i Center; opóźnienie/polaryzacja z `route_*_dly`/`_pol` |
| Stereo | Front (L/R) `route_front` | 1 lub 2 | gdy `src_mode` = Per-Channel i `sub_width` > 0: kanał L dostaje SL, R dostaje SR; w przeciwnym razie mono |
| Surround | Front, Center, Surround, Height, LFE wg `route_*` | 1 lub 2 | dowolne kombinacje klas; sub dla LFE przechodzi dodatkowo przez filtry LFE (kaskada z `sub_lp_*` — dla czystego LFE użyj trybu LFE) |
| LFE | wyłącznie LFE: `lfe_trim` | 1 (suma) | patrz §4; inne kanały to wyłącznie dry |
**Szerokość:** `sub_width` w ∈ [0,1] przy dwóch strumieniach: `SL' = (1−w)·(SL+SR)/2 + w·SL`, analogicznie `SR'`. Center otrzymuje `(SL'+SR')/2`.

**Router (S7):** dla kanału wyjściowego j: `u_j = G_j · pol_j · delay(D_j)(strumień)`, gdzie `G_j` — wzmocnienie klasy (wygładzane G20), `D_j` — opóźnienie klasy w próbkach (z `route_*_dly`), `pol_j` — ±1. Klasa → parametry: Front — `route_front`, `route_front_dly`, `route_front_pol`; Center — `route_center`, `route_center_dly`, `route_center_pol`; Surround — `route_surround`, `route_surround_dly`, `route_surround_pol`; Height — `route_height`, `route_height_dly`, `route_height_pol`; LFE — `route_lfe` (tylko w trybie Surround), `route_lfe_dly`, `route_lfe_pol` (także w trybie LFE). Opóźnienia zaokrąglone do próbki, linie wstępnie zaalokowane na maksimum (30 ms przy 192 kHz).

## 4. Tryb LFE — specyfikacja

1. **Wyjście:** tylko kanał LFE (mono); pozostałe kanały to dry (lub cisza przy Wet Only). Gdy wejście ma LFE (np. 5.1→5.1), `LFE_out = LFE_dry + sub` (Add) lub `sub` (Wet Only).
2. **Źródło:** standardowe `feed_*`. Typowo Front i Center włączone, Surround/Height wyłączone; `feed_lfe` włączane przy wzmacnianiu istniejącego LFE.
3. **Tor:** różnice względem trybu normalnego: filtry zastąpione przez `lfe_hp_freq` (HP) i `lfe_lp_freq` + `lfe_lp_slope` (LP, pojedyncza sekcja, bez kaskady z `sub_lp_*`); limiter na `lfe_ceiling` (domyślnie −1,0 dBFS); poziom `lfe_trim`. Drive stoi przed filtrami, więc harmoniczne ponad pasmem LFE są odcinane.
4. **Pasmo:** 20–120 Hz [ITU-R BS.775-4 rekomendacje 4–5 i Annex 7; ATSC A/52]; domyślne nachylenie LPF 24 dB/okt (łagodniejsze niż stromy filtr 8. rzędu, bo enkoder dodaje własny, a Annex 7 ostrzega przed kaskadą LPF i opóźnieniem grupowym). W Print dostępny Linear Phase.
5. **Konwencje poziomu (`lfe_conv`) — dwie:**
   - **+10 dB** — film, DVD-Video, definicja LFE w A/52 i wymiana programów wg ITU-R BS.775-4 (zapis z offsetem −10 dB kompensowanym +10 dB przy odtwarzaniu); sygnał z pliku odtwarza się o 10 dB głośniej względem kanału głównego o tym samym poziomie cyfrowym.
   - **0 dB** — muzyka na nośnikach (DVD-Audio, SACD): brak offsetu odtwarzania.
   Wtyczka **nie zmienia poziomu**; pokazuje dBFS oraz „szacowany poziom akustyczny względem kanału głównego” = dBFS + 10 dB (konwencja +10) lub + 0 dB. Różnica względem pierwotnych trzech konwencji z Fazy 0: „Wymiana −10 dB” i „Film +10 dB” opisują ten sam efekt netto (+10 dB przy odtwarzaniu) i nie są osobnymi ustawieniami.
6. **Metering LFE:** RMS i peak w paśmie 20–120 Hz (RMS 300 ms), true-peak w Print; nie LUFS (LFE nie jest uwzględniane w BS.1770 [W]).
7. **Downmix:** LFE bywa pomijane przy downmixie do stereo (BS.775-4 Annex 7; A/52 — LFE nie występuje w równaniach downmixu AC-3). `lfe_bleed` kieruje kopię sygnału LFE (po filtrach LFE) do klasy Front/Center na poziomie `lfe_bleed`. **W-05** pojawia się, gdy tryb = LFE, `lfe_bleed` = Off i RMS sub w LFE przewyższa RMS dry kanałów Front w paśmie 20–120 Hz o więcej niż −6 dB.
8. **Faza względem bass managementu:** `route_lfe_dly`, `route_lfe_pol`, `sub_rot` dopasowują sub do basu przekierowanego z kanałów głównych w pobliżu crossovera (~80 Hz); automatyczne „Match” — COULD.
9. **Atmos:** kanał LFE występuje wyłącznie w bedach (7.1.2 itd.); obiekty nie mogą zasilać LFE [F]. Wtyczka przetwarza bed jak dowolny layout.
10. **Pro Tools / AAX:** poza release v1.0 [U]; cel CI.

## 5. Wet Only i szyny pomocnicze
- Wet Only: dry wyciszone (10 ms), latencja bez zmian, kanały bez przypisanego suba milczą — gotowe do aux/send.
- Mono→N (SHOULD): wejście mono, wyjście layoutu wielokanałowego z routingiem wg klas; dry nieobecne (brak kanałów dry poza Center).

## 6. Metering i analiza
| Element | Algorytm | Odświeżanie | Uwagi |
|---|---|---|---|
| Wejście (źródło) | peak + RMS 300 ms ze strumienia po S1 | 30 Hz | skala −60…0 dBFS |
| Sub | peak + RMS po limiterze (przed routerem) | 30 Hz | |
| Wyjście | peak i true-peak (4×, UI) per grupa kanałów; wskaźnik clip | 30 Hz | W-06 |
| GR / bramka | GR kompresora i limitera, stan bramki | 60 Hz | |
| Drive | różnica RMS wyjścia/wejścia stopnia + LED zakresu nasycenia | 30 Hz | |
| Wysokość | f(Sub −1) w Hz, nuta + cents, pasek pewności | 15 Hz | gdy silnik dostarcza estymatę |
| Widmo | FFT 16384, okno Hanna, nakładanie 75%, wygładzanie 1/6 okt.; osie 10–500 Hz (log); krzywe: dry (po S1), sub (po limiterze), suma (średnia kanałów Front) | 20 Hz | nakładka ISO 226:2023 przy 40/60/80 phon — SHOULD, dane z implementacji referencyjnej [P2] |
| Oscyloskop | okno 50–500 ms; dry w paśmie LF, sub, suma; wyzwalanie na narastającym przejściu przez zero suba | 30 Hz | |
| Korelometr | korelacja L/R wyjścia 20–150 Hz (300 ms) + zgodność mono (SPEC §8.C) | 30 Hz | |
| LFE | RMS/peak 20–120 Hz + odczyt konwencji (§4 pkt 5) | 30 Hz | tylko tryb LFE |
Dane do UI: bufory audio → `AbstractFifo` (SPSC) → wątek UI; widmo liczone w wątku UI/tła, nie w audio. Zamknięty edytor nie powoduje żadnego dodatkowego kosztu w wątku audio (FIFO zapisywane tylko przy otwartym edytorze).

## 7. UI

### 7.1 Zasady
- Natywny JUCE (komponenty wektorowe), HiDPI, skalowanie 75–200% przy stałym aspekcie; domyślnie 1000 × 620 px przy 100% [W]. Ciemny motyw domyślny; jasny — COULD.
- Język UI: angielski (standard branżowy).
- Interakcje: podwójne kliknięcie = reset do wartości domyślnej; Shift-przeciąganie = precyzyjna zmiana; kółko myszy = krok; menu kontekstowe → „Enter value…”; Alt-kliknięcie = reset fine — zgodnie z konwencjami JUCE. Wszystkie wartości z jednostkami w tooltipach.
- Rysowanie bez alokacji w `paint`; timer UI 30–60 fps; brak wpływu na wątek audio.
- Dostępność: etykiety dostępności dla wszystkich kontrolek, kolejność fokusu klawiaturą; wariant wysokiego kontrastu — SHOULD.

### 7.2 Widok Main (kolumny)
| Sekcja | Kontrolki (ID) |
|---|---|
| Pasek górny | A/B, Undo/Redo, przeglądarka presetów, `quality`, `bypass` |
| **SOURCE** | `src_mode`, zakres dwuuchwytowy `range_lo`/`range_hi` z `range_slope`, `det_thresh`, `src_gain`, sekcja Feed (`feed_front`, `feed_center`, `feed_surround`, `feed_height`, `feed_lfe`), `sc_use`, `sc_gain` |
| **GENERATOR** | `engine`, `base_mode`/`base_freq`, `sub1_level`, `sub2_level`, `shape`, `follow`, `env_attack`, `env_release`, `dynamics`, `conf_gate`; parametry silnika (kontekstowo: TRIGGER `trig_*`, TRACK `trk_*`/`phase_gen`, HARMONIC `harm_*`, HYBRID `hyb_*`) |
| **SUB** | **`drive`** (duże pokrętło główne) + `drive_mode`, `drive_balance`, `drive_tone`, `drive_auto`, `drive_pos`; `sub_level`; Boost (`boost_gain`, `boost_type`, `boost_freq`, `boost_follow`, `boost_q`); Gate; Comp; `sub_pol`, `sub_rot`, Auto-Align (przycisk), `align_obj`, `align_lock` |
| **OUTPUT** | `out_mode`, `sum_mode`, `dry_level`/`mix`, `mix_law`, `wet_only`, `align_ms`, `out_trim`, klasy routingu (`route_*`), `sub_width` |
| Dół | analizator (widmo/oscyloskop), metry, korelometr, wskaźnik wysokości i pewności |
Widok **Advanced** (zakładki): EQ (5 pasm z krzywą), Filtry i Limiter (`sub_*`, `filt_*`, `lim_*`), Routing (opóźnienia/polaryzacje klas), LFE (`lfe_*`), Ustawienia (skala, motyw).

### 7.3 Szkic układu (Main)
```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ [A|B] ↶ ↷  ◀ Preset: Kick Beef ▶ [Save]                       Quality [Live|Mix|Print]  ⏻│
├───────────────┬────────────────────┬──────────────────────────────┬───────────────────────┤
│ SOURCE        │ GENERATOR          │ SUB                          │ OUTPUT                │
│ mode ▾        │ engine ▾           │        ╭────────╮            │ out mode ▾            │
│ range lo━hi   │ base freq A/F  45  │        │ DRIVE  │  mode ▾    │ Add | Mix | Wet Only  │
│ threshold     │ Sub−1  Sub−2       │        ╰────────╯  tone bias  │ dry / mix  align      │
│ feed (5)      │ shape follow A R   │ level  boost  gate  comp     │ Front Ctr Sur Hgt LFE │
│ key           │ dynamics conf      │ ⌀  rot  [Auto] lock          │ width  trim           │
├───────────────┴────────────────────┴──────────────────────────────┴───────────────────────┤
│ Spectrum 10–500 Hz (dry · sub · sum)  |  Scope  |  Meters · Correlation · Pitch ● conf    │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```
Makiety wizualne w Fazie 4 (opcjonalnie z podglądem w przeglądarce — decyzja użytkownika).

### 7.4 Ostrzeżenia i stany
| ID | Warunek | Komunikat | Reakcja |
|---|---|---|---|
| W-01 | `out_mode` = LFE, brak kanału LFE | „No LFE channel in this layout” | zachowanie Mono |
| W-02 | `sc_use` ≠ Off, szyna sidechain nieaktywna | „External key not connected” | źródło z main |
| W-03 | ρ < −0,5 przy `src_mode` = Sum | „Source is out of phase — use Auto” | – |
| W-04 | sub L/R w sumie mono znoszą się (ρ_sub < −0,5) | „Sub L/R cancel in mono” | – |
| W-05 | patrz §4 pkt 7 | „Content only in LFE is lost in stereo downmix” | – |
| W-06 | szczyt wyjścia ≥ 0 dBFS | wskaźnik clip | – |
| W-07 | zmiana `quality`/`filt_phase`/`align_ms` | „Latency changed — restart playback” | – |
| W-08 | Linear Phase przy `quality` ≠ Print | „Linear phase requires Print” | stosuje Min Phase |
| W-09 | pewność < próg | „Low confidence — sub gated” | wygaszanie |
| W-10 | `range_hi` < 1,2·`range_lo` | komunikat korekty | auto-clamp |
| W-11 | podgląd odsłuchowy aktywny podczas renderu offline | „Preview disabled for render” | auto-wyłączenie (SHOULD) |
| W-12 | layout nieobsługiwany (ambisonic) | „Layout not supported” | przepuszczanie |

## 8. Presety
- **Format pliku:** XML `*.sgpreset` [W] (nazwa rozszerzenia powiązana z produktem; zmieniana razem z nazwą produktu): `<Preset name category author version>` + zrzut parametrów. Katalogi: użytkownik — macOS `~/Library/Application Support/<Vendor>/<Product>/Presets/`, Windows `%APPDATA%\<Vendor>\<Product>\Presets\`; fabryczne — macOS `/Library/Application Support/<Vendor>/<Product>/Presets/Factory/`, Windows `%PROGRAMDATA%\<Vendor>\<Product>\Presets\Factory\`; kopia zapasowa osadzona (BinaryData).
- **Fabryczne (v1.0: 32):** Kick Beef (6): Tight 808 Kick Sub · Rock Kick Weight · Punch + Body · Club Kick Floor · Trigger Thump · Soft Sine Kick; Bass/808 (6): 808 Glide Sub · Bass Guitar Octave · Synth Bass Floor · Sine Reinforce · Saturated 808 · Clean Octave Down; Mix-bus Low-End (4): Gentle Depth · Wide Mix Floor · Master Low Support · Parallel Sub; Cinematic Boom/Impact (6): Big Hit · Deep Boom · Hybrid Rumble · Impact Drop · Riser Tail · Sub Drop; LFE (6): LFE From Fronts 5.1 · LFE Enhance 5.1 · LFE 7.1.4 Bed · LFE Impact · LFE Safe Ceiling · Music 0 dB LFE; Utility (4): Init · Wet Only Aux · Phase Check · Engine Test Tone.
- **Programy hosta:** presety fabryczne eksponowane jako programy hosta (SHOULD).
- **A/B:** dwa pełne zrzuty parametrów w stanie wtyczki; przełączenie natychmiastowe (wygładzanie parametrów zapewnia brak trzasków); „Copy A→B”.
- **Undo/Redo:** `juce::UndoManager` na APVTS; łączenie edycji w oknie 500 ms; limit 100 kroków.

## 9. Stan wtyczki (serializacja)
- XML (`<SUBGEN stateVersion="1">`) zawierający: drzewo parametrów APVTS, `<UI scale page abSlot theme/>`, `<AB>` (oba sloty), `<ALIGN pol rot conf/>`, `<PRESET name category/>`. Stan binarny dla hosta = skompresowany XML.
- **Migracja:** łańcuch funkcji `migrate(vN → vN+1)`; nieznane parametry ignorowane; brakujące ustawiane na wartości neutralne (PARAMETERS §5); testy: stan z v1.0 ładuje się w kolejnych wersjach bez zmiany brzmienia (T-STATE).
- **Stabilność:** ID parametrów niezmienne po wydaniu 1.0 (PARAMETERS §5).
