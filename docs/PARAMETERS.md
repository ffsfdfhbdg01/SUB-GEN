# SUB-GEN — PARAMETERS (rejestr parametrów v1.0)

Status: projekt Fazy 1 · Źródło prawdy dla ID, zakresów i wartości domyślnych · Powiązane: [SPEC.md](SPEC.md), [SPEC_IO_UI.md](SPEC_IO_UI.md), [TEST_PLAN.md](TEST_PLAN.md)
Liczba parametrów w v1.0: **141** (sprawdzane skryptem spójności; zob. SPEC_BUILD_RELEASE §11).

## 1. Konwencje
- **ID:** `lower_snake_case`, ASCII, ≤ 24 znaki. W JUCE: `juce::ParameterID { id, 1 }` (version hint 1 dla wszystkich parametrów v1.0; nowe parametry w v1.x dostają wyższy hint).
- **Typ:** `float` (NormalisableRange), `choice` (AudioParameterChoice), `bool` (AudioParameterBool).
- **Jednostki:** dB = 20·log10 amplitudy; „−60 = Off” oznacza, że wartość minimalna daje wzmocnienie dokładnie 0,0 (UI pokazuje „Off”).
- **Skala:** lin · log (po wartości fizycznej: Hz, ms, stosunek) · lin dB.
- **Wygładzanie:** G20 = multiplikatywne 20 ms (wzmocnienia) · F20 = multiplikatywne 20 ms w domenie log (częstotliwości) · L20 = liniowe 20 ms · X ms = crossfade · „–” = brak (zmiana na granicy bloku).
- **Auto:** A = automatyzowalny · N = nieautomatyzowalny (`withAutomatable(false)`; zmiana wpływa na latencję lub alokację).
- **Relacje wymuszane w `sanitise()`** (audio thread, bez alokacji): `range_hi` ≥ 1,2 × `range_lo` · `lfe_lp_freq` ≥ 2 × `lfe_hp_freq` · `filt_type` = Bessel ogranicza `sub_hp_slope` i `sub_lp_slope` do 48 dB/okt · `filt_phase` = LinearPhase działa tylko przy `quality` = Print. Parametr „nadrzędny” (pierwszy w relacji) wygrywa.
- **Nazwy w UI i nazwy hosta po angielsku**; krótkie nazwy (≤ 8 znaków) dla AAX/kontrolerów powstaną w Fazie 4.

## 2. Rejestr

### 2.1 Global
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `bypass` | Bypass | bool | off · on | off | X 10 ms | A | zwracany przez `getBypassParameter()`; zachowuje latencję |
| `quality` | Quality | choice | Live · Mix · Print | Mix | X 40 ms (sub wyciszany) | N | oversampling, lookahead, latencja; zmiana → `setLatencySamples` |
| `out_trim` | Output Trim | float | −24…+12 dB | 0 | lin dB · G20 | A | po sumowaniu dry+sub; 0 dB = bit-exact |

### 2.2 Source
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `src_gain` | Source Gain | float | −24…+24 dB | 0 | lin dB · G20 | A | tylko tor detekcji; nie zmienia dry |
| `src_mode` | Source Mode | choice | Sum · Max · Per-Channel · Auto | Auto | – | A | SPEC §5.2, §8.A |
| `feed_front` | Feed Front | float | −60…+6 dB (−60 = Off) | 0 | lin dB · G20 | A | klasa Front |
| `feed_center` | Feed Center | float | −60…+6 dB | 0 | lin dB · G20 | A | klasa Center |
| `feed_surround` | Feed Surround | float | −60…+6 dB | −60 | lin dB · G20 | A | klasa Surround |
| `feed_height` | Feed Height | float | −60…+6 dB | −60 | lin dB · G20 | A | klasa Height |
| `feed_lfe` | Feed LFE | float | −60…+6 dB | −60 | lin dB · G20 | A | wejściowy kanał LFE |
| `range_lo` | Range Low | float | 15…250 Hz | 30 | log · F20 | A | HP pasma źródłowego |
| `range_hi` | Range High | float | 30…500 Hz | 120 | log · F20 | A | LP pasma źródłowego |
| `range_slope` | Range Slope | choice | 12 · 24 · 48 dB/okt | 24 | – | A | Butterworth |
| `det_thresh` | Threshold | float | −90…0 dBFS | −55 | lin dB · L20 | A | próg aktywności źródła |
| `sc_use` | External Key | choice | Off · Detect · Trigger | Off | – | A | wymaga szyny sidechain |
| `sc_gain` | Key Gain | float | −24…+24 dB | 0 | lin dB · G20 | A | |

