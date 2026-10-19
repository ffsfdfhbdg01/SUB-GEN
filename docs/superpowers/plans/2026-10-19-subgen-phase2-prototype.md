# SUB-GEN — plan implementacji Fazy 2 (prototyp DSP w Pythonie)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zbudować offline'owy prototyp DSP SUB-GEN (filtry, pięć silników, Drive, narzędzia fazy, filtry LFE, dynamika), który mierzy każdy punkt P2-01…P2-11 względem progów SPEC i dostarcza wartości oraz korekty specyfikacji potrzebne wersji C++.

**Architecture:** Mała biblioteka `proto/subgen_proto` (jeden moduł na blok DSP, podwójna precyzja, deterministyczna) z testami `unittest` per moduł oraz jeden skrypt eksperymentu na punkt P2, który uruchamia moduł na sygnałach testowych z TEST_PLAN, wypisuje PASS/FAIL/INFO względem progu SPEC i zapisuje JSON + wykresy. Bloki nieliniowe są liczone zdarzeniowo lub wektorowo; pętle po próbkach tylko tam, gdzie konieczne, i tylko na sygnałach ≤ 5 s.

**Tech Stack:** Python 3.13 (venv `/Volumes/Sam-dysk/SUB-GEN/.venv`), numpy, scipy, matplotlib, soundfile, stdlib `unittest`.

**Spec:** `docs/SPEC.md` (§6–§9), `docs/SPEC_IO_UI.md`, `docs/SPEC_BUILD_RELEASE.md` (§8 = lista pomiarów Fazy 2), `docs/PARAMETERS.md`, `docs/TEST_PLAN.md`.

**Polecenia** (z katalogu głównego repo): wszystkie testy `PYTHONPATH=proto .venv/bin/python -m unittest discover -s proto/tests -t proto -v` · jeden moduł `PYTHONPATH=proto .venv/bin/python -m unittest tests.test_svf -v` · eksperyment `PYTHONPATH=proto .venv/bin/python -m experiments.p2_01_filters`.

## Global Constraints
- Podwójna precyzja wszędzie; deterministyczność: każdy generator losowy przyjmuje stałe `seed`.
- Częstotliwości próbkowania: 44100, 48000, 88200, 96000, 176400, 192000 Hz. Testy filtrów obejmują wszystkie sześć; testy silników dodatkowo 44100 i 96000.
- Tylko numpy, scipy, matplotlib, soundfile + stdlib; wersje przypięte w `proto/requirements.txt`; venv i `renders/` poza gitem.
- Każdy plik źródłowy zaczyna się od `# SPDX-License-Identifier: AGPL-3.0-or-later`.
- Brak sieci w testach; brak `git push`; commity lokalne. Dysk: venv < 1 GiB, `renders/` < 500 MB (Sam-dysk miał 21 GiB wolnego).
- Progi z SPEC/TEST_PLAN (verbatim): błąd amplitudy Butterworth/LR ≤ 0,01 dB; Cytomic vs RBJ ≤ 0,05 dB; błąd częstotliwości Sub −1/−2 ≤ 0,1%; błąd f0 ≤ 0,5%; aliasing ≤ −90 dB (Mix, Print), ≤ −80 dB (Live); Auto Gain RMS ±0,5 dB; para Hilberta 90° ± 1° w 12–600 Hz; rotacja ± 1°, amplituda ± 0,05 dB; uderzenia TRIGGER identyczne ≤ −120 dB; wagi harmonicznych ± 0,5 dB; Auto-Align ± 15°; THD kompresora ≤ −60 dB (z regułą tętnień).
- Stałe z SPEC: próg uzbrajania DIVIDE `θ = max(0,05·e_pk, 10^(−90/20))` (Live 0,03); wygładzanie LP Butterworth 4. rzędu przy `0,87·f_est`; poziom Constant −12 dBFS; próg pewności `0,2 + 0,6·conf_gate/100`; Drive: odniesienie −12 dBFS, pre-gain `30 dB·drive/100`, `α = min(1, drive/5)`; MPM: próg 0,9, ok. 2 kHz po decymacji, hop 2 ms; pasmo Hilberta 12–600 Hz.
- **Niejednoznaczność SPEC §7.6:** docelowa amplituda FIR liniowofazowego to `|H_prototypu|`, nie `|H|²` (zapis „|H|²” w SPEC to błąd/skrót). Rozstrzygnąć w Zadaniu 9 i zapisać jako korektę SPEC.

## Review Focus
1. Częstotliwości inne niż 48 kHz: 44,1 kHz daje niecałkowitą decymację do ok. 2 kHz, 192 kHz stresuje filtry niskich częstotliwości — każde zadanie silnikowe uruchamia test dokładności przy 44100 i 96000.
2. Bas niebędący czystym sinusem: obcięty/prostokątny, ze składową stałą, nuta zaczynająca się w szczycie przebiegu — DIVIDE ma nadal dzielić częstotliwość i nigdy nie zwracać NaN.
3. Wejścia bardzo ciche (−90 dBFS) i pełnoskalowe: podłogi (`θ`, `E_FLOOR`) zachowują ciszę ciszą, a głośne wejście daje skończone wyjście.
4. Nuty z mocną 2. harmoniczną i glissanda przez oktawę (808): TRACK nie może zgłaszać błędów oktawowych.
5. Uderzenia gęściej niż `trig_lockout` oraz nakładające się retrigery: brak podwójnego wyzwolenia, trzasku i NaN.

---

### Task 1: Środowisko, narzędzia pomiarowe, sygnały testowe, rejestrator wyników

