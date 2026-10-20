# SUB-GEN — Raport Fazy 2: prototyp DSP (Python)

Data: 2026-10-20 · Gałąź: `dev` (lokalnie, nic nie wypchnięto) · Stan: **czeka na Twoją akceptację (bramka Fazy 2)**
Plan: `docs/superpowers/plans/2026-10-19-subgen-phase2-prototype.md` · Kod: `proto/` · Wykresy: `docs/img/` · Pliki odsłuchowe: `renders/` (poza gitem)

## 0. W skrócie

- Zbudowałem offline'owy prototyp wszystkich bloków DSP ze SPEC: filtry TPT/Cytomic, silniki DIVIDE, TRACK, TRIGGER i HARMONIC, Drive z ADAA i oversamplingiem, parę Hilberta z rotacją i Auto-Align, filtry LFE z FIR liniowofazowym, kompresor/bramkę/limiter, budżet latencji i pliki odsłuchowe.
- **216 testów jednostkowych — wszystkie zielone** (z `-W error`). **11 eksperymentów, 86 kontroli: 80 PASS, 6 FAIL.** Każdy FAIL to celowa kontrola „dosłownie według SPEC”, która dokumentuje wadę SPEC (§5).
- **22 pliki odsłuchowe** (UC1–UC5) z `renders/README.txt` oraz skrypt **`render_file`** do Twojego materiału (§8).
- Najważniejsze wnioski:
  1. **DIVIDE** dzieli częstotliwość dokładnie (błąd ~2·10⁻⁹ %), bez nieciągłości i ze stałą polaryzacją uderzenie w uderzenie (100/100). Reguła pewności z SPEC §6.2 zamyka jednak bramkę na każdej stopie (spadek wysokości) — zastąpiłem ją regułą „trendu”.
  2. **TRACK** zna wysokość dopiero po ~2 okresach nuty. L_look ze SPEC (Mix 25 / Print 50 ms) wyrównuje sub z dry tylko dla nut ≥ 100 / ≥ 45 Hz. Proponuję **Mix 47 / Print 65 ms**.
  3. **HARMONIC**: obwiednia ze SPEC (peak follower 5/60 ms) zaniża amplitudę o ~1 dB i faluje, przez co wagi harmonicznych są błędne o 13,6 dB. Max-hold z 1,5 okresu daje 0,017 dB.
  4. **Drive**: Auto Gain trzyma ±0,04 dB. Aliasing trybu Hard przy drive 100 % to w Live −55 dB, a w Mix −77 dB, więc cele SPEC (−80/−90 dB) nie są spełnione. Print daje −95 dB ✓. Po filtrze LP toru sub zostaje jednak tylko −129 dB nawet w Live.
  5. **Kompresor**: zniekształcenia zależą od detektora, nie od „reguły tętnień”. Detektor bez tętnień daje THD −124 dB przy dowolnych czasach. Reguła pomaga tylko detektorom z tętnieniami i nawet z nią nie schodzą poniżej −57 dB.
  6. **Para Hilberta**: wystarczą 4 sekcje na gałąź (błąd 0,13°). Kryteria SPEC (1° i ±0,05 dB) są sprzeczne. **θ = 0 nie jest tożsamością**: gałąź A ma +71…−141° fazy i 9,8 ms opóźnienia przy 30 Hz. Dlatego Auto-Align musi analizować sub *po* gałęzi A.
  7. **Linear Phase**: celem ma być |H|, nie |H|². Kaiser β ≈ 3, a nie 8. HP 20 Hz kosztuje ~159 ms latencji (48 kHz); sam LP 120 Hz — 39 ms.
  8. **LFE**: domyślnie **Butterworth 24 dB/okt przy 120 Hz**: 14,25 dB przy 1,5·fc, −0,17 dB przy 80 Hz, 3,65 ms przy 40 Hz.
  9. **Opóźnienie toru sub** (pasmo źródłowe + LP + rotacja) wynosi 26 ms przy 40 Hz i 37 ms przy 30 Hz. Zakres `align_ms` 0–20 ms nie wystarcza.
  10. Kilka testów z TEST_PLAN mierzy nie to, co powinno. Przykłady: T-ALIAS-01 jest ślepy dla tonów 30/80/120 Hz przy 48 kHz, T-TRIG-03 liczony przez Hilberta jest obciążony, a T-DRV-05 nie określa kroku. Poprawki są w §6.

## 1. Zakres, metoda i odtworzenie

- **Moduły** (`proto/subgen_proto/`): `util` (pomiary), `signals` (SIG-01…19), `svf` (sekcje TPT/Cytomic, projekty BW/LR/Bessel), `envelope`, `band` (pasmo źródłowe x_b), `divide`, `track`, `trigger`, `harmonic`, `drive`, `hilbert`, `align`, `lfe`, `dynamics`, `render`. Razem ~1900 linii plus ~2300 linii testów.
- **Testy:** `proto/tests/` (216). **Eksperymenty:** `proto/experiments/p2_01…p2_11` zapisują JSON do `proto/out/p2_NN.json` (poza gitem) i wykresy do `docs/img/p2_NN_*.png`.
- **Odtworzenie:**
  ```
  PYTHONPATH=proto .venv/bin/python -W error -m unittest discover -s proto/tests -t proto
  PYTHONPATH=proto .venv/bin/python -m experiments.p2_01_filters      # … do p2_11_renders
  ```