### 2.3 Engine (wspólne)
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `engine` | Engine | choice | Divide · Track · Trigger · Harmonic · Hybrid | Divide | X 30 ms | A | zmiana nie zmienia latencji |
| `sub1_level` | Sub −1 Level | float | −60…+12 dB (−60 = Off) | 0 | lin dB · G20 | A | dla HARMONIC = poziom harmonicznych |
| `sub2_level` | Sub −2 Level | float | −60…+12 dB | −60 | lin dB · G20 | A | nie dotyczy HARMONIC |
| `shape` | Shape | float | 0…100 % | 0 | lin · L20 | A | 0 = sinus, 100 = kształt natywny silnika |
| `base_mode` | Base Freq Mode | choice | Auto · Fixed | Auto | – | A | |
| `base_freq` | Base Frequency | float | 20…200 Hz | 45 | log · F20 | A | = częstotliwość Sub −1 (DIVIDE, TRACK, TRIGGER); = podstawowa źródła (HARMONIC) |
| `follow` | Envelope | choice | Follow · Constant | Follow | X 20 ms | A | |
| `env_attack` | Env Attack | float | 0,1…50 ms | 3 | log · – | A | |
| `env_release` | Env Release | float | 5…1000 ms | 80 | log · – | A | |
| `dynamics` | Dynamics | float | −100…+100 % | 0 | lin · L20 | A | makro: − transjent, + sustain |
| `conf_gate` | Confidence Gate | float | 0…100 % | 50 | lin · L20 | A | DIVIDE, TRACK |

### 2.4 TRACK
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `trk_speed` | Tracking Speed | float | 0…100 % | 50 | lin · L20 | A | kompromis gładkość ↔ zwinność |
| `trk_glide` | Glide | float | 0…200 ms | 0 | lin · – | A | |
| `phase_gen` | Phase Mode | choice | Locked · Free · Reset | Locked | – | A | dotyczy TRACK; w DIVIDE polaryzacja wg SPEC §6.2 |

### 2.5 TRIGGER
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `trig_thresh` | Trig Threshold | float | −60…0 dBFS | −30 | lin dB · – | A | |
| `trig_hold` | Trig Hold | float | 0…200 ms | 20 | lin · – | A | |
| `trig_lockout` | Trig Lockout | float | 5…500 ms | 60 | log · – | A | |
| `trig_f_start` | Pitch Start | float | 30…200 Hz | 80 | log · F20 | A | f_end = `base_freq` (Fixed) lub f0/2 (Auto) |
| `trig_pitch_time` | Pitch Time | float | 1…200 ms | 30 | log · – | A | stała czasowa τ_p |
| `trig_attack` | Trig Attack | float | 0…20 ms | 2 | lin · – | A | |
| `trig_decay` | Trig Decay (T60) | float | 20…3000 ms | 350 | log · – | A | |
| `trig_phase` | Start Phase | float | 0…360 ° | 0 | lin · – | A | |
| `trig_vel` | Velocity | float | 0…100 % | 60 | lin · L20 | A | |
| `trig_retrigger` | Retrigger | choice | Restart · Legato | Restart | – | A | |

### 2.6 HARMONIC i HYBRID
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `harm_amount` | Harmonics | float | 0…100 % | 50 | lin · L20 | A | |
| `harm_balance` | Even/Odd | float | −100…+100 | 0 | lin · L20 | A | − parzyste, + nieparzyste |
| `harm_max` | Max Harmonic | choice | 3rd · 4th · 5th | 4th | – | A | |
| `hyb_divide` | Hybrid Divide | float | 0…100 % | 100 | lin · L20 | A | |
| `hyb_harmonic` | Hybrid Harmonic | float | 0…100 % | 35 | lin · L20 | A | |