**Files:**
- Create: `.gitignore`, `proto/requirements.txt`, `proto/subgen_proto/__init__.py`, `proto/subgen_proto/util.py`, `proto/subgen_proto/signals.py`, `proto/experiments/__init__.py`, `proto/experiments/_common.py`
- Test: `proto/tests/__init__.py`, `proto/tests/test_util.py`, `proto/tests/test_signals.py`, `proto/tests/test_common.py`

**Interfaces:**
- Produces `util`: `db(x)->ndarray` · `undb(d)->ndarray` · `rms(x)->float` · `null_db(ref,test)->float` (20·log10(rms(ref−test)/rms(ref)), minimum −400) · `peak_freq(x,fs,f_lo,f_hi,pad=16)->(f_hz,amp)` (okno Blackman-Harris, interpolacja paraboliczna logarytmu) · `zero_crossings(x,rising=True)->ndarray` (ułamkowe indeksy) · `zc_freq(x,fs)->float` · `harmonic_amps(x,fs,f0,n=10)->ndarray` · `thd_db(x,fs,f0,n=10)->float` · `band_db(x,fs,f1,f2)->float` (RMS pasma w dBFS) · `frac_delay(x,d)->ndarray` (FFT, dla sygnałów okresowych) · `a_weight_sos(fs)->ndarray` · `loudness_a_db(x,fs)->float`.
- Produces `signals` (wszystkie `float64`, mono, `level_db` = szczyt dBFS): `sine(f,fs,dur,level_db=-6.0,phase=0.0)` · `log_sweep(f0,f1,fs,dur,level_db=-12.0)` · `kick(fs,n_hits=1,spacing=0.5,level_db=-6.0,levels_db=None,f_start=150.0,f_end=45.0,tau_pitch=0.030,t60=0.300,phase=0.0)` (SIG-03) · `bass_808(fs,f_end=45.0,glide_from=90.0,glide_ms=80.0,harm2_db=-8.0,sustain=0.6,level_db=-6.0)` (SIG-04) · `bass_pluck(fs,f0,dur=0.8,level_db=-6.0,seed=0)` (SIG-05) · `chord(fs,freqs=(55,69,82),dur=2.0,level_db=-12.0)` (SIG-06) · `pink_noise(fs,dur,f_lo=20.0,f_hi=2000.0,level_db=-24.0,seed=1)` (SIG-07/16) · `antiphase_pair(fs,f=60.0,dur=2.0,level_db=-12.0)->(L,R)` (SIG-08) · `impulse(n,amp=0.5)` · `silence(fs,dur)` · `dc(fs,dur,value=0.5)` · `square(f,fs,dur,level_db=0.0)` · `nan_inf(n)` · `denormal_ramp(n)` · `two_tone(f1,f2,fs,dur,level_db=-12.0)` · `kick_plus_808(fs)` (SIG-18) · `sine_in_noise(f,fs,dur,snr_db,level_db=-12.0,seed=2)` (SIG-19).
- Produces `experiments._common`: `Recorder(name)` z metodami `check(label,value,op,threshold,unit='')->bool` (op ∈ `<=,>=,<,>,==` z tolerancją), `info(label,value)`, `save()->Path` (`proto/out/<name>.json`), `fig_path(suffix)->Path` (`docs/img/<name>_<suffix>.png`).

- [ ] **Step 1: Środowisko.** `python3 -m venv .venv && .venv/bin/pip install numpy scipy matplotlib soundfile`, zapisać `pip freeze` do `proto/requirements.txt`, utworzyć `.gitignore` (`.venv/`, `__pycache__/`, `proto/out/`, `renders/`). Weryfikacja: `.venv/bin/python -c "import numpy,scipy,matplotlib,soundfile;print(soundfile.__libsndfile_version__)"` wypisuje wersję; `du -sh .venv` < 1 GiB.
- [ ] **Step 2: Testy (czerwone).** `test_peak_freq_accuracy` (`sine(40.3,48000,2.0)` → błąd < 0,01 Hz, amplituda w 1% od 0,501) · `test_zc_freq_sine` (`sine(31.7,44100,1.0)` → błąd < 1e-3 Hz) · `test_null_db_identical` (`null_db(x,x.copy()) <= -300`) · `test_thd_db_known` (sinus + 1% 3. harmonicznej → −40 dB ± 0,2) · `test_frac_delay_roundtrip` (opóźnienie 0,5 próbki sinusa okresowego zgodne z analitycznym ≤ 1e-9) · `test_a_weight_gain` (1 kHz: 0 ± 0,05 dB; 100 Hz: −19,1 ± 0,3; 50 Hz: −30,2 ± 0,3) · `test_kick_deterministic_and_peak` (dwa wywołania identyczne; szczyt ≤ 10^(−6/20)) · `test_pink_noise_band` (energia poza 20–2000 Hz ≤ −40 dB względem w paśmie) · `test_sine_in_noise_snr` (zmierzone SNR ± 0,2 dB) · `test_denormal_ramp_subnormal` (minimum niezerowe < 2.2e-308) · `test_recorder_check_ops` (`check` zwraca poprawne bool dla każdego `op` i zapisuje do JSON).
- [ ] **Step 3: Uruchomić** `PYTHONPATH=proto .venv/bin/python -m unittest tests.test_util tests.test_signals tests.test_common` → oczekiwane `ModuleNotFoundError`.
- [ ] **Step 4: Implementacja** sygnatur z Interfaces. `pink_noise`: kształtowanie widma FFT 1/√f w paśmie. `peak_freq`: okno Blackman-Harris, zero-padding `pad`×, parabola na log-amplitudzie trzech binów wokół maksimum.
- [ ] **Step 5: Testy zielone** (to samo polecenie) i commit: `git add -A && git commit -m "feat(proto): environment, measurement utilities, test signals"`.