- **Proces:**
  - TDD (test czerwony → implementacja → test zielony).
  - Testy mutacyjne kluczowych zachowań; każdy mutant został wykryty.
  - Dziennik decyzji: pełna lista rozstrzygnięć w Załączniku C.
  - Na końcu przegląd całej gałęzi, wykonany przeze mnie, bez niezależnego recenzenta (§9, decyzja D8). Przegląd znalazł dwa błędy w `render_file`; oba są naprawione z testami.
- **Ograniczenia prototypu:**
  - Działa offline na całych buforach.
  - Nie obejmuje przetwarzania blokowego, wygładzania parametrów, wielokanałowości silników, strażników NaN ani wątków.
  - Te elementy sprawdzą testy C++ w Fazie 3, na danych złotych z prototypu.

## 2. Wyniki P2-01…P2-11

| P2 | Blok | Kryterium (SPEC / TEST_PLAN) | Zmierzono | Status |
|---|---|---|---|---|
| P2-01 | Filtry TPT/Cytomic | BW/LR/Bessel vs SciPy ≤ 0,01 dB (T-FILT-01, -04); Cytomic vs RBJ ≤ 0,05 dB (T-FILT-02) | 2,3·10⁻⁷ dB; 1,1·10⁻⁷ dB; pętla TPT vs analityka przy 15 Hz/192 kHz: 7·10⁻¹³; współczynniki float32: 0,175 dB (uzasadnia `double`) | PASS |
| P2-01 | — | zmienność opóźnienia grupowego Bessela do fc/2 ≤ 1 % (T-FILT-04) | rząd 2: **2,02 %**, rzędy 4/8: 0,01/0,00 % | INFO → K-17 |
| P2-02 | DIVIDE | częstotliwość Sub −1/−2 ≤ 0,1 % (T-DIV-01) | ~2·10⁻⁹ % (44,1/48/96 kHz, 30–200 Hz) | PASS |
| P2-02 | — | ciągłość, polaryzacja, czystość sinusa (shape 0, 3f/2 ≤ −18 dB) | \|Δy\| ≤ \|Δx\|; 100/100 uderzeń, korelacja 1,0; 3f/2 −23,4 dB (natywnie −4,4 dB) | PASS |
| P2-02 | — | bramka pewności (T-DIV-04/05) | stopa: otwarta 100 % (reguła ze SPEC: 0 %); SNR w paśmie 20/15/10/5/3 dB → otwarta 99/68/27/8/4 %; szum losowy ≤ 1,5 %; akord 0 % | PASS (po korekcie K-02) |
| P2-03 | TRACK | błąd ≤ 0,5 %, 0 błędów oktawowych (T-TRK-01) | MPM: najgorsza mediana 1,8 centa, 0 błędów (SIG-01/04/05); YIN: 36 klatek z błędem oktawowym (SIG-05) | PASS (MPM) |
| P2-03 | — | opóźnienie glissanda 80→40 Hz (T-TRK-02 [P2]) | 43 ms przy oknie 83,5 ms (≈ W/2 + 1 ms) | INFO → granica 50 ms |
| P2-03 | — | zamek fazy ≤ 15° (T-TRK-04) | PLL 5 Hz: 1,35° (czysty sygnał), 5,9° (SNR 10 dB) | PASS |
| P2-04 | TRIGGER | onset ≤ 1 próbka / Live ≤ 2 ms (T-TRIG-01) | 0,49 próbki (stały bias −0,486, rozrzut 0,0067); latencja 0,18 ms | PASS |
| P2-04 | — | determinizm ≤ −120 dB; krzywe ≤ 0,5 % / 0,1 dB; Restart ≤ 10⁻³; Legato: ciągła faza (T-TRIG-02…04) | −211 dB; 0,0002 % / 0,023 dB; 4,3·10⁻⁴; reszta 2·10⁻¹⁶ | PASS |
| P2-05 | HARMONIC | wagi ≤ 0,5 dB, DC ≤ −80 dBFS (T-HARM-01) | 0,017 dB; −248 dBFS (obwiednia ze SPEC: 13,6 dB) | PASS (po korekcie K-07) |
| P2-05 | — | IMD dwóch tonów (T-HARM-02, informacyjnie) | 72 % mocy wyjścia to intermodulacja (najsilniejsza linia: f1+f2) | INFO |
| P2-06 | Drive | harmoniczne vs wzorzec 16× ≤ 1 dB (T-DRV-01) | Live 0,11 / Mix 0,03 / Print 0,01 dB | PASS |
| P2-06 | — | ADAA vs naiwny (T-DRV-02), Auto Gain ±0,5 dB (T-DRV-03), DC ≤ −80 dBFS (T-DRV-04), drive 0 bit-exact (T-DRV-05) | −7,8 dB aliasingu; 0,036 dB; −291 dBFS; ✓ | PASS |
| P2-06 | — | aliasing Live ≤ −80, Mix ≤ −90, Print ≤ −90 dB (T-ALIAS-01 [P2]) | Hard 100 %: Live **−55**, Mix **−77**, Print −95 dB; Fold: brak aliasingu | **2× FAIL** → K-09, D2 |
| P2-07 | Faza | para Hilberta ≤ 1° (T-FILT-05); rotacja ±1° / ±0,05 dB (T-PHS-02); polaryzacja (T-PHS-01) | 3 sekcje/gałąź: 0,86°, 4: 0,133°; rotacja 0,133° / 0,010 dB; x + (−x) = 0 | PASS |
| P2-07 | — | Auto-Align ±15° (T-PHS-03) | 48 syntetycznych optimów w 7,5°; brak nakładania → pewność 0; SIG-18: siatka 15° traci ≤ 0,12 dB | PASS |
| P2-08 | LFE/FIR | 24 dB/okt → ≥ 14 dB przy 1,5·fc (T-LFE-01); opóźnienie wyświetlane vs zmierzone ±2 % (T-LFE-07) | 14,31 dB (szum szerokopasmowy); 0,0005 % | PASS |
| P2-08 | — | FIR: symetria, ≤ 0,1 dB, opóźnienie (N−1)/2 (T-FILT-06) | β 3: 0,061 dB ✓; β 8 (wartość ze SPEC): **0,194 dB** | PASS / **FAIL (SPEC)** → K-12 |
| P2-09 | Dynamika | krzywa statyczna ≤ 0,1 dB (T-CMP-01); czasy ±10 % (T-CMP-03); bramka ±10 % (T-GATE-01) | ✓; ✓; 0,05 % | PASS |
| P2-09 | — | THD ≤ −60 dB przy GR 6 dB (T-CMP-02) | detektory bez tętnień: −124 dB; RMS jednobiegunowy + reguła: **−56,9 dB** | PASS / **FAIL (SPEC)** → K-13 |
| P2-09 | — | limiter: sufit (T-LIM-01); THD (T-LIM-02 [P2]) | przekroczenie 3·10⁻¹⁶; THD od −15 (Live) do −71 dB | PASS / INFO |
| P2-10 | Latencja | L_look ze SPEC wyrównuje TRACK od 55 Hz; `align_ms` kompensuje tor sub | 25 ms → dopiero od 100 Hz; tor sub przy 40 Hz = 25,9 ms > 20 ms | **2× FAIL** → K-05, K-15 |
| P2-11 | Odsłuch | UC1–UC5, dopasowanie głośności, sub Off = wejście, determinizm, LFE | 22 pliki, 15 MB; UC3 bit-exact; UC4 −191 dB; UC5 tylko FL/FR/LFE, separacja 57 dB | PASS |