### 2.7 Tor sub — Boost, Gate
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `boost_gain` | Boost | float | 0…+18 dB | 0 | lin dB · G20 | A | 0 dB = tożsamość |
| `boost_type` | Boost Type | choice | Peak · Low Shelf | Peak | X 20 ms | A | |
| `boost_freq` | Boost Freq | float | 20…200 Hz | 55 | log · F20 | A | |
| `boost_follow` | Boost Follow | bool | off · on | on | – | A | środek podąża za częstotliwością Sub −1 |
| `boost_q` | Boost Q | float | 0,3…8 | 1,0 | log · F20 | A | |
| `gate_on` | Gate | bool | off · on | off | X 10 ms | A | |
| `gate_thresh` | Gate Threshold | float | −80…0 dBFS | −50 | lin dB · L20 | A | |
| `gate_range` | Gate Range | float | −60…0 dB | −40 | lin dB · L20 | A | głębokość tłumienia |
| `gate_hold` | Gate Hold | float | 0…500 ms | 40 | lin · – | A | |
| `gate_release` | Gate Release | float | 5…1000 ms | 120 | log · – | A | |

### 2.8 Tor sub — Drive/Saturate
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `drive` | Drive | float | 0…100 % | 0 | lin · L20 | A | pokrętło główne; pre-gain 0…+30 dB; 0 = tożsamość |
| `drive_mode` | Drive Mode | choice | Soft · Tube · Tape · Fold · Hard | Soft | X 20 ms | A | SPEC §7.3 |
| `drive_balance` | Even/Odd Bias | float | −100…+100 | 0 | lin · L20 | A | − więcej parzystych, + więcej nieparzystych |
| `drive_tone` | Drive Tone | float | 60…2000 Hz | 500 | log · F20 | A | LP po saturacji |
| `drive_auto` | Auto Gain | bool | off · on | on | – | A | kompensacja poziomu RMS |
| `drive_pos` | Drive Position | choice | Pre-Comp · Post-Comp | Pre-Comp | X 20 ms | A | |

### 2.9 Tor sub — Compressor
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `comp_on` | Comp | bool | off · on | off | X 10 ms | A | |
| `comp_thresh` | Comp Threshold | float | −40…0 dBFS | −18 | lin dB · L20 | A | |
| `comp_ratio` | Comp Ratio | float | 1…20 :1 | 3 | log · L20 | A | 20 = limiting |
| `comp_knee` | Comp Knee | float | 0…12 dB | 6 | lin · L20 | A | |
| `comp_attack` | Comp Attack | float | 0,1…100 ms | 10 | log · – | A | podłoga wg reguły tętnień (SPEC §7.4) |
| `comp_release` | Comp Release | float | 10…1000 ms | 150 | log · – | A | |
| `comp_auto_rel` | Auto Release | bool | off · on | on | – | A | |
| `comp_detect` | Comp Detector | choice | Peak · RMS | RMS | – | A | |
| `comp_makeup` | Comp Makeup | float | −12…+24 dB | 0 | lin dB · G20 | A | |
| `comp_auto_mu` | Auto Makeup | bool | off · on | off | – | A | |

### 2.10 Tor sub — Parametric EQ (5 pasm; wzorzec `eqN_*`, N = 1…5)
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `eqN_on` | Band N On | bool | off · on | off | X 10 ms | A | 5 parametrów |
| `eqN_type` | Band N Type | choice | Bell · Low Shelf · High Shelf | wg tabeli poniżej | X 20 ms | A | |
| `eqN_freq` | Band N Freq | float | 15…1000 Hz | wg tabeli poniżej | log · F20 | A | |
| `eqN_gain` | Band N Gain | float | −18…+18 dB | 0 | lin dB · G20 | A | |
| `eqN_q` | Band N Q | float | 0,3…10 | wg tabeli poniżej | log · F20 | A | dla półek: nachylenie |