### Task 2: Filtry TPT, Butterworth/LR/Bessel, kształty Cytomic (P2-01)

**Files:**
- Create: `proto/subgen_proto/svf.py`, `proto/experiments/p2_01_filters.py`
- Test: `proto/tests/test_svf.py`

**Interfaces:**
- Consumes: `util.null_db`, `experiments._common.Recorder`.
- Produces `svf`: `Section(g,k,m0,m1,m2)` (frozen dataclass; `y = m0·v0 + m1·v1 + m2·v2`) · `FirstOrder(g,kind)` (`kind ∈ {'low','high'}`) · `design_butter(order,fc,fs,kind)->list` (sekcje: `Q_i = 1/(2cos((2i−1)π/(2n)))`, `k = 1/Q`, **`g = tan(π·fc/fs)`**) · `design_lr(order,fc,fs,kind)->list` (dwie kaskadowane Butterworth(order/2); order ∈ {2,4,8,16}) · `design_bessel(order,fc,fs,kind)->list` (order ∈ {2,4,8}; bieguny `scipy.signal.bessel(order,1,analog=True,output='zpk',norm='mag')`; dla sekcji o `ω0_i`, `Q_i = ω0_i/(2|Re p|)`: **LP `g = ω0_i·tan(π fc/fs)`, HP `g = tan(π fc/fs)/ω0_i`**, tak aby wynik był identyczny z pojedynczym prewarpem w SciPy) · `cytomic(kind,fc,q,gain_db,fs)->Section` (`kind ∈ {'bell','lowshelf','highshelf'}`, `A = 10^(gain_db/40)`; wzory z SPEC §7.2) · `process(sections,x)->ndarray` · `process_varying(x,g,k,mode='low')->ndarray` (`g` na próbkę) · `freqz(sections,freqs_hz,fs)->ndarray` (zespolona odpowiedź analityczna: `s' = j·tan(ω/2)/g`, `H = (m0·D + m1·s' + m2)/D`, `D = s'² + k·s' + 1`) · `group_delay_s(sections,freqs_hz,fs)->ndarray`.

- [ ] **Step 1: Testy (czerwone).** `test_butter_matches_scipy` (order ∈ {2,4,8,16}, fc ∈ {15,22,60,120,500}, wszystkie sześć fs, low/high: max |dB(`freqz`) − dB(`sosfreqz(butter(...,output='sos'))`)| < 0,01 dB w 5 Hz…0,45·fs) · `test_time_domain_matches_analytic` (odpowiedź impulsowa `process(design_butter(4,60,48000,'low'), δ)` o długości 2^16: |FFT| vs `freqz` względny błąd < 1e-6 w 5–2000 Hz) · `test_lr4_lp_hp_sum_allpass` (|LP+HP| = 1 ± 0,01 dB dla LR4 przy 120 Hz) · `test_bessel_matches_scipy` (order 2,4,8, fc ∈ {22,120}: < 0,01 dB vs `bessel(...,norm='mag')`; zmienność opóźnienia grupowego do fc/2 ≤ 1%) · `test_cytomic_bell_matches_rbj` (gain ∈ {−18,−6,6,18}, Q ∈ {0,3;1;10}, f ∈ {40,160,1000}: < 0,05 dB) · `test_cytomic_shelf_matches_rbj` (Q = 0,71) · `test_cytomic_zero_gain_identity` (`process` = `x` ≤ 1e-12) · `test_modulated_cutoff_stable` (g przemiatane 15…500 Hz z częstością 10 Hz: wyjście skończone i |y| ≤ 4·max|x|).
- [ ] **Step 2: Uruchomić** `tests.test_svf` → oczekiwane błędy importu.
- [ ] **Step 3: Implementacja** wg Interfaces. Stan TPT wg Cytomic: `v3 = v0 − ic2eq; v1 = a1·ic1eq + a2·v3; v2 = ic2eq + a2·ic1eq + a3·v3; ic1eq = 2v1 − ic1eq; ic2eq = 2v2 − ic2eq`, `a1 = 1/(1+g(g+k))`, `a2 = g·a1`, `a3 = g·a2`; HP: `m0=1,m1=−k,m2=−1`; LP: `m2=1`. Jeśli porównanie z RBJ dla półek się nie zgadza, zapisać różnicę definicji Q jako korektę SPEC §7.2 zamiast naginać test.
- [ ] **Step 4: Testy zielone**; `experiments/p2_01_filters.py`: tabela maksymalnych błędów (BW/LR/Bessel × rząd × fc × fs), stabilność przy fs 192 kHz i 15 Hz, wykres odpowiedzi; `check` względem progów 0,01 / 0,05 dB. Uruchomić: `PYTHONPATH=proto .venv/bin/python -m experiments.p2_01_filters`.
- [ ] **Step 5: Commit** `feat(proto): TPT filters and Cytomic shapes (P2-01)`.

### Task 3: Obwiednie i silnik DIVIDE (P2-02)

**Files:**
- Create: `proto/subgen_proto/envelope.py`, `proto/subgen_proto/divide.py`, `proto/experiments/p2_02_divide.py`
- Test: `proto/tests/test_divide.py`