Wykresy: [filtry](img/p2_01_responses.png) · [DIVIDE](img/p2_02_divide.png) · [TRACK](img/p2_03_track.png) · [TRIGGER](img/p2_04_trigger.png) · [HARMONIC](img/p2_05_harmonic.png) · [Drive](img/p2_06_drive.png) · [faza](img/p2_07_phase.png) · [LFE/FIR](img/p2_08_lfe.png) · [dynamika](img/p2_09_dynamics.png)

## 3. Wybrane warianty

**DIVIDE — licznik (odwracanie znaku) czy „phase halving” (Hilbert).**
Wybieram **licznik jako podstawę** plus tor wygładzający: LP 4. rzędu, który śledzi 0,87·f_est. Licznik ma zerową latencję, a sub jest sprzężony z kształtem fali, więc polaryzacja jest identyczna przy każdym uderzeniu, a częstotliwość dokładna.
„Phase halving” daje czystsze widmo (3f/2 na −123 dB) i ~3× mniejszy błąd częstotliwości w szumie. Wymaga jednak sygnału analitycznego (FIR Hilberta, czyli dodatkowej latencji), dlatego proponuję go co najwyżej jako wariant dla Print (COULD).

**TRACK — detektor i tryb fazy.**
- **MPM (NSDF)** na ramkach „świadomych onsetu” (cisza przed nutą jest wycinana).
- Pewność skalibrowana do jasności NSDF: 0,6 → 0, 1,0 → 1.
- Prior „trzymaj + 3 zgodne ramki”, stosowany także do pierwszej wartości.
- **YIN odrzucony**: błędy oktawowe na SIG-05 i bramka otwarta na złych oktawach przy niskim SNR.
- **PLL** służy tylko do zamka fazy na materiale ustalonym. Przy paśmie 5 Hz daje 1,35° (5,9° przy SNR 10 dB, gdzie Hilbert ma 13,3°). Musi startować z częstotliwości podanej przez MPM, a do glissand potrzebuje ≥ 20 Hz pasma.
- TRACK **nie nadąża za spadkami wysokości stopy i 808**: estymata stoi na średniej z okna przez 100–150 ms. Do tych sygnałów służą DIVIDE i TRIGGER (UC1, UC4); TRACK służy do linii basowych (UC2).
- Okno: 2,5 okresu `range_lo`. Minimum to 2 okresy, bo lag musi zmieścić się w W/2.

**LFE.**
Domyślnie Butterworth 24 dB/okt przy 120 Hz. W kaskadzie z (założonym) filtrem enkodera daje −0,18 dB przy 80 Hz, −3,11 dB przy 120 Hz i opóźnienie 11 / 14,5 / 39 ms przy 40 / 80 / 120 Hz. Przy 120 Hz opóźnienie pochodzi głównie z enkodera.

**Linear Phase.**
Cel |H|, okno Kaisera β = 3. Długości dla 0,1 dB (HP 20 Hz + LP 120 Hz):

| fs | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| N (β 3) | 12 130 | 15 256 | 30 475 | 60 924 |
| latencja | 137,5 ms | 158,9 ms | 158,7 ms | 158,7 ms |

Koszt wyznacza narożnik HP (przy 48 kHz):

| HP | 20 Hz | 30 Hz | 40 Hz | 60 Hz | sam LP 120 Hz |
|---|---|---|---|---|---|
| latencja | 159 ms | 108 ms | 81 ms | 61 ms | 39 ms |

**Para Hilberta.**
4 sekcje 1. rzędu na gałąź (razem 8 all-passów): błąd 0,133°, amplituda rotacji ±0,010 dB, identycznie przy 44,1/48/96 kHz. Bieguny są w Załączniku B.