Wartości domyślne pasm: 1 — Low Shelf, 40 Hz, Q 0,71 · 2 — Bell, 60 Hz, Q 1,0 · 3 — Bell, 100 Hz, Q 1,0 · 4 — Bell, 160 Hz, Q 1,0 · 5 — High Shelf, 300 Hz, Q 0,71.
Pełna lista ID: `eq1_on` `eq1_type` `eq1_freq` `eq1_gain` `eq1_q` · `eq2_on` `eq2_type` `eq2_freq` `eq2_gain` `eq2_q` · `eq3_on` `eq3_type` `eq3_freq` `eq3_gain` `eq3_q` · `eq4_on` `eq4_type` `eq4_freq` `eq4_gain` `eq4_q` · `eq5_on` `eq5_type` `eq5_freq` `eq5_gain` `eq5_q`.

### 2.11 Tor sub — Filtry, Limiter, Faza, Poziom
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `sub_hp_freq` | Subsonic HP | float | 10…60 Hz | 22 | log · F20 | A | w trybie LFE zastępowany przez `lfe_hp_freq` |
| `sub_hp_slope` | HP Slope | choice | 12 · 24 · 48 · 96 dB/okt | 24 | – | A | |
| `sub_lp_freq` | Sub LP | float | 30…500 Hz | 140 | log · F20 | A | w trybie LFE zastępowany przez `lfe_lp_freq` |
| `sub_lp_slope` | LP Slope | choice | 12 · 24 · 48 · 96 dB/okt | 24 | – | A | |
| `filt_type` | Filter Type | choice | Butterworth · Linkwitz-Riley · Bessel | Butterworth | X 20 ms | A | |
| `filt_phase` | Filter Phase | choice | Min Phase · Linear Phase | Min Phase | X 40 ms | N | Linear tylko w Print; zmienia latencję |
| `lim_on` | Limiter | bool | off · on | on | X 10 ms | A | tylko tor sub |
| `lim_ceiling` | Limiter Ceiling | float | −12…0 dBFS | −1,0 | lin dB · L20 | A | |
| `lim_release` | Limiter Release | float | 10…500 ms | 100 | log · – | A | |
| `lim_tp` | True Peak | bool | off · on | off | – | A | detekcja 4× (Print) |
| `sub_pol` | Polarity | bool | 0° · 180° | 0° | X 5 ms | A | |
| `sub_rot` | Phase Rotation | float | −180…+180 ° | 0 | lin · L20 | A | SPEC §7.7 |
| `align_obj` | Align Objective | choice | Energy · Headroom | Energy | – | A | cel Auto-Align (SPEC §8.B) |
| `align_lock` | Align Lock | bool | off · on | off | – | A | blokuje zmiany z Auto-Align |
| `sub_level` | Sub Level | float | −60…+18 dB (−60 = Off) | −6 | lin dB · G20 | A | poziom na początku toru sub (SPEC §7.1); poziom końcowy ustawiają `route_*` i `lfe_trim` |

### 2.12 Sumowanie
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `sum_mode` | Sum Mode | choice | Add · Mix | Add | X 20 ms | A | |
| `dry_level` | Dry Level | float | −60…+6 dB (−60 = Off) | 0 | lin dB · G20 | A | tryb Add |
| `mix` | Mix | float | 0…100 % | 50 | lin · L20 | A | tryb Mix |
| `mix_law` | Mix Law | choice | Equal Power · Linear | Equal Power | – | A | |
| `wet_only` | Wet Only | bool | off · on | off | X 10 ms | A | dry = 0, latencja bez zmian |
| `align_ms` | Align | float | 0…20 ms | 0 | lin · – | N | dodatkowe opóźnienie dry; zmienia latencję |