**Interfaces:**
- Consumes: `svf.design_butter`, `svf.process_varying`, `util`, `signals`.
- Produces: `envelope.peak_follower(x,fs,attack_ms,release_ms)` · `envelope.rms_follower(x,fs,tau_ms)` · `DivideResult(y1,y2,f_est,conf,rising,falling)` · `divide.divide(x,fs,*,live=False)->DivideResult` (SPEC §6.2: uzbrajanie progiem `θ`, przejścia narastające flipują `s1`, opadające w cyklach z `s1=+1` flipują `s2`; zdarzeniowo, ułamkowe czasy przejść) · `divide.smooth_sub(res,fs,which)->ndarray` · `divide.shape_blend(y_native,y_smooth,shape_pct)` · `divide.apply_confidence_gate(y,conf,conf_gate_pct,fs)` · `divide.phase_halving(x,fs)->ndarray` (wariant `A·cos(φ/2 + c0)` z `scipy.signal.hilbert`, `c0` tak, by pierwszy płat był współfazowy).

- [ ] **Step 1: Testy (czerwone).** `test_sine_division_accuracy` (f ∈ {30,40,55,80,120,200}, −18 dBFS, 3 s: `zc_freq(y1)` w granicach 0,1% od f/2, `y2` od f/4, po 5 cyklach; fs ∈ {44100,48000,96000}) · `test_continuity_at_flips` (max |Δy1| ≤ 1,05·max |Δx| dla sinusa i `kick`) · `test_polarity_rule_kicks` (100 uderzeń: znak pierwszego płatu `y1` = znaku `x`; korelacja przebiegów `y1` między uderzeniami ≥ 0,99) · `test_noise_immunity` (60 Hz + szum, SNR 20 i 10 dB: błąd okresu < 2%; SNR 3 dB: bramka pewności wycisza ≥ 80% próbek) · `test_polyphonic_gated` (`chord` i `pink_noise` przy `conf_gate`=50: poziom suba ≤ źródło − 40 dB po 100 ms) · `test_constant_mode_level` (−40…−6 dBFS → szczyt −12 dBFS ± 0,5 dB) · `test_phase_halving_accuracy` (40 Hz → 20 Hz ± 0,02) · `test_clipped_square_and_dc` (kwadrat 40 Hz i sinus z DC 0,2: f/2 w 0,1%, brak NaN) · `test_onset_mid_cycle` (start w szczycie: brak NaN, f/2 w 0,1% po 5 cyklach) · `test_quiet_and_loud` (−90 dBFS: `conf` = 0 i brak flipów; 0 dBFS + 12 dB: skończone).
- [ ] **Step 2: Uruchomić** `tests.test_divide` → czerwone.
- [ ] **Step 3: Implementacja.** Wektorowo: kandydaci przejść z maskami, uzbrojenie z sumą narastającą masek `x < −θ` / `x > θ` (`θ` z `peak_follower`, atak 1 ms, release 50 ms), pętla tylko po zdarzeniach; sekwencje `s1`, `s2` budowane z przełączeń; `f_est` = mediana 3 okresów + jednobiegunowy 5 Hz; `conf` wg SPEC §6.2.
- [ ] **Step 4: Testy zielone**; `p2_02_divide.py`: dokładność i THD (3f/2 względem Sub −1 przy shape 0), jitter, polifonia, porównanie z `phase_halving`, wykresy; progi: błąd < 0,1%, 3f/2 ≤ −18 dB (INFO jeśli niespełnione), ciągłość.
- [ ] **Step 5: Commit** `feat(proto): DIVIDE engine and envelopes (P2-02)`.

### Task 4: Detektory f0 i oscylator TRACK (P2-03)

**Files:**
- Create: `proto/subgen_proto/track.py`, `proto/experiments/p2_03_track.py`
- Test: `proto/tests/test_track.py`

**Interfaces:**
- Consumes: `signals`, `util`, `svf`.
- Produces: `Track(t,f0,conf)` · `track.mpm(x,fs,*,f_min=20.0,f_max=300.0,hop_ms=2.0,window_periods=2.5,fs_d_target=2000.0,prior=True)->Track` (NSDF, próg 0,9, interpolacja paraboliczna, prior ciągłości ±25%, skok oktawowy po 3 zgodnych ramkach) · `track.yin(x,fs,...)->Track` · `track.pll(x,fs,*,f_init=60.0,bw_hz=5.0)->(f_hz,phase_rad)` · `track.evaluate(trk,truth_f0,onset_t)->dict(cents_median,octave_errors,lock_time_s,lag_s)` · `track.oscillator(f0_per_sample,fs,shape_pct,phase0=0.0)->ndarray`.

- [ ] **Step 1: Testy (czerwone).** `test_mpm_sine_accuracy` i `test_yin_sine_accuracy` (f ∈ {30,41.2,55,82.4,110,164.8}, fs ∈ {44100,48000}: mediana błędu ≤ 8,7 cenów = 0,5%) · `test_octave_errors_808` (`bass_808` z 2. harmoniczną −8 dB, nuty E1…B2: `octave_errors == 0` dla MPM z priorem) · `test_glide_lag_bounded` (80 → 40 Hz w 200 ms: `lag_s < 0.060`) · `test_pll_phase_error` (55 Hz: rms(2·φ_sub − φ_in) po zbieżności ≤ 15°) · `test_noise_low_confidence` (szum różowy: mediana `conf` < 0,5) · `test_decimation_nonint_ratio` (fs 44100: efektywne `fs_d` ∈ [1500,2500], detekcja działa) · `test_oscillator_shape_blend` (shape 0 → czysty sinus, THD ≤ −80 dB).
- [ ] **Step 2: Uruchomić** → czerwone.
- [ ] **Step 3: Implementacja.** Decymacja `scipy.signal.resample_poly` do `fs_d` najbliższego ≥ `fs_d_target`; NSDF przez FFT; okno `W = ceil(window_periods·fs_d/max(f_min,20))`.
- [ ] **Step 4: Testy zielone**; `p2_03_track.py`: porównanie MPM/YIN/PLL na SIG-01/04/05/19 (błąd w cenach, błędy oktawowe, czas zaczepienia, opóźnienie dla okien 1,5/2/2,5/3 okresów), rekomendacja detektora i `L_look` (Mix/Print); progi: błąd ≤ 0,5%, 0 błędów oktawowych.
- [ ] **Step 5: Commit** `feat(proto): pitch trackers and oscillator (P2-03)`.

