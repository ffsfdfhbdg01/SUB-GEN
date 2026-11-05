# SPK-1 i SPK-2 — checklista dla Cubase i GarageBand (Faza 3, STOP 2)

**Cel.** SPK-1: Cubase ładuje VST3, a GarageBand AU zbudowane samym CLT + CMake + Ninja (SPEC_BUILD_RELEASE §2 i §9). SPK-2: Cubase przyjmuje VST3 z wejściem równym wyjściu dla 5.1, 7.1.2 i 7.1.4 oraz parę 5.0 → 5.1, a kanały trafiają na swoje miejsca. Kroki pokrywają część T-HOST-01 i T-HOST-02.

## Przygotowanie

1. **Wtyczka.** Build `cmake --build --preset release` (universal) kopiuje ją do `~/Library/Audio/Plug-Ins/VST3/SUB-GEN.vst3` i `~/Library/Audio/Plug-Ins/Components/SUB-GEN.component`. Producent na listach hosta: „Vendor”.
2. **Pliki sesji.** Polecenie `python3 tools/make_channel_id_session.py` zapisuje do `renders/spk2/` pliki `spk2_5.0.wav`, `spk2_5.1.wav`, `spk2_7.1.2.wav` i `spk2_7.1.4.wav`: 48 kHz, 24 bit, WAV z maską kanałów, 8,5 s (0,5 s ciszy, potem sygnał). Kanał k pliku niesie ton 1000 + 100·k Hz (−20 dBFS) i łoże 60 Hz o poziomie swojej klasy. Kolejność kanałów i tony:

   | Plik | Kanały i tony |
   |---|---|
   | 5.0 | L 1000 · R 1100 · C 1200 · Ls 1300 · Rs 1400 Hz |
   | 5.1 | L 1000 · R 1100 · C 1200 · LFE 1300 · Ls 1400 · Rs 1500 Hz |
   | 7.1.2 | L 1000 · R 1100 · C 1200 · LFE 1300 · Lrs 1400 · Rrs 1500 · Ls 1600 · Rs 1700 · Ltf 1800 · Rtf 1900 Hz |
   | 7.1.4 | jak 7.1.2, dalej Ltr 2000 · Rtr 2100 Hz |

3. **Analiza zgrania.** Polecenie `python3 tools/check_bounce.py PLIK.wav --layout 5.1` drukuje dla każdego kanału ton, który niesie (czyli skąd przyszedł), łoże 60 Hz, sub 30 Hz (DIVIDE z łoża; od −50 dBFS liczy się jako sub) i początek sygnału. Zgranie eksportuj jako WAV interleaved (24 bit lub 32 bit float, bez RF64).

## Kroki

Wszystkie parametry domyślne, chyba że krok mówi inaczej. Parametr `out_mode` ma w edytorze nazwę „Output Mode”.

1. [ ] **SPK-1, VST3.** W Cubase (13 lub nowszy) otwórz Studio → VST Plug-in Manager i odśwież listę. SUB-GEN (Vendor) ma być na liście. Wstaw go na ścieżkę stereo: wtyczka się ładuje, edytor zastępczy (suwaki) się otwiera, dźwięk przechodzi.
2. [ ] **Ścieżki surround.** Zaimportuj `spk2_5.1.wav`, `spk2_7.1.2.wav` i `spk2_7.1.4.wav`, każdy na ścieżkę audio o tej samej konfiguracji. Wstaw SUB-GEN na każdą: czy Cubase go oferuje (wejście = wyjście)?
3. [ ] **Para 5.0 → 5.1.** Zaimportuj `spk2_5.0.wav` na ścieżkę 5.0. Czy Cubase pozwala wtyczce wyjść jako 5.1 (konfiguracja wyjść wtyczki, ścieżka 5.0 wysłana na grupę 5.1 albo inna droga)? Zanotuj, co Cubase oferuje.
4. [ ] **`out_mode` LFE, 5.1.** Na ścieżce 5.1 ustaw Output Mode = LFE. File → Export → Audio Mixdown: kanał wyjściowy 5.1, interleaved. Uruchom `check_bounce.py` z `--layout 5.1`. Oczekiwany wynik (ta sama wtyczka zrenderowana offline, z kompensacją latencji):

   ```
    ch  speaker  tone of            tone dB   bed dB    sub dB   onset ms
     0  L        1000 Hz (L)         -20.0    -20.0   -173.7     500.0
     1  R        1100 Hz (R)         -20.0    -20.0   -173.7     500.0
     2  C        1200 Hz (C)         -20.0    -18.4   -172.1     500.0
     3  LFE      1300 Hz (LFE)       -20.0    -26.0    -26.8     500.0
     4  Ls       1400 Hz (Ls)        -20.0    -21.9   -175.7     500.0
     5  Rs       1500 Hz (Rs)        -20.0    -21.9   -175.7     500.0
   sub (30 Hz >= -50 dBFS) in: 3 (LFE)
   channel order: as the session's
   ```