**Dynamika.**
- Detektor RMS: średnia kwadratowa w oknie max(10 ms, 1,5·T_sub), a potem hold w tym samym oknie.
- Detektor Peak: hold |x| w tym samym oknie.
- Auto release: krótsze tłumienie zwalnia w 50 ms, długie w 500 ms (wolna gałąź ma atak 150 ms).
- Limiter: lookahead złożony z min-hold, release i wygładzania Hanna — z konstrukcji nie przekracza sufitu.
- Live: c·tanh(x/c) z ADAA.

## 4. Wartości do wpisania w SPEC

| Parametr | Wartość | Źródło |
|---|---|---|
| L_look Live / Mix / Print | **0 / 47 / 65 ms** (TRACK 45/55 + kompresor 0/5 + limiter 2/5) | P2-03, P2-10 |
| L_os | odczyt `getLatencyInSamples()` z JUCE w Fazie 3; prototyp (FIR Kaisera 110 dB) daje 72 próbki — to górne ograniczenie | P2-06, P2-10 |
| L_lin | (N−1)/2 przy **nieparzystym N** (tabela §3) | P2-08 |
| L_total przy 48 kHz | Live 0; Mix 48,5 ms; Print 66,5 ms; Print + Linear Phase (HP 20 + LP 120) 225 ms | P2-10 |
| TRACK | fs_d = fs/⌊fs/2000⌋ (2004,5 Hz przy 44,1 kHz); W = ⌈2,5·fs_d/max(range_lo, 20)⌉; hop 2 ms; próg MPM 0,9; pewność = clip((jasność − 0,6)/0,4); prior: okno ±25 %, 3 zgodne ramki, 5 ramek bez sygnału zeruje stan | P2-03 |
| DIVIDE | pewność = 1 − min(1, mediana(r z 5 okresów)/0,15), gdzie r = \|T_k − (2T_{k−1} − T_{k−2})\|/T_{k−1}; bramka z histerezą 0,2; przerwa ≥ max(2,5 okresu, 40 ms) zaczyna nową nutę; tryb Constant: maksimum kroczące z 1,25 okresu | P2-02 |
| HARMONIC | obwiednia: max-hold z 1,5 okresu `range_lo`, potem atak 5 / release 60 ms; HP 15 Hz 2. rzędu | P2-05 |
| Drive | oversampling: przepustowe do 0,45·fs, zaporowe ≥ 100 dB od 0,55·fs; Auto Gain: tabela w Załączniku A | P2-06 |
| Para Hilberta | 4 sekcje/gałąź, bieguny w Załączniku B | P2-07 |
| Kompresor | detektory jak w §3; auto release 50 / 500 ms (wolna gałąź: atak 150 ms) | P2-09 |
| Limiter | lookahead 2 ms (Mix) / 5 ms (Print); THD przy wejściu sufit + 6 dB i release 100 ms: Mix ≤ −35 dB, Print ≤ −50 dB (granice T-LIM-02) | P2-09 |
| LFE | domyślnie Butterworth 24 dB/okt przy 120 Hz | P2-08 |
| `align_ms` | 0–40 ms + akcja Auto (opóźnienie grupowe toru sub przy f_sub1) | P2-10 |
| T-TRK-02 | granica opóźnienia glissanda: 50 ms (przy W = 83,5 ms) | P2-03 |

## 5. Sześć kontroli FAIL — co oznaczają

Wszystkie sprawdzają brzmienie SPEC; żadna nie wskazuje błędu w prototypie.

1. **T-ALIAS-01, Live (ADAA) ≤ −80 dB.** Hard przy drive 100 % daje −55 dB (25 % → −75,5; 50 % → −68,7 dB). Sam ADAA 1. rzędu nie wystarczy przy bardzo twardym przesterowaniu. → K-09, D2.
2. **T-ALIAS-01, Mix (ADAA + 2×) ≤ −90 dB.** Hard przy drive 100 % daje −77 dB (do 50 % spełnia: −90,7 dB). → K-09, D2.
3. **T-FILT-06 z β = 8** (wartość ze SPEC) przy 16 384 tapach: 0,194 dB zamiast ≤ 0,1 dB. Z β = 3 jest 0,061 dB. → K-12.
4. **T-CMP-02 z jednobiegunowym detektorem RMS i regułą tętnień:** −56,9 dB zamiast ≤ −60 dB. Detektor zdefiniowany w K-13 daje −124 dB.
5. **L_look Mix 25 ms** wyrównuje TRACK dopiero od 100 Hz, a nie od 55 Hz (A1). → K-05, D3.
6. **Zakres `align_ms` 0–20 ms** nie pokrywa naturalnego opóźnienia toru sub przy 40 Hz (25,9 ms). → K-15, D6.

## 6. Proponowane korekty SPEC i TEST_PLAN

Naniosę je dopiero po Twoim OK, osobnym commitem. Do bramki SPEC pozostaje bez zmian.