### Task 5: Silnik TRIGGER (P2-04)

**Files:**
- Create: `proto/subgen_proto/trigger.py`, `proto/experiments/p2_04_trigger.py`
- Test: `proto/tests/test_trigger.py`

**Interfaces:**
- Consumes: `envelope.peak_follower`, `signals`, `util`.
- Produces: `trigger.detect_onsets(x,fs,*,thresh_db=-30.0,hold_ms=20.0,lockout_ms=60.0)->ndarray` (ułamkowe indeksy; obwiednia szybka atak 0,2 ms / release 15 ms, histereza 3 dB) · `trigger.voice(fs,n,*,f_start=80.0,f_end=45.0,tau_p_ms=30.0,attack_ms=2.0,t60_ms=350.0,phase0_deg=0.0,t_on_frac=0.0)->(y1,y2)` (SPEC §6.4; y2 = f/2) · `trigger.render(x,fs,*,retrigger='restart',velocity_pct=60.0,follow=False,**voice_kwargs)->ndarray` (restart: crossfade 3 ms; legato: faza ciągła).

- [ ] **Step 1: Testy (czerwone).** `test_onset_time_accuracy` (`kick` z uderzeniami co 0,5 s: błąd wykrycia ≤ 1 próbka wobec znanego czasu progu; wariant live ≤ 2 ms) · `test_voice_determinism` (100 identycznych uderzeń na siatce całkowitych próbek: `null_db` między głosami ≤ −120) · `test_pitch_and_amp_curves` (częstotliwość chwilowa z fazy analitycznej w 0,5% wzoru po 3 ms; obwiednia w 0,1 dB) · `test_retrigger_restart_no_click` (dwa uderzenia co 100 ms: max skok ≤ 1e-3 pełnej skali) · `test_retrigger_legato_phase_continuous` (skok fazy rozwiniętej < 0,05 rad) · `test_velocity_scaling` (vel 100%: szczyt głosu ∝ szczyt źródła ± 0,5 dB; vel 0: stały) · `test_hits_closer_than_lockout` (uderzenia co 30 ms przy lockout 60: drugie ignorowane, brak NaN).
- [ ] **Step 2–4: Uruchomić (czerwone) → implementacja → zielone.** `p2_04_trigger.py`: powtarzalność, dokładność krzywych, wpływ ułamkowego czasu onsetu na różnicę głosów, wykresy.
- [ ] **Step 5: Commit** `feat(proto): TRIGGER engine (P2-04)`.

### Task 6: Silnik HARMONIC (P2-05)

**Files:**
- Create: `proto/subgen_proto/harmonic.py`, `proto/experiments/p2_05_harmonic.py`
- Test: `proto/tests/test_harmonic.py`

**Interfaces:**
- Consumes: `envelope.peak_follower`, `svf`, `util`.
- Produces: `harmonic.harmonic(x_b,fs,*,amount_pct=50.0,balance=0.0,k_max=4,att_ms=5.0,rel_ms=60.0,e_floor_db=-80.0)->ndarray` (SPEC §6.5: normalizacja, `Σ w_k·T_k`, `w_k = (amount/100)·c_k/k`, HP 15 Hz).

- [ ] **Step 1: Testy (czerwone).** `test_harmonic_weights_unit_sine` (amount 100, balance 0, k_max 5: amplitudy 2.–5. harmonicznej względem obwiedni = 1/2, 1/3, 1/4, 1/5 ± 0,5 dB) · `test_dc_removed` (|średnia| ≤ 1e-4) · `test_balance_even_odd` (balance +100: parzyste ≤ −60 dB względem nieparzystych; −100: odwrotnie) · `test_level_independent` (−40…−6 dBFS: stosunki harmonicznych w 0,5 dB) · `test_quiet_floor_and_loud` (−90 dBFS: wyjście ≤ −110 dBFS; +12 dB: skończone) · `test_fs_independent` (44100/96000: wagi w 0,5 dB).
- [ ] **Step 2–4: czerwone → implementacja → zielone.** `p2_05_harmonic.py`: wagi vs amplituda, IMD dwutonowe (SIG-17, INFO), zachowanie na `bass_808` (onset), wykres.
- [ ] **Step 5: Commit** `feat(proto): HARMONIC engine (P2-05)`.

### Task 7: Drive/Saturate, ADAA, oversampling, Auto Gain (P2-06)

**Files:**
- Create: `proto/subgen_proto/drive.py`, `proto/experiments/p2_06_drive.py`
- Test: `proto/tests/test_drive.py`

**Interfaces:**
- Consumes: `svf`, `util`, `signals`.
- Produces: `drive.shape_fn(mode,bias=0.0)->(f,F1)` (tryby `soft,tube,tape,fold,hard`; wzory z SPEC §7.3; bias: `f_b(x)=f(x+b)−f(b)`, `F1_b(x)=F1(x+b)−x·f(b)`) · `drive.process(x,fs,*,drive_pct,mode,balance=0.0,tone_hz=500.0,auto_gain=True,os_factor=1,adaa=True)->ndarray` (odniesienie −12 dBFS, pre-gain `30·drive/100` dB, `α = min(1, drive/5)`, ADAA 1. rzędu, ton LP 2. rzędu) · `drive.auto_gain_db(mode,drive_pct,balance)->float` · `drive.alias_error_db(x,fs,**params)->float` (wzorzec: naiwna nieliniowość przy 16× przez `resample_poly`, kompensacja opóźnienia 0,5 próbki ADAA przez `util.frac_delay`, pasmo 20 Hz…min(20 kHz, 0,45·fs)).