5. [ ] **`out_mode` Surround, 5.1.** To samo z Output Mode = Surround. Oczekiwany sub: L, R i C około −27 dBFS, Ls i Rs około −33 dBFS (`route_surround` −6 dB), LFE bez suba:

   ```
   sub (30 Hz >= -50 dBFS) in: 0 (L), 1 (R), 2 (C), 4 (Ls), 5 (Rs)
   channel order: as the session's
   ```

6. [ ] **7.1.4 (i 7.1.2), oba tryby.** Kroki 4–5 na ścieżce 7.1.4 z `--layout 7.1.4`. W trybie LFE sub ma być tylko w LFE. W trybie Surround: L, R, C, a kanały surround (Lrs, Rrs, Ls, Rs) 6 dB ciszej. Kanały wysokości i LFE bez suba. Wiersz `channel order` pokazuje, czy Cubase zachował kolejność kanałów. Jeśli nie, `check_bounce.py` wypisze, który kanał źródła trafił gdzie.
7. [ ] **PDC.** W krokach 4–6 kolumna „onset ms” ma mieć 500,0 (± 1 ms): Cubase skompensował latencję wtyczki (Quality Mix: 2260 próbek, 47,1 ms przy 48 kHz). Wynik około 547 ms oznacza brak kompensacji (sprawdź, czy „Constrain Delay Compensation” jest wyłączone). Opcjonalnie powtórz z Quality = Print: onset nadal 500 ms.
8. [ ] **Zapis i odczyt projektu.** Zmień kilka parametrów (Drive 40 %, Output Mode, Phase Rotation 30°, Align Lock on). Zapisz projekt, zamknij go i otwórz ponownie: wartości są zachowane, odtwarzanie działa.
9. [ ] **SPK-1, AU (T-HOST-02).** W GarageBand utwórz nowy projekt ze ścieżką audio. Plug-iny → Audio Units → Vendor → SUB-GEN: wtyczka się ładuje, edytor się otwiera. Na nagraniu basu sub jest słyszalny, a zmiana parametru działa.

## Wyniki do odesłania

Dla każdego kroku: OK albo NOK i notatka. Dla kroków 4–6 wklej wydruk `check_bounce.py`. Przy NOK — co Cubase lub GarageBand pokazał (zrzut lub komunikat).

| Krok | Wynik | Notatki |
|---|---|---|
| 1 SPK-1 VST3 | | |
| 2 ścieżki 5.1 / 7.1.2 / 7.1.4 | | |
| 3 para 5.0 → 5.1 | | |
| 4 LFE, 5.1 | | |
| 5 Surround, 5.1 | | |
| 6 7.1.4 (7.1.2) | | |
| 7 PDC | | |
| 8 zapis i odczyt | | |
| 9 SPK-1 AU | | |

**Reguła cięcia (SPEC_BUILD_RELEASE §12):** jeśli SPK-2 wypadnie negatywnie dla 5.0 → 5.1, warianty x.0 → x.1 zostają tylko tam, gdzie host je przyjmuje.