### 2.13 Routing
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `out_mode` | Output Mode | choice | Auto · Mono · Stereo · Surround · LFE | Auto | X 30 ms | A | Auto: LFE przy layoucie z LFE, inaczej Mono |
| `route_front` | Route Front | float | −60…+6 dB | 0 | lin dB · G20 | A | Mono, Stereo, Surround |
| `route_center` | Route Center | float | −60…+6 dB | 0 | lin dB · G20 | A | Mono, Surround |
| `route_surround` | Route Surround | float | −60…+6 dB | −6 | lin dB · G20 | A | tylko Surround |
| `route_height` | Route Height | float | −60…+6 dB | −60 | lin dB · G20 | A | tylko Surround |
| `route_lfe` | Route LFE | float | −60…+6 dB | −60 | lin dB · G20 | A | tylko Surround (w LFE steruje `lfe_trim`) |
| `route_front_dly` | Front Delay | float | 0…30 ms | 0 | lin · – | A | |
| `route_center_dly` | Center Delay | float | 0…30 ms | 0 | lin · – | A | |
| `route_surround_dly` | Surround Delay | float | 0…30 ms | 0 | lin · – | A | |
| `route_height_dly` | Height Delay | float | 0…30 ms | 0 | lin · – | A | |
| `route_lfe_dly` | LFE Delay | float | 0…30 ms | 0 | lin · – | A | także w trybie LFE |
| `route_front_pol` | Front Polarity | bool | 0° · 180° | 0° | X 5 ms | A | |
| `route_center_pol` | Center Polarity | bool | 0° · 180° | 0° | X 5 ms | A | |
| `route_surround_pol` | Surround Polarity | bool | 0° · 180° | 0° | X 5 ms | A | |
| `route_height_pol` | Height Polarity | bool | 0° · 180° | 0° | X 5 ms | A | |
| `route_lfe_pol` | LFE Polarity | bool | 0° · 180° | 0° | X 5 ms | A | także w trybie LFE |
| `sub_width` | Sub Width | float | 0…100 % | 0 | lin · L20 | A | 0 = mono; wymaga dwóch strumieni sub |

### 2.14 LFE
| ID | Nazwa | Typ | Zakres / wartości | Domyślna | Skala · wygładzanie | Auto | Uwagi |
|---|---|---|---|---|---|---|---|
| `lfe_hp_freq` | LFE HP | float | 15…40 Hz | 20 | log · F20 | A | |
| `lfe_lp_freq` | LFE LP | float | 60…150 Hz | 120 | log · F20 | A | |
| `lfe_lp_slope` | LFE LP Slope | choice | 12 · 24 · 48 · 96 dB/okt | 24 | – | A | |
| `lfe_conv` | LFE Convention | choice | +10 dB · 0 dB | +10 dB | – | A | tylko metering i ostrzeżenia |
| `lfe_ceiling` | LFE Ceiling | float | −12…0 dBFS | −1,0 | lin dB · L20 | A | |
| `lfe_bleed` | LFE Bleed | float | −60…−6 dB (−60 = Off) | −60 | lin dB · G20 | A | wysyłka do kanałów głównych |
| `lfe_trim` | LFE Trim | float | −24…+12 dB | 0 | lin dB · G20 | A | |

## 3. Wartości inicjalne („Init”)
Domyślne z tabel powyżej tworzą preset „Init”: silnik Divide, Sub −1 0 dB, Sub −2 Off, Sub Level −6 dB, Drive 0%, tryb Add, Dry 0 dB, Output Mode Auto, jakość Mix. Ustawienia Init mają zachowywać dry bit-exact (po kompensacji latencji) przy `sub_level` = Off.

## 4. Identyfikatory zarezerwowane (v1.1+)
`note_lock` · `note_scale` · `bass_mono_freq` · `ms_mode` · `eq_phase` · `band2_*` (druga ścieżka źródłowa) · `midi_*` · `spec_*` (silnik SPECTRAL) · `route_matrix_*` (macierz per kanał). ID zarezerwowane nie mogą być użyte do innych celów.

## 5. Zasady zmian po wydaniu
1. ID nigdy nie są zmieniane ani ponownie używane; parametr wycofywany zostaje w rejestrze jako „deprecated” i jest ignorowany przy wczytywaniu stanu.
2. Zakres może zostać tylko rozszerzony; znaczenie wartości choice nie może się zmienić (nowe wartości dopisywane na końcu).
3. Domyślne wartości nowych parametrów nie mogą zmieniać brzmienia wczytywanych starych stanów (migracja ustawia wartość neutralną).
4. Każda zmiana rejestru wymaga wpisu w CHANGELOG i aktualizacji testu migracji stanu (TEST_PLAN T-STATE-02).

## 6. Stan poza parametrami (nie są parametrami hosta)
Skala GUI, aktywna strona UI, sloty A/B, nazwa i kategoria presetu, wynik Auto-Align (polaryzacja, rotacja, pewność), zakres osi analizatora, historia undo. Zapisywane w stanie wtyczki (SPEC_IO_UI §9).