- [ ] **Step 1: Testy (czerwone).** `test_antiderivatives_numeric` (dla każdego trybu i bias ∈ {0; 0,35; 0,5}: `(F1(x+h) − F1(x−h))/(2h)` = `f(x)` ± 1e-6, h=1e-4, x ∈ [−6,6]) · `test_drive_zero_identity` (`np.array_equal(process(x,..,drive_pct=0), x)`) · `test_alpha_fade_continuity` (różnica wyjść dla drive 4,9% i 5,0% < 1e-3 względnie) · `test_auto_gain_rms` (tryby × drive ∈ {10,25,50,75,100} × balance ∈ {−100,0,100}: RMS sinusa referencyjnego w ± 0,5 dB względem drive 0) · `test_dc_after_tube` (|DC| po HP 5 Hz ≤ −80 dBFS) · `test_adaa_reduces_aliasing_hard` (Hard, drive 100%, 80 Hz, fs 48000: aliasing z ADAA ≥ 6 dB niższy niż naiwny) · `test_fs_44100_96000` (auto-gain i identyczność przy drive 0 przy obu fs).
- [ ] **Step 2–3: czerwone → implementacja.** Stabilne `ln cosh`: `|x| + log1p(exp(−2|x|)) − ln 2`; ADAA: `(F1(u[n])−F1(u[n−1]))/(u[n]−u[n−1])`, a przy `|Δu| < 1e-5` `f((u[n]+u[n−1])/2)`; Auto Gain liczone numerycznie przez 2048 próbek jednego okresu sinusa 0,25.
- [ ] **Step 4: Testy zielone**; `p2_06_drive.py`: tabela aliasingu (Live = ADAA, Mix = ADAA + 2×, Print = ADAA + 4×; tony 30/80/120 Hz; Hard i Fold; drive 25/50/100%; fs 44,1/48 kHz) względem progów −80/−90/−90 dB, widma harmonicznych per charakter, tablica Auto Gain, wykresy.
- [ ] **Step 5: Commit** `feat(proto): Drive with ADAA and auto gain (P2-06)`.

### Task 8: Narzędzia fazy — para Hilberta, rotacja, Auto-Align (P2-07)

**Files:**
- Create: `proto/subgen_proto/hilbert.py`, `proto/subgen_proto/align.py`, `proto/experiments/p2_07_phase.py`
- Test: `proto/tests/test_phase.py`

**Interfaces:**
- Consumes: `util`, `signals`.
- Produces: `hilbert.design(n_sections,f_lo=12.0,f_hi=600.0)->HilbertDesign(poles_a,poles_b,max_err_deg)` (rzeczywiste bieguny analogowe w rad/s; optymalizacja minimaks `scipy.optimize` startująca od geometrycznego przeplotu) · `hilbert.min_sections(error_deg=1.0,f_lo=12.0,f_hi=600.0,n_max=12)->int` · `hilbert.allpass_chain(x,poles_rad_s,fs)->ndarray` (`H(z)=(a+z⁻¹)/(1+a·z⁻¹)`, `a=(g−1)/(g+1)`, `g=p/(2fs)`) · `hilbert.rotate(x,theta_deg,design,fs)->ndarray` (`y = cos θ·A + sin θ·B`, B wyprzedza A o 90°, więc dodatnie θ = wyprzedzenie fazy) · `align.AlignResult(pol,rot_deg,gain_db,confidence_db)` · `align.auto_align(dry,sub,fs,band,objective='energy',step_deg=15)->AlignResult` (`objective ∈ {'energy','headroom'}`, sygnał analityczny suba, siatka θ × polaryzacja).

- [ ] **Step 1: Testy (czerwone).** `test_hilbert_pair_error` (`min_sections(1.0)` ≤ 10 i zwrócony projekt: |Δφ − 90°| ≤ 1° na 400 punktach logarytmicznych 12–600 Hz; różnica amplitud ≤ 0,05 dB) · `test_rotation_phase_and_gain` (θ ∈ {−180,−90,−45,0,45,90,180}, f ∈ {30,100,300} Hz, fs ∈ {44100,48000,96000}: przesunięcie fazy względem θ=0 równe θ ± 1°; amplituda ± 0,05 dB) · `test_polarity_exact` (`x + (−x) == 0`) · `test_auto_align_known_optimum` (dry = sin(f·t) + 0,5·sin(f/2·t + φd), sub = sin(f/2·t), φd ∈ {0,60,120,180,−90}: `rot_deg` w ± 15° od optimum, polaryzacja dla 180) · `test_auto_align_no_overlap_low_confidence` (dry bez składowej f/2: `confidence_db < 0.5`) · `test_headroom_objective_reduces_peak` (szczyt sumy dla wybranego θ ≤ szczyt dla θ=0).
- [ ] **Step 2–4: czerwone → implementacja → zielone.** `p2_07_phase.py`: tabela rząd vs błąd, opóźnienie grupowe pary w 30–200 Hz, wynik Auto-Align dla `kick_plus_808` (obie funkcje celu), wykresy.
- [ ] **Step 5: Commit** `feat(proto): Hilbert pair, rotation, auto-align (P2-07)`.

### Task 9: Filtry LFE i FIR liniowofazowy (P2-08)