| # | Gdzie | Problem | Propozycja | Dowód |
|---|---|---|---|---|
| K-01 | SPEC §7.6 | Wzór Q Butterwortha w formie cos jest poprawny tylko dla rzędów parzystych (n = 3 daje 0,577 zamiast 1) | Q = 1/(2·sin((2i−1)π/(2n))) | P2-01 |
| K-02 | SPEC §6.2, TEST_PLAN T-DIV-05 | Reguła pewności „zmiana okresu > 15 %, ≥ 3 skoki w 1 s” zamyka bramkę na każdej stopie (spadek 16–20 % na okres) i migocze przy SNR 20 dB | Nieregularność względem trendu (mediana z 5 okresów) i histereza bramki (§4). T-DIV-05: akord −40 dB; szum losowy: otwarcie ≤ 3 % czasu, sub ≥ 12 dB poniżej szumu | P2-02 |
| K-03 | SPEC §6.2 (Constant) | Peak follower z atakiem 1 ms przestrzeliwuje poziom | Dzielić przez maksimum kroczące \|x\| z 1,25 okresu f_ref | P2-02 |
| K-04 | SPEC §6.3 | (a) surowa jasność NSDF dla szumu pasmowego to 0,45–0,56, więc nie może być pewnością; (b) przy oknie stałej długości pewność rośnie powoli po onsecie; (c) ocena kandydatów f/2 i 2f w oknie ±25 % wciąga kwinty i seksty; (d) okno < 2 okresów nie mieści f_min | (a) kalibracja (§4); (b) ramki świadome onsetu, czyli wycięcie ciszy i lag ≤ połowa reszty; (c) hold + 3 zgodne ramki, również na starcie; (d) W ≥ 2 okresy | P2-03 |
| K-05 | SPEC §4.4 | L_look 25/50 ms wyrównuje TRACK tylko od 100/45 Hz | 47/65 ms, czyli TRACK + kompresor + limiter (stopnie z wyprzedzeniem sumują się) | P2-03, P2-10 |
| K-06 | SPEC §6.4 | Follow „mnoży przez e_rms” bez normalizacji; Legato „obwiednia od początku” daje skok amplitudy do 0 | Follow: e_rms dzielone przez jego maksimum w uderzeniu, = 1 do tego maksimum, potem tylko maleje. Legato: atak od bieżącej amplitudy. Opcjonalnie: +0,5 próbki do czasu onsetu (stały bias follower'a). Uwaga w UI: ponowne uzbrojenie trwa ~15 ms·ln(szczyt/próg) + hold, czyli przy głośnej stopie ~67 ms, dłużej niż lockout 60 ms | P2-04 |
| K-07 | SPEC §6.5 | Obwiednia e_pk_s (peak follower 5/60 ms) siedzi 1,05 dB pod amplitudą i faluje 0,2–0,7 dB, przez co wagi są błędne o 13,6 dB | Max-hold z 1,5 okresu `range_lo`, potem 5/60 ms; HP 15 Hz 2. rzędu | P2-05 |
| K-08 | SPEC §7.3 | Auto Gain nie obejmuje α, DC ani Tone; wzór na bias jest addytywny (Tube przy balance −100 → 0,85); kryterium T-DRV-05 „skok ≤ 10⁻⁶” nie podaje kroku | Auto Gain na RMS(AC) mieszanki α (Tone — według D4). Tube = Soft z biasem 0,35. T-DRV-05: zmiana wyjścia proporcjonalna do kroku parametru (zmierzono 4–7·10⁻³ na 0,1 % drive) | P2-06 |
| K-09 | SPEC §4.5, TEST_PLAN T-ALIAS-01 | Tony 30/80/120 Hz przy 48 kHz są ślepe: fs/f0 jest całkowite, więc każdy alias ląduje na harmonicznej (naiwny shaper mierzy −255 dB). Cele −80/−90 dB są nieosiągalne dla Hard w Live/Mix | Mierzyć składowe nieharmoniczne dla tonów 31/81/121 Hz. Kryterium według D2: (a) po LP toru sub (ADAA: −129 dB) albo (b) pełnopasmowe progi Live −55 / Mix −75 / Print −90 dB | P2-06 |
| K-10 | SPEC §7.7 | Kryteria 1° i ±0,05 dB są sprzeczne (±0,05 dB wymaga ≤ 0,66°); θ = 0 to gałąź A, a nie tożsamość; znak w zapisie | 4 sekcje na gałąź (0,133°). Opis: θ = 0 ma fazę i opóźnienie gałęzi A. Zapis „cos θ·a − sin θ·b” z b *opóźnionym* o 90° to ta sama operacja co „cos θ·A + sin θ·B” z B *wyprzedzającym* | P2-07 |
| K-11 | SPEC §8.B | Auto-Align liczy na subie *przed* rotacją, a stopień rotacji działa na gałęzi A, więc wynik nie zgadza się z tym, co zrobi stopień (na SIG-18 różnica do ~240°) | Analizować sub *po* gałęzi A. Kanonicznie \|θ\| ≤ 90° i odwrócenie polaryzacji zamiast ±180°. Remisy rozstrzygać na korzyść tożsamości. Pewność = najlepszy vs mediana | P2-07 |
| K-12 | SPEC §7.6, T-FILT-06 | Cel \|H\|² podwaja nachylenie i daje −6 dB w narożniku; β ≈ 8 nie trzyma 0,1 dB przy 16 384 tapach; parzyste N daje opóźnienie połówkowe; limit 32 768 nie wystarcza przy 192 kHz | Cel \|H\|, β = 3, N = 2^k + 1 (nieparzyste); długości z §3; zakres Linear Phase według D5 | P2-08 |
| K-13 | SPEC §7.4 | „Reguła tętnień” nie usuwa zniekształceń w stanie ustalonym, a na onsecie bywa szkodliwa (Peak: −46 dB z regułą wobec −57 dB bez niej) | Zdefiniować detektory bez tętnień (§3); reguła według D7; auto release 50/500 ms; w T-CMP-03 dopisać, że okno detektora dodaje się do release | P2-09 |
| K-14 | SPEC §7.8 | Konstrukcja lookahead i granice THD nie są określone | Min-hold → release → wygładzanie Hanna, opóźnienie dokładnie round(lookahead·fs); granice T-LIM-02 z §4 | P2-09 |
| K-15 | SPEC §9.2, PARAMETERS `align_ms` | Naturalne opóźnienie toru sub to 36,6 / 25,9 / 19,9 / 15,1 ms przy 30 / 40 / 50 / 80 Hz | Zakres 0–40 ms + akcja Auto; ewentualnie HP pasma źródłowego 2. rzędu | P2-10 |
| K-16 | SPEC_IO_UI §4 | Domyślny filtr LFE nie jest określony liczbowo; parametry enkodera to założenie | Butterworth 24 dB/okt przy 120 Hz. Uwaga: ATSC A/52 podaje tylko rząd (8) i 120 Hz — 0,1 dB / 80 dB to założenie | P2-08 |
| K-17 | TEST_PLAN T-FILT-04 | Bessel 2. rzędu ma 2,02 % zmienności opóźnienia do fc/2 | Kryterium ≤ 1 % tylko dla rzędów 4 i 8; dla rzędu 2 ≤ 2,5 % | P2-01 |
| K-18 | TEST_PLAN T-TRIG-02/03/04 | Hilbert na zanikającym chirpie daje 3–8 % i 0,3–0,7 dB błędu; globalna druga różnica wykrywa „klik” na zagięciu ataku; test rozmiarów bloków nie da się zrobić offline | T-TRIG-03: połowy okresu między przejściami przez zero i próbki szczytowe. T-TRIG-04: druga różnica w oknie ±1 ms wokół retriggera. T-TRIG-02: rozmiary bloków w C++ | P2-04 |
| K-19 | TEST_PLAN T-TRK-02 | granica „[P2]” | ≤ 50 ms przy W = 83,5 ms (zmierzono 43 ms) | P2-03 |
| K-20 | TEST_PLAN T-HARM-02, T-LIM-02 | wartości „[P2]” | T-HARM-02 informacyjnie (72 % IMD dla dwóch tonów); T-LIM-02 z §4 | P2-05, P2-09 |

## 7. Ryzyka i ograniczenia

- **TRACK jest wolny na onsetach** (2 okresy) i nie śledzi szybkich spadków wysokości. Wymaga L_look ≥ 47/65 ms, a w Live sub wchodzi z opóźnieniem. Do stopy i 808 zostają DIVIDE i TRIGGER.
- **Aliasing Hard w Live/Mix** przy wysokim drive (−55/−77 dB pełnopasmowo). Po LP toru sub jest nieistotny (−129 dB), ale tryb LP przy `sub_lp_freq` ustawionym wysoko go odsłoni.
- **HARMONIC przy polifonii**: 72 % mocy to intermodulacja. Nadaje się do subów monofonicznych i ustalonych, co SPEC już zaznacza.
- **Linear Phase z HP 20 Hz** kosztuje 140–160 ms latencji.
- **Opóźnienie toru sub** (26 ms przy 40 Hz) przekracza obecny zakres `align_ms`.
- **Rzeczywiste latencje oversamplerów JUCE** nie są zmierzone; prototyp podaje górne ograniczenie.
- **Filtr enkodera LFE** to założenie (0,1 dB / 80 dB).
- **Prototyp jest offline**: brak przetwarzania blokowego, wygładzania parametrów, strażników NaN i wielokanałowości silników. To zakres testów C++ w Fazie 3.
- **Przegląd kodu wykonałem sam.** Niezależny recenzent jest opcją według D8.
- Drobne usterki odłożone (kod prototypu, nie ścieżka użytkownika): limiter rzuca błąd, gdy lookahead jest dłuższy niż bufor; `lfe_51` wymaga feedu LFE ≥ długości frontu; `harmonic` rzuca błąd przy pustym wejściu; kompresor i bramka przyjmują f_sub1 = 0 (dzielenie przez zero — wtyczka ogranicza do ≥ 20 Hz).

## 8. Odsłuch

- Pliki: `renders/` (24-bit, 48 kHz), opis w `renders/README.txt`.
- **A** = dry. **B** = dry + sub, dopasowane do A w głośności ważonej A — B *nie* jest głośniejsze, więc słuchaj masy, głębi, zwartości i wysokości, nie poziomu. **W** = sam sub.
- Dry jest opóźnione o opóźnienie grupowe pasma źródłowego, tak jak zrobi to `align_ms`. Do odsłuchu potrzebne są monitory albo słuchawki schodzące do 30 Hz.
- **UC1, stopa:** DIVIDE i TRIGGER. Szukaj masy 35–50 Hz bez rozmycia kliku i identycznych uderzeń.
- **UC2, 808 i bas:** DIVIDE i TRACK. Sub powinien iść za glissandem, bez skoków oktawowych. TRACK jest gładki, ale dogania szybkie zjazdy.
- **UC3, bus:** DIVIDE z kompresorem oraz wariant HYBRID (HARMONIC słychać na małych głośnikach).
- **UC4, impact:** TRIGGER z Drive. Ten sam boom trzy razy.
- **UC5, 5.1 LFE:** sub tylko w kanale LFE, zapisany 10 dB niżej. Odtwarzaj z konwencją +10 dB.
- **Twój materiał:**
  ```
  PYTHONPATH=proto .venv/bin/python -m experiments.render_file twoj_plik.wav --engine divide|track|trigger|harmonic
  ```
  Opcje: `--mode add|wet`, `--sub-db`, `--drive`, `--range-lo/--range-hi`, `--lookahead-ms`, `--lp`, `--ceiling`, `--align-ms` (domyślnie auto) i `--channels` (domyślnie para przednia; `3` to LFE w pliku 5.1).

## 9. Decyzje dla Ciebie (bramka Fazy 2)

| # | Pytanie | Rekomendacja | Alternatywa |
|---|---|---|---|
| D1 | Czy przyjmujesz korekty K-01…K-20? | Tak — naniosę je w SPEC, TEST_PLAN i PARAMETERS osobnym commitem | wybiórczo |
| D2 | Jak mierzyć aliasing Drive? | (a) aliasing w paśmie po LP toru sub ≤ −80/−90 dB (spełnione: −129 dB); wartości pełnopasmowe tylko informacyjnie | (b) pełnopasmowe progi Live −55 / Mix −75 dB albo 2× także w Live (~1,5 ms latencji, czyli koniec „Live = 0”) |
| D3 | L_look? | Mix 47 / Print 65 ms | zostawić 25/50 ms — TRACK wejdzie późno dla nut < 100/45 Hz |
| D4 | Czy Auto Gain ma kompensować Tone? | Tak — liczyć z Tone przy f_sub1 (inaczej Fold/Hard przy Tone 500 Hz tracą do 8,8 dB) | nie; opis w UI |
| D5 | Zakres Linear Phase w v1.0? | tylko LP (39 ms przy 48 kHz) | HP+LP (do ~160 ms) albo całość do v1.1 (SPK-6) |
| D6 | `align_ms`? | 0–40 ms + akcja Auto | zostawić 0–20 ms |
| D7 | Reguła tętnień kompresora? | zastąpić definicją detektorów bez tętnień, regułę usunąć | zostawić jako podłogę czasów |
| D8 | Niezależny przegląd kodu prototypu przed Fazą 3? | tak, jeśli zgodzisz się na subagenta-recenzenta (najmocniejszy model, cała gałąź) | wystarczy mój przegląd |
| D9 | Push na GitHub? | nadal nie (zgodnie z „Bez pusha na razie”); zapytam przed Fazą 3 | wypchnąć `dev` teraz |

## 10. Następne kroki

Po Twoim OK:
1. Naniosę przyjęte korekty w SPEC.
2. Napiszę plan Fazy 3 według kamieni milowych z planu Fazy 2:
   - M1: szkielet repo, CMake + JUCE 9.0.4, CI macOS/Windows, spike'i SPK-1/3/5;
   - M2: biblioteka `subgen_dsp` w C++ — port prototypu z danymi złotymi z `proto/`, testy Catch2;
   - M3: powłoka wtyczki;
   - M4: silniki;
   - M5: tor sub;
   - M6: instalatory i release (draft), spike'i SPK-2/4/6.

---

## Załącznik A — Auto Gain G_c [dB]

Wiersze: drive 5 / 10 / 25 / 50 / 75 / 100 %.

| Tryb | balance −100 | balance 0 | balance +100 |
|---|---|---|---|
| Soft | 1,72 / 0,72 / −1,31 / −2,46 / −2,79 / −2,92 | 0,87 / 0,08 / −1,50 / −2,48 / −2,80 / −2,92 | jak 0 |
| Tube | 3,41 / 2,01 / −0,92 / −2,43 / −2,79 / −2,92 | 1,28 / 0,39 / −1,41 / −2,47 / −2,80 / −2,92 | 0,87 / 0,08 / −1,50 / −2,48 / −2,80 / −2,92 |
| Tape | 3,06 / 2,14 / 0,18 / −1,49 / −2,27 / −2,65 | 2,29 / 1,61 / 0,02 / −1,51 / −2,27 / −2,65 | jak 0 |
| Fold | 0,41 / 0,04 / 0,76 / 0,01 / 0,01 / 0,00 | −1,46 / −1,25 / 1,38 / −0,31 / −0,49 / −0,13 | jak 0 |
| Hard | 0,66 / −0,44 / −2,00 / −2,65 / −2,87 / −2,95 | −0,85 / −1,34 / −2,14 / −2,67 / −2,87 / −2,95 | jak 0 |

## Załącznik B — para Hilberta (4 sekcje na gałąź, 12–600 Hz, błąd 0,1333°)

Sekcja: (p − s)/(p + s), cyfrowo (a + z⁻¹)/(1 + a·z⁻¹), a = (g − 1)/(g + 1), g = p/(2·fs).

- **Gałąź A** (bieguny p/2π): 4,047 · 30,298 · 119,010 · 514,848 Hz
- **Gałąź B** (wyprzedza A o 90°): 13,985 · 60,499 · 237,641 · 1778,969 Hz

## Załącznik C — rozstrzygnięcia podjęte w Twoim imieniu (pełna lista)

Każde z kosztem, jeśli okaże się błędne. Kolejność jak w dzienniku.

**Konfiguracja**

1. Bez osobnego worktree: repo jest lokalne, praca na `dev`. Koszt: brak.
2. Przegląd końcowy wykonałem sam, bo subagenta uruchamiam tylko na Twoją prośbę. Koszt: słabszy przegląd (D8).
3. Analiza filtrów (`freqz`, `group_delay_s`) obsługuje sekcje 1. rzędu (potrzebne dla LR2). Koszt: jedna gałąź kodu.
4. Wyniki eksperymentów mają nazwy `p2_NN`. Koszt: brak.
5. Cel FIR to |H| prototypu. Koszt: dłuższe jądra niż przy |H|² (→ K-12).

**Zadanie 2 — filtry**

6. Q Butterwortha w formie sin. Koszt: brak (→ K-01).

**Zadanie 3 — DIVIDE**

7. Pewność według reguły trendu z histerezą, zamiast reguły ze SPEC. Koszt: trochę więcej fałszywych otwarć na szumie (→ K-02).
8. T-DIV-05 rozdzielony na akord (−40 dB) i szum losowy (otwarcie ≤ 3 %, ≥ 12 dB pod szumem). Koszt: krótkie wybuchy suba na czystym szumie.
9. Testy odporności na szum biegną po filtrze pasma źródłowego. Koszt: brak.
10. Test polaryzacji w testach jednostkowych ma 40 uderzeń (w eksperymencie 100). Koszt: brak.

**Zadanie 4 — TRACK**

11. Pewność = skalibrowana jasność NSDF. Koszt: domyślny próg 0,5 odpowiada jasności 0,8.
12. Ramki świadome onsetu. Koszt: wysokość znana po 2 okresach, a nie po ~1,3.
13. Prior „trzymaj + 3 zgodne ramki”, także na starcie. Koszt: +4 ms do pierwszej estymaty.
14. YIN nie podaje estymaty, gdy minimum leży poza zakresem lagów. Koszt: brak.
15. Definicje metryk `evaluate()`: błąd oktawowy > 600 centów przy pewności ≥ 0,5; zamek = 3 ramki w 50 centach; lag dopasowany tam, gdzie wysokość się zmienia. Koszt: inne liczby przy innych definicjach.
16. PLL: pasmo = szerokość −3 dB. Decymacja całkowita D = ⌊fs/2000⌋. Pasmo badania 25–250 Hz. Kryterium bramki z DIVIDE nie przenoszę na TRACK. Koszt: brak.

**Zadanie 5 — TRIGGER**

17. Normalizacja Follow. Koszt: Follow kształtuje tylko ogon (→ K-06).
18. Atak Legato od bieżącej amplitudy. Koszt: zagięcie zbocza ≤ 0,008 pełnej skali.
19. `render()` zwraca sub1_level·Sub1 + sub2_level·Sub2. Koszt: brak.
20. T-TRIG-03 mierzony w dziedzinie czasu. Koszt: brak (→ K-18).
21. Metryka kliku liczona lokalnie (±1 ms); Legato sprawdzane dokładną formułą przenoszenia stanu. Koszt: brak.
22. Determinizm sprawdzam na uderzeniach 1–99 (uderzenie 0 nie ma ogona poprzednika); rozmiary bloków to test C++. Koszt: brak.

**Zadanie 6 — HARMONIC**

23. Obwiednia max-hold z nowym argumentem `f_lo`. Koszt: obwiednia spóźnia się na spadkach o 50 ms + release.
24. HP 15 Hz 2. rzędu; wspólne `sliding_peak` z DIVIDE. Koszt: brak.

**Zadanie 7 — Drive**

25. Aliasing mierzony jako składowe nieharmoniczne; wzorzec 16× z ADAA. Koszt: inne liczby niż „błąd względem wzorca”.
26. Tony testowe 31/81/121 Hz zamiast 30/80/120 Hz. Koszt: brak (→ K-09).
27. Auto Gain liczony na RMS(AC), bez Tone. Koszt: Tone zmienia poziom (→ D4).
28. Tube = Soft z biasem 0,35; oversampling na FIR Kaisera (110 dB). Koszt: latencja JUCE nie jest zmodelowana.
29. Test ciągłości α jako proporcjonalność. Koszt: brak (→ K-08).

**Zadanie 8 — faza**

30. Rząd pary Hilberta liczony na gałąź; rekomendacja 4. Koszt: 8 all-passów.
31. Równoważne zapisy znaku rotacji. Koszt: brak.
32. Auto-Align: 24 rotacje, postać kanoniczna, remisy na korzyść tożsamości. Koszt: brak.
33. Projekt biegunów metodą LS + SLSQP, bez prewarpingu. Koszt: brak.

**Zadanie 9 — LFE/FIR**

34. Kaiser β = 3. Koszt: większe listki w głębokim paśmie zaporowym (powyżej −70 dB bezwzględnie).
35. Cel |H|; tolerancja w narożniku 0,5 dB; nieparzyste N. Koszt: brak.

**Zadanie 10 — dynamika**

36. Detektory z hold. Koszt: poza f_sub1 o 0,22–0,30 dB więcej redukcji.
37. Teza briefu „bez reguły THD > −50 dB” jest fałszywa dla detektorów bez tętnień. Koszt: brak.
38. T-CMP-03 mierzony przy 200 Hz; auto release 50/500 ms. Koszt: inne stałe auto release.
39. Konstrukcja bramki i limitera; bramka przyjmuje `f_sub1`. Koszt: brak.

**Zadanie 11 — latencja i odsłuch**

40. Dodatkowe funkcje w `render.py`; `lfe_51` filtruje kanał LFE. Koszt: brak.
41. TRACK w Mix/Print bierze z wyprzedzeniem tylko wysokość i pewność, a amplitudę na bieżąco. Koszt: brak — inaczej sub zaczynałby przed dry.
42. L_look jako suma wyprzedzeń; L_os jako górne ograniczenie; rendery z opóźnionym dry. Koszt: L_total do potwierdzenia w JUCE.

**Przegląd końcowy**

43. Naprawione: `render_file` dodawał sub do surroundów i LFE pliku 5.1 — teraz para przednia, a `--channels` wybiera inne kanały.
44. Naprawione: `render_file` nie wyrównywał dry, inaczej niż rendery — teraz `--align-ms` (domyślnie auto).