**Files:**
- Create: `proto/subgen_proto/lfe.py`, `proto/experiments/p2_08_lfe.py`
- Test: `proto/tests/test_lfe.py`

**Interfaces:**
- Consumes: `svf.design_*`, `svf.freqz`, `svf.group_delay_s`.
- Produces: `lfe.encoder_filter_sos(fs)->ndarray` (założenie: `ellip(8, 0.1 dB, 80 dB)` przy 120 Hz — wartości parametrów nie wynikają z A/52, oznaczyć w raporcie) · `lfe.linear_phase_fir(target_mag,fs,n_taps,beta=8.0)->ndarray` (symetryczne jądro; `target_mag(f_hz)->amplituda`, **docelowo `|H_prototypu|`**) · `lfe.min_taps(target_mag,fs,tol_db=0.1,f_check)->int` · `lfe.out_of_band_db(kind,order,fc,fs,ratio=1.5)->float`.

- [ ] **Step 1: Testy (czerwone).** `test_group_delay_matches_scipy` (BW4 LP 120 Hz: opóźnienie grupowe z `group_delay_s` vs `scipy.signal.group_delay(sos)` ≤ 0,5% w 10–200 Hz) · `test_out_of_band_attenuation` (Butterworth przy 1,5·fc dla 12/24/48/96 dB/okt ≥ 7,0 / 14,2 / 28,1 / 56,3 dB, tolerancja 0,1 dB) · `test_fir_symmetric_and_error` (cel = BW4 LP 120 × BW4 HP 20 przy 48 kHz, N=16384: jądro dokładnie symetryczne; błąd amplitudy ≤ 0,1 dB w 25–2000 Hz; opóźnienie (N−1)/2) · `test_min_taps_monotonic` (węższy narożnik → więcej próbek) · `test_cascade_with_encoder` (suma opóźnień grupowych przy 40 Hz = suma składników ± 1e-9).
- [ ] **Step 2–4: czerwone → implementacja → zielone.** `p2_08_lfe.py`: tabela dla BW/LR/Bessel × 12–96 dB/okt: opóźnienie grupowe przy 40/80/120 Hz, tłumienie przy 1,5·fc, kaskada z filtrem enkodera (opóźnienie i zafalowanie amplitudy); FIR: `min_taps` dla HP 20 Hz + LP 120 Hz przy 44,1/48/96 kHz i wynikowa latencja w ms; rekomendacja domyślnego nachylenia i korekta zapisu |H|² → |H|.
- [ ] **Step 5: Commit** `feat(proto): LFE filter analysis and linear-phase FIR (P2-08)`.

### Task 10: Dynamika — kompresor, bramka, limiter (P2-09)

**Files:**
- Create: `proto/subgen_proto/dynamics.py`, `proto/experiments/p2_09_dynamics.py`
- Test: `proto/tests/test_dynamics.py`

**Interfaces:**
- Consumes: `envelope`, `util`, `signals`.
- Produces: `dynamics.compressor(x,fs,*,thresh_db,ratio,knee_db,attack_ms,release_ms,detect='rms',f_sub1=40.0,ripple_rule=True,auto_release=False,makeup_db=0.0)->(y,gr_db)` (krzywa statyczna z SPEC §7.4, GR wygładzane w dB, reguła: `attack_eff = max(attack, 0,25·T)`, `release_eff = max(release, 1,0·T)`, `T = 1/f_sub1`) · `dynamics.gate(x,fs,*,thresh_db,range_db,hold_ms,release_ms,attack_ms=2.0,hyst_db=3.0)->(y,open_mask)` · `dynamics.limiter(x,fs,*,ceiling_db,lookahead_ms,release_ms,live=False)->ndarray` (live: `c·tanh(x/c)`; inaczej limiter z oknem min i wygładzaniem Hanna, opóźnienie dokładnie `round(lookahead·fs)`).

- [ ] **Step 1: Testy (czerwone).** `test_comp_static_curve` (kroki sinusa 30 Hz co 3 dB: zmierzone GR vs wzór ≤ 0,1 dB, kolano ciągłe) · `test_comp_ripple_rule_thd` (40 Hz, GR 6 dB, atak 1 ms, release 10 ms: THD z regułą ≤ −60 dB; bez reguły > −50 dB) · `test_attack_release_time` (czas do 63% w 10% ustawienia dla ustawień powyżej podłogi reguły) · `test_gate_hysteresis_no_chatter` (sygnał ±1 dB wokół progu: ≤ 2 zmiany stanu) · `test_limiter_ceiling` (prostokąt +6 dB ponad sufit, tryby live i lookahead: |y| ≤ sufit + 1e-9) · `test_limiter_lookahead_delay` (impuls opóźniony dokładnie o `round(lookahead·fs)`) · `test_limiter_thd_reported_finite` (40 Hz, wejście sufit+6 dB: THD skończone).
- [ ] **Step 2–4: czerwone → implementacja → zielone.** `p2_09_dynamics.py`: THD vs czasy (atak/release 0,1…100 ms) dla 30/50/100 Hz z regułą i bez, limiter THD dla lookahead 2/5 ms, rekomendowane stałe reguły.
- [ ] **Step 5: Commit** `feat(proto): compressor, gate, limiter (P2-09)`.

### Task 11: Budżet latencji i pliki odsłuchowe (P2-10, P2-11)

**Files:**
- Create: `proto/subgen_proto/render.py`, `proto/experiments/p2_10_latency.py`, `proto/experiments/p2_11_renders.py`
- Test: `proto/tests/test_render.py`

**Interfaces:**
- Consumes: wszystkie moduły z Zadań 2–10; wyniki `proto/out/p2_03.json`, `p2_08.json`.
- Produces: `render.gain_match(ref,test,fs)->(test_matched,gain_db)` (dopasowanie RMS ważonego A) · `render.write_ab(dirpath,name,dry,wet,fs)->list[Path]` (24-bit WAV, `<name>_A_dry.wav`, `<name>_B_<label>.wav` dopasowane głośnością) · `render.lfe_51(front_lr,lfe,fs)->ndarray` (6 kanałów, kolejność WAV: FL FR FC LFE BL BR).

- [ ] **Step 1: Testy (czerwone).** `test_gain_match` (po dopasowaniu `loudness_a_db` różni się < 0,1 dB) · `test_write_ab_files` (katalog tymczasowy: 24-bit, fs i długości zgodne) · `test_lfe_51_channel_layout` (6 kanałów; energia kanału LFE ≤ 120 Hz: ≥ 40 dB powyżej 240 Hz niższa).
- [ ] **Step 2–3: czerwone → implementacja.**
- [ ] **Step 4: Zielone; eksperymenty.** `p2_10_latency.py`: z wyników Zadań 4 i 9 wyznacza `L_look` (Mix/Print), opóźnienie Linear Phase i sumaryczną tabelę `L_total` (SPEC §4.4) dla fs 44,1/48/96 kHz. `p2_11_renders.py`: pliki do `renders/` — UC1 (stopa: dry vs Divide vs Trigger), UC2 (808 i bas: Divide vs Track), UC3 (suma), UC4 (impact Trigger + Drive), UC5 (LFE 5.1 ze stopy, kanał LFE), każdy z wariantami dopasowanymi głośnością; plik `renders/README.txt` z listą i wskazówkami odsłuchu. Skrypt `render_file` (argumenty: wejściowy WAV, silnik, parametry) pozwalający przetworzyć własny materiał użytkownika.
- [ ] **Step 5: Commit** `feat(proto): latency budget and listening renders (P2-10, P2-11)`.

### Task 12: Raport Fazy 2 i korekty SPEC

**Files:**
- Create: `docs/PHASE2_REPORT.md`, `docs/img/` (wykresy z eksperymentów)
- Modify: `docs/DECISIONS.md` (zapis wyników i proponowanych ADR), `README.md` nie jest tworzony

- [ ] **Step 1:** Uruchomić wszystkie testy (`unittest discover`) i wszystkie eksperymenty; zebrać `proto/out/*.json`.
- [ ] **Step 2:** Napisać `docs/PHASE2_REPORT.md`: tabela P2-01…P2-11 (kryterium z SPEC → zmierzona wartość → PASS/FAIL/INFO), wybrane warianty (DIVIDE: licznik vs phase halving; TRACK: detektor i tryb fazy; domyślne nachylenie LFE; długości Linear Phase), wartości do wpisania w SPEC (`L_look`, stałe reguły tętnień, stałe kompresora/limitera, rząd pary Hilberta, tablica Auto Gain), lista **korekt SPEC** z uzasadnieniem (m.in. `|H|` zamiast `|H|²`, konwencja znaku `sub_rot`), niepowodzenia i ryzyka, instrukcja odsłuchu `renders/`.
- [ ] **Step 3:** Uzupełnić `docs/DECISIONS.md` (wnioski → proponowane ADR-012+). Nie edytować SPEC przed bramką — korekty są propozycjami.
- [ ] **Step 4: Commit** `docs: phase 2 report and proposed spec corrections`.
- [ ] **Step 5:** Zatrzymać się na bramce Fazy 2: raport do użytkownika (wyniki, odsłuch, korekty SPEC, decyzje).

---

## Kamienie milowe Fazy 3 (nie są zadaniami tego planu)
Osobny plan powstanie po raporcie Fazy 2 (wartości z prototypu zmieniają stałe i sygnatury): M1 szkielet repo + CMake + JUCE 9.0.4 + CI macOS/Windows + spike'i SPK-1/3/5; M2 biblioteka `subgen_dsp` w C++ (port prototypów z danymi złotymi z `proto/`, testy Catch2); M3 powłoka wtyczki (parametry z PARAMETERS.md, I/O, router, stan); M4 silniki; M5 tor sub; M6 instalatory + workflow release (draft) + spike'i SPK-2/4/6.

## Self-review (wykonane)
- **Pokrycie SPEC §8 (SPEC_BUILD_RELEASE):** P2-01→Zadanie 2, P2-02→3, P2-03→4, P2-04→5, P2-05→6, P2-06→7, P2-07→8, P2-08→9, P2-09→10, P2-10 i P2-11→11; zbiorczy raport→12.
- **Spójność typów:** `Section/FirstOrder` (Zad. 2) używane w 3, 7, 9; `peak_follower` (Zad. 3) w 5, 6, 10; `Recorder` (Zad. 1) w każdym eksperymencie; nazwy `design_*`/`freqz`/`group_delay_s` takie same w Zad. 2 i 9.
- **Review Focus:** (1) fs w Zad. 3, 4, 6, 7, 8; (2)–(3) `test_clipped_square_and_dc`, `test_onset_mid_cycle`, `test_quiet_and_loud` w Zad. 3 oraz `test_quiet_floor_and_loud` w Zad. 6; (4) `test_octave_errors_808` w Zad. 4; (5) `test_hits_closer_than_lockout` i retrigger w Zad. 5.
- **Proporcja:** plan zawiera sygnatury, nazwy testów z asercjami i wartościami SPEC; ciała funkcji pominięte poza algorytmami, których sygnatura i testy nie determinują (TPT SVF, odwzorowanie biegunów Bessela).
