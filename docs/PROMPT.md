# SUB-GEN — brief projektu (v2)

Zmiany względem v1: pokrętło Drive/Saturate (sekcja 8), dystrybucja przez GitHub Release z instalatorami macOS i Windows + CI (sekcja 14), Windows w v1.0, repo publiczne (licencje, strategia pushowania), warianty I/O x.0 → x.1 i wyjścia Mono/Stereo/LFE (sekcja 10), parametr Align (sekcja 8).
Repo docelowe: https://github.com/ffsfdfhbdg01/SUB-GEN (PUBLICZNE, na starcie puste).

# Zadanie: profesjonalna wtyczka audio — generator subharmoniczny i wzmacniacz basu (mono / stereo / surround / LFE)

## 1. Rola i cel
Jesteś doświadczonym inżynierem DSP i twórcą wtyczek audio (C++/JUCE), znasz psychoakustykę, miks muzyczny i post-produkcję filmową. Zaprojektuj, zaimplementuj, zwaliduj i spakuj profesjonalną wtyczkę generującą sub-bas — w klasie Waves Submarine / LoAir, Avid Pro Subharmonic, Brainworx bx_subsynth i dbx 120A/120XP, z elementami psychoakustycznymi w stylu MaxxBass, ale z szerszym zestawem funkcji, pełną obsługą surround i dedykowanym trybem LFE.

Wtyczka wykrywa (albo syntetyzuje) niskie pasmo, generuje poniżej niego sub (−1 i −2 oktawy) i/lub wzmacnia je psychoakustycznie, po czym dodaje wynik do oryginału albo oddaje sam sygnał dodany (wet-only — do aux/send/LFE).

Trzy scenariusze, które muszą działać równie dobrze:
1. Muzyka — „beef” do stopy, sub pod bas/808, pogłębienie dołu busa lub całego miksu.
2. Efekty dźwiękowe i sound design — potężne boomy, impacty, drony; sub bywa głównym elementem brzmienia i może być w pełni syntetyczny.
3. Miks filmowy — stereo i surround/immersive (w tym bedy Atmos), osobny kanał LFE, zgodność z normami, bezpieczeństwo w kinie i w domu, sensowny downmix.

Priorytety: poprawność DSP > stabilność i real-time safety > użyteczność w produkcji > funkcje dodatkowe.

## 2. Sposób pracy (obowiązkowy)
- **Fazy z bramkami.** Po każdej fazie krótki raport (zrobione / zweryfikowane i jak / niezweryfikowane / ryzyka / decyzje do podjęcia) i **czekaj na moje „OK”**. Fazy: 0 preflight + research → 1 specyfikacja i architektura → 2 prototyp DSP offline (Python/NumPy/SciPy lub C++ CLI) z pomiarami → 3 rdzeń wtyczki (DSP, parametry, I/O, formaty) + CI na macOS i Windows → 4 UI, metering, presety → 5 walidacja i stress-testy → 6 pakowanie, instalatory macOS + Windows, release (draft). Zakres startowy to tylko MUST; SHOULD/COULD dopiero po akceptacji planu.
- **Nie zgaduj.** Twierdzenia o API (JUCE, VST3, AU, AAX, CLAP), normach (ITU-R, SMPTE, Dolby, ISO) i konkurencji weryfikuj w dokumentacji, kodzie SDK i źródłach z sieci; zapisuj je w `docs/RESEARCH.md`. Czego nie da się zweryfikować, oznacz jako hipotezę i zaplanuj test, który ją rozstrzygnie. Liczby z tego briefu to cele początkowe — potwierdź lub skoryguj je w Fazie 1 z uzasadnieniem.
- **Nie słyszysz dźwięku.** Jakość udowadniaj pomiarami (widmo, THD+N, aliasing, null-testy, korelacja, latencja) i generuj pliki WAV do mojego odsłuchu A/B z krótką instrukcją, na co zwrócić uwagę. Nie pisz, że coś „brzmi dobrze”. Żadnego „działa” bez uruchomionego testu i jego wyniku; jeśli czegoś nie sprawdzono, napisz to wprost.
- Konkurencję analizuj tylko z publicznych materiałów (manuale, specyfikacje, recenzje, literatura). Algorytmy implementuj własne — bez kopiowania cudzego kodu i bez reverse engineeringu.
- Pytania do mnie zbieraj w jednej liście na końcu Fazy 0, każde z rekomendacją i wartością domyślną.
- Repo: `docs/SPEC.md`, `docs/RESEARCH.md`, `docs/DECISIONS.md` (krótkie ADR), `docs/TEST_REPORT.md`, `CHANGELOG.md`; małe, opisowe commity. Rozmawiaj ze mną po polsku; kod, identyfikatory, komentarze i commity po angielsku.
- Publikacja: repo SUB-GEN jest PUBLICZNE — wszystko, co tam trafi, jest od razu widoczne. Push i release do tego repo są autoryzowane na zasadach z sekcji 14; poza tym repo niczego nie publikuj ani nie wysyłaj na zewnątrz bez mojej zgody. Nigdy nie commituj sekretów, certyfikatów, kluczy ani SDK, których licencja zabrania redystrybucji (np. AAX SDK).

## 3. Parametry projektu i założenia techniczne
Wartości domyślne; zakwestionuj je z uzasadnieniem, jeśli research wskaże coś lepszego. Pozycje w [nawiasach] potwierdzimy w Fazie 0.
- Nazwa: SUB-GEN (nazwa repo). Sprawdź kolizje z istniejącymi produktami; alternatywy proponuj tylko w razie kolizji.
- Przeznaczenie: [użytek własny + publiczny release w repo SUB-GEN]. Licencja projektu: do rozstrzygnięcia w Fazie 0 — repo jest publiczne, a JUCE działa na AGPLv3 albo licencji komercyjnej JUCE; zweryfikuj aktualne warunki i konsekwencje (open-source vs zamknięte źródła).
- Hosty docelowe: [Cubase/Nuendo (lokalnie jest Cubase 13), Pro Tools (AAX; lokalnie niedostępny), Logic Pro, Reaper, DaVinci Resolve/Fairlight, Ableton Live]. Maksymalny layout: [7.1.4], rozważ 9.1.6.
- Stack: C++20, JUCE (najnowsza stabilna; zweryfikuj wersję), CMake (+ Ninja). Szablon CI/CD: rozważ wzorowanie się na MIT-owym Pamplejuce. Formaty: VST3, AU, AAX (do testów Pro Tools Developer; wersja produkcyjna wymaga AAX SDK i podpisu PACE — zweryfikuj i opisz kroki), Standalone do testów, CLAP opcjonalnie.
- Platformy: macOS universal (arm64 + x86_64) ORAZ Windows x64 — oba w v1.0 (release zawiera instalatory dla obu). Lokalnie pracuję na macOS (Apple Silicon, tylko Command Line Tools, bez pełnego Xcode); Windows budujesz i testujesz w CI (GitHub Actions). Sprawdź, czy CMake + Ninja na samym CLT daje VST3/AU w wersji universal; pełny Xcode instaluj wyłącznie za moją zgodą (rozmiar!).
- Precyzja: wewnętrznie double (float64), I/O float32 i float64. Fs 44,1–192 kHz; dowolny, zmienny rozmiar bloku (1–8192). Filtry 20–120 Hz przy fs 96–192 kHz są numerycznie wrażliwe: topologie TPT/Cytomic SVF lub kaskady biquadów w formie transposed DF-II w double, weryfikacja względem SciPy, wygładzanie parametrów w skali logarytmicznej.
- Real-time safety: zero alokacji, blokad, I/O i wyjątków w wątku audio; parametry atomowe; komunikacja z GUI bez blokad.
- Środowisko: miejsce jest ograniczone (dysk systemowy i /Volumes/Sam-dysk po ok. 20 GB wolnego — sprawdzaj na bieżąco). Projekt, zależności, SDK, cache, środowiska Pythona i buildy trzymaj w `/Volumes/Sam-dysk/SUB-GEN`; na dysku systemowym tylko to, czego wymagają hosty (lokalizacje instalacji wtyczek). Przed pobraniem czegokolwiek dużego podaj rozmiar i zapytaj.
- Zależności: sprawdzaj licencje (JUCE, SDK, biblioteki DSP) i zgłaszaj konsekwencje dla dystrybucji.

## 4. Architektura sygnałowa (doprecyzuj w Fazie 1)
Wejście → input trim → wybór źródła detekcji (kanały / M-S / sidechain) → filtr źródłowy (zakres basu) → silnik sub → tor sub (filtry, EQ, boost, saturacja Drive, gate, kompresor, limiter, faza) → dopasowanie fazy do dry → router / macierz wyjściowa → sumowanie z dry (dry opóźnione o latencję toru sub) → output trim → zabezpieczenia (subsonic HPF, blokada DC, limiter) → wyjście.
Wymagania: przy poziomie sub = 0 (tryb ADD) lub Mix = 0% (tryb MIX) oraz w bypassie wynik jest identyczny z wejściem (po uwzględnieniu raportowanej latencji); wet-only + dry-only = pełny mix.

## 5. Silniki generowania sub (tryby działania, przełączane w locie bez trzasków)
Zbadaj i zaimplementuj (kolejność ≈ priorytet):
- **DIVIDE** — analogowy dzielnik oktawowy (jak dbx 120A): filtr pasmowy → detekcja przejść przez zero z histerezą → flip-flop /2 (i /4) → wygładzenie do sinusa; amplituda z followera obwiedni wejścia. Lekki i odporny na szybkie zmiany wysokości (stopy), ograniczony do materiału quasi-monofonicznego.
- **TRACK** — śledzenie wysokości (f0) + oscylator: detektor f0 (np. YIN / MPM / PLL — wybierz na podstawie badań), oscylator sinus/trójkąt/mieszanka bez aliasingu, korekta błędów oktawowych (np. 808 z mocną 2. harmoniczną), bramka pewności detekcji (przy szumie i polifonii sub wygasa zamiast śmiecić), opcjonalna kwantyzacja do półtonów / skali.
- **TRIGGER** — syntetyczny sub wyzwalany transjentem lub sidechainem: detekcja onsetu (threshold, histereza, hold, lockout), obwiednia A/D/S/R, pitch envelope (np. 80 → 45 Hz), stała lub dopasowana częstotliwość, deterministyczna faza startowa (powtarzalny „punch”). Do stóp, impactów, FX.
- **HARMONIC** — psychoakustyczny („missing fundamental”, jak MaxxBass / Renaissance Bass): kontrolowane harmoniczne 2f–5f niskiego pasma, żeby na małych głośnikach słyszalna była podstawowa; z kontrolą składu harmonicznych i oversamplingiem.
- **SPECTRAL/HQ** (opcjonalnie, linear-phase lub offline): jakościowe przesunięcie o oktawę w dół (np. STFT / phase vocoder z oknem dobranym do 20–30 Hz), z raportowaną latencją; do renderu/printu.
- **HYBRID** — dowolna kombinacja silników z niezależnymi poziomami.

Każdy silnik ma: Sub1 (−1 okt) i Sub2 (−2 okt) z niezależnym poziomem, kształtem fali i fazą; obwiednię Follow (sub podąża za dynamiką wejścia) / Constant; zakres detekcji „Source range”.
**Base frequency:** Fixed (ręcznie, ok. 20–200 Hz, skala log, wpis numeryczny + nazwa nuty) lub Auto (śledzona). W DIVIDE/TRACK wyznacza środek/zakres pasma źródłowego, w TRIGGER częstotliwość generowanego tonu (albo początkową/końcową przy pitch-envelope).
**Frequency Boost:** regulowany boost rezonansowy (peak lub low-shelf, do ok. +18 dB, regulowane Q) na torze sub, domyślnie podążający za Base frequency. Jeśli widzisz lepszą interpretację tych dwóch pojęć, zaproponuj ją w Fazie 1.
UI ma podpowiadać mocne i słabe strony każdego silnika.

## 6. Psychoakustyka — badania i konsekwencje projektowe
Zrób research (m.in. ISO 226:2023, Schouten/Terhardt o wysokości wirtualnej, literatura o maskowaniu, percepcji i lokalizacji niskich częstotliwości, ITU-R BS.775/BS.2051, specyfikacje Dolby/SMPTE dla LFE) i uzasadnij nim decyzje. Minimum do uwzględnienia:
- Głośność niskich tonów rośnie z poziomem znacznie szybciej niż w średnicy, a czułość słuchu spada gwałtownie poniżej ok. 60 Hz → ścisła kontrola dynamiki toru sub, metering ważony krzywymi równej głośności, domyślny filtr subsonic (ok. 20–25 Hz): energia poniżej ok. 20 Hz marnuje headroom i może uszkadzać głośniki bass-reflex.
- Poniżej ok. 80–100 Hz lokalizacja jest słaba → sub domyślnie mono; szerokość stereo opcjonalna, z ostrzeżeniem o kompatybilności mono.
- Maskowanie wzwyż: nadmiar sub zamula low-mid → gate/expander, sidechain/ducking, analizator z podglądem sumy.
- Ocena wysokości i „uderzenia” basu wymaga kilku okresów (30 Hz = 33 ms/okres) → rozsądne dolne limity attack/release; zero-latency vs lookahead jako świadomy kompromis.
- Wysokość wirtualna: harmoniczne mogą zastąpić podstawową na małych głośnikach → tryb HARMONIC + podgląd „small speaker”.
- Polaryzacja i faza w niskich pasmach decydują o sumowaniu z oryginałem (stopa, bas) i o headroomie (asymetria przebiegów); opóźnienie grupowe filtrów LF jest słyszalne na transjentach → wybór minimum-phase vs linear-phase.

## 7. Korelacja fazowa — trzy warstwy
- **A. Detekcja na wejściu (stereo/surround):** Sum (L+R) / Mid / Max / per-channel / Auto. Auto korzysta z korelacji, bo przy sygnale antyfazowym suma L+R się znosi i sub zniknie (wtedy |L|+|R| albo kanał dominujący). Wybór kanałów i wag (domyślnie bez LFE i bez surroundów).
- **B. Relacja sub ↔ dry:** polaryzacja 0/180°; rotacja fazy 0–360° (Hilbert/all-pass); Auto-align — analiza korelacji dry↔sub w paśmie ok. 30–150 Hz, wybór polaryzacji/rotacji maksymalizujących energię sumy, wskaźnik i tryb hold/lock. Tryby generacji: phase-locked (stała relacja do wejścia — DIVIDE/PLL), free-running, reset-on-trigger (ta sama faza startowa przy każdym uderzeniu). Dzielenie przez 2 ma z natury dwuznaczność znaku — zaprojektuj spójne ustalanie polaryzacji.
- **C. Wyjście:** bass-mono poniżej zadanego progu, szerokość sub 0–100%, korelometr (−1…+1) dla wejścia, sub i sumy w paśmie LF, wskaźnik kompatybilności mono.

## 8. Sekcje funkcjonalne (zakresy, jednostki i wartości domyślne podaj w SPEC)
Cała ta sekcja należy do zakresu MUST.
- **Wejście:** input trim; źródło detekcji; filtr źródłowy HP/LP 12–48 dB/okt; czułość/threshold; sidechain / external key.
- **Generator:** silnik, Base frequency, Sub1/Sub2, kształt fali, tracking speed/smoothing, Follow/Constant, gate/range, Frequency Boost.
- **Tor sub (tylko dodany sygnał, przed sumowaniem):** filtry HP/LP (nachylenie do 96 dB/okt; Butterworth/Linkwitz-Riley/Bessel; minimum-phase lub linear-phase); parametryczny EQ (≥ 5 pasm: HP/LP, shelve, bell; Q; krzywa na analizatorze); kompresor (threshold, ratio, knee, attack, release/auto, RMS/peak, makeup, filtr sidechaina, opcjonalnie lookahead); gate/expander; limiter true-peak; polaryzacja i rotacja fazy.
- **Drive/Saturate (pokrętło na widoku Main):** 0–100% (z odczytem w dB) na torze sub, domyślnie po generatorze i przed kompresorem (pozycja Pre/Post-comp do wyboru). Min. 4 charaktery krzywej (Soft/tanh, Tube — asymetryczna, parzyste harmoniczne, Tape, Hard/Fold), regulacja balansu harmonicznych parzystych/nieparzystych, filtr tonu po saturacji i kompensacja poziomu (auto-gain on/off) do uczciwych porównań A/B. Anty-aliasing: oversampling (≥ 4× w trybach Mix/Print) i/lub ADAA; blokada DC po krzywych asymetrycznych. Podgląd krzywej transferowej w UI. Parametr automatyzowalny i wygładzany. Psychoakustycznie: nasycenie dokłada harmoniczne, dzięki którym sub jest słyszalny na małych głośnikach (wysokość wirtualna), przy zachowaniu kontroli pasma (w trybie LFE saturacja działa przed LPF LFE, więc harmoniczne ponad pasmem są odcinane).
- **Sumowanie/wyjście:** tryb ADD (dry bez zmian + sub o regulowanym poziomie — domyślny) i MIX (crossfade dry/wet, prawo equal-power lub liniowe); Wet-only (dry wyciszone, latencja bez zmian — do aux); Align (ręczne i automatyczne wyrównanie czasowe dry ↔ sub, ms/próbki); bypass bez trzasków i bez zmiany latencji; output trim i ceiling.
- **Routing/surround:** macierz źródło → cel; tryby wyjścia przełączane w trakcie pracy: Mono (jeden sub, sumowany i dodawany do wybranych/wszystkich kanałów), Stereo (L/R niezależnie lub sprzężone, z kontrolą szerokości), Surround (per kanał: poziom, opóźnienie, polaryzacja, mute/solo), LFE-only (sekcja 9).
- **Metering:** widmo 10–500 Hz (dry / sub / suma, nakładka krzywej ISO 226), oscyloskop / phase scope, korelometr, peak/RMS/true-peak, GR.

## 9. Tryb LFE
- Wyjście wyłącznie do kanału LFE (mono), identyfikowanego po typie kanału (`juce::AudioChannelSet`), nigdy po indeksie. Layout bez LFE → czytelny komunikat i bezpieczny fallback (wybór kanału docelowego albo wyłączenie) — o ile nie działa wariant x.0 → x.1 z sekcji 10.
- Pasmo: LPF domyślnie 120 Hz (wybór 80/100/120 Hz), nachylenie ≥ 48 dB/okt (cel: ≥ 60 dB tłumienia już oktawę powyżej odcięcia), HPF subsonic ok. 20 Hz. Zweryfikuj w normach i specyfikacjach (Dolby, SMPTE, ITU-R, wymagania dystrybutorów).
- Generacja zoptymalizowana pod LFE: zakres bazowy ok. 25–80 Hz, priorytet czystego przebiegu (niski THD) i kontroli szczytów, bez sztuczek „dla małych głośników” (LFE gra na subwooferze), domyślny kompresor/limiter dobrany do krótkich, potężnych zdarzeń.
- Poziomy: wskaźnik/kalibracja względem konwencji LFE (m.in. +10 dB odtwarzania względem kanałów głównych w kinie — zweryfikuj), konfigurowalny sufit true-peak. LFE nie wlicza się do pomiaru głośności wg ITU-R BS.1770 (zweryfikuj) — mierz RMS/peak w paśmie 20–120 Hz, nie LUFS.
- Faza względem bass managementu: LFE sumuje się w subwooferze z basem przekierowanym z kanałów głównych (crossover ok. 80 Hz) → narzędzia opóźnienia/rotacji fazy i wskaźnik korelacji LFE ↔ bas z kanałów głównych.
- Downmix: LFE bywa pomijane przy downmixie do stereo (zweryfikuj ITU-R BS.775 / Dolby) → ostrzeżenie i opcjonalny bleed do kanałów głównych, żeby krytyczny bas nie był wyłącznie w LFE.
- Atmos: obsługa kanału LFE w bedach (7.1.2 itd.); obiekty poza zakresem wtyczki — opisz to w dokumentacji.

## 10. Formaty kanałów i I/O
- Layouty: mono, stereo, LCR, quad, 5.0, 5.1, 6.1, 7.0, 7.1 (także SDDS), 7.1.2, 7.1.4, 9.1.6 — wszystko, co host faktycznie udostępnia; ambisonics tylko jeśli uzasadnisz sens (np. praca na kanale W).
- Kanały identyfikuj po typie, nie po indeksie; testuj różne kolejności hostów (Pro Tools film, Logic, VST3/Steinberg, DTS).
- Konfiguracje I/O: główna in == out; dodatkowo (tam, gdzie format pozwala) warianty x.0 → x.1 (np. 5.0 → 5.1, 7.0 → 7.1: wtyczka dokłada kanał LFE, gdy wejście go nie ma — jak w LoAir), mono → stereo i mono → surround dla wet-only; opcjonalny bus sidechain; AAX multi-mono. Zbadaj ograniczenia VST3/AU/AAX i opisz macierz wsparcia w SPEC.
- Wyjścia w stylu Avid Pro Subharmonic: Mono / Stereo / LFE (dla layoutów wielokanałowych) jako przełączane tryby.
- Zmiana trybu wyjścia, layoutu i silnika w trakcie pracy: bez trzasków (crossfade równej mocy), bez zmiany raportowanej latencji (stała latencja per tryb jakości).

## 11. Dodatkowe funkcje (zaproponuj własne w SPEC z uzasadnieniem i priorytetem)
- **MUST:** A/B + undo/redo + presety z kategoriami (Kick Beef, Bass/808, Mix-bus Low-End, Cinematic Boom/Impact, LFE, Utility); Solo sub / Delta (odsłuch samego dodanego sygnału); tryby jakości Live / Mix / Print (oversampling, długości okien, linear-phase); wyrównanie głośności przy A/B (głośniejsze nie może wydawać się lepsze).
- **SHOULD:** Note-lock + strojenie A=440; wyzwalanie MIDI (tam, gdzie format pozwala — zweryfikuj); podgląd „small speaker” i podgląd bass-managementu (LR4 ~80 Hz); przetwarzanie M/S; trimy per kanał w surround; analizator z wagą ISO 226.
- **COULD:** ambisonics (kanał W); morphing presetów; dopasowanie do tonacji utworu.

## 12. Wymagania jakościowe i kryteria akceptacji (cele początkowe)
- Zestaw sygnałów testowych: sinusy, sweepy, burst'y, syntetyczna stopa z pitch-dropem, bas 808, miks polifoniczny, szum różowy, sygnały antyfazowe L/R, impulsy, cisza, DC, pełna skala, NaN/Inf, denormale.
- Dokładność: Sub1/Sub2 dla sinusów 30–200 Hz = f/2 i f/4 z błędem < 0,1% po ustabilizowaniu; THD+N toru sub (TRACK/TRIGGER, sinus, Drive = 0) ≤ −70 dB; dla DIVIDE i HARMONIC udokumentuj widmo harmonicznych.
- Drive/Saturate: pomiar widma harmonicznych i aliasingu dla każdego charakteru i kilku poziomów drive; brak DC; poziom wyjścia skompensowany (±0,5 dB) przy auto-gain.
- Aliasing: ≤ −90 dB względem sygnału w paśmie 20 Hz–20 kHz przy fs ≥ 44,1 kHz (oversampling na nieliniowościach).
- Null-testy: bypass i sub = 0 → identyczność z wejściem po kompensacji latencji (≤ −140 dBFS, docelowo bit-exact); wet-only + dry-only = pełny mix w tolerancji ≤ −140 dB.
- Niezmienniczość: identyczny wynik niezależnie od rozmiaru bloku (≤ −130 dB, docelowo bit-exact); spójność między fs (relacje fazowe, latencja w ms); offline bounce = real-time.
- Latencja raportowana dokładnie (błąd ≤ 1 próbki względem pomiaru impulsem).
- LFE i surround: brak energii poza pasmem LFE zgodnie z deklaracją; poprawny routing w każdym layoucie i kolejności kanałów; przecieki między kanałami ≤ −120 dB.
- Faza: Auto-align zwiększa energię sumy na sygnałach testowych w antyfazie; polaryzacja i rotacja zgodne z teorią (pomiar).
- Stabilność: losowa automatyzacja wszystkich parametrów (≥ 10^6 zmian), ekstremalne ustawienia i sygnały → brak NaN/Inf, trzasków (limit skoku w dB/ms) i wzrostu pamięci; test soak ≥ 8 h przed wydaniem.
- Real-time safety sprawdzona narzędziowo: ASan/UBSan/TSan, RealtimeSanitizer jeśli dostępny, własny hook alokacji w testach.
- CPU: wyznacz budżet w Fazie 1 (stereo 48 kHz / 128 próbek na Apple Silicon z wszystkimi sekcjami oraz 7.1.4), zmierz i raportuj; brak skoków CPU.
- Stan i automatyzacja: pełny save/restore z wersjonowaniem i migracją, stabilne ID parametrów, sensowne nazwy/zakresy/skale pod automatyzację i kontrolery.
- Walidatory (wszystkie PASS, na macOS i Windows tam, gdzie dotyczy): pluginval (strictness 10) dla VST3/AU, `auval -v` dla AU, validator Steinberga dla VST3, AAX Validator (Avid), clap-validator (jeśli CLAP). Headless testy regresji (własny host JUCE lub np. Pedalboard) odpalane skryptami i w CI.
- Instalatory: instalacja na czystym koncie macOS i na czystym środowisku Windows (runner CI/VM), wtyczki skanują się i ładują w hostach, odinstalowanie usuwa wszystko, co dodano.
- Testy w hostach: przygotuj dla mnie checklistę ręczną (Cubase, Pro Tools, Logic, Reaper, Nuendo, Resolve/Fairlight, Ableton) i pliki testowe (kick-beef, 808, impact FX, test kanałów 5.1/7.1.4 z tonem identyfikacyjnym na każdym kanale, A/B).

## 13. UI/UX i artefakty
- UI: natywny JUCE (wektorowy, HiDPI, skalowanie 75–200%), ciemny motyw, po angielsku (standard branżowy); widok Main (kluczowe pokrętła, w tym Drive/Saturate) i Advanced (EQ, kompresor, macierz routingu). Double-click reset, fine-adjust (Shift/Cmd), wpis numeryczny, tooltips z jednostkami, czytelne ostrzeżenia („layout bez LFE”, „brak sygnału sidechain”, „sygnał znosi się w sumie mono”). Rysowanie i analiza nie mogą obciążać wątku audio; zamknięty edytor = zerowy koszt. Funkcje czysto odsłuchowe (podglądy) wyraźnie oznaczone i wyłączane przy renderze offline.
- Artefakty: kod (CMake) i buildy VST3/AU/AAX/Standalone; skrypty `build.sh`, `test.sh`, `validate.sh`, `package.sh`; workflow GitHub Actions; docs (SPEC, RESEARCH, DECISIONS, TEST_REPORT, instrukcja po polsku); presety fabryczne; raport licencji.

## 14. Dystrybucja: instalatory i release na GitHubie
- Repo docelowe: https://github.com/ffsfdfhbdg01/SUB-GEN (PUBLICZNE, na starcie puste). Wynik końcowy: GitHub Release (SemVer, tag `vX.Y.Z`) z instalatorem macOS i instalatorem Windows, sumami SHA-256 i notatkami wydania z `CHANGELOG.md`; README z instrukcją instalacji i odinstalowania.
- **macOS:** instalator `.pkg` (pkgbuild/productbuild; ewentualnie w `.dmg`), universal. VST3 → `/Library/Audio/Plug-Ins/VST3`, AU → `/Library/Audio/Plug-Ins/Components`, AAX → `/Library/Application Support/Avid/Audio/Plug-Ins` (tylko gdy AAX w buildzie), presety → `/Library/Application Support/<producent>/<nazwa>/`. Podpis Developer ID + notaryzacja, jeśli mam konto Apple Developer (pytanie w Fazie 0); w przeciwnym razie instalator niepodpisany + opis, jak go uruchomić mimo Gatekeepera.
- **Windows:** instalator `.exe` (Inno Setup lub NSIS — wybierz i uzasadnij) → VST3 do `%CommonProgramFiles%\VST3`, AAX do `%CommonProgramFiles%\Avid\Audio\Plug-Ins` (tylko gdy w buildzie), presety, wpis odinstalowania w „Aplikacje i funkcje”. Podpis Authenticode (np. Azure Trusted Signing), jeśli mam dostęp; w przeciwnym razie niepodpisany + informacja o SmartScreen.
- Odinstalowanie usuwa wszystko, co dodał instalator; aktualizacja nie nadpisuje presetów użytkownika.
- Buildy wyłącznie w CI (GitHub Actions, runnery macOS i Windows), uruchamiane na tagu; artefakty trafiają do release automatycznie. CI działa od Fazy 3 (build + testy + pluginval na obu platformach przy każdym pushu), bo Windowsa nie uruchomię lokalnie. Sekrety (certyfikaty, hasła) wyłącznie w GitHub Secrets — nigdy w repo ani w logach.
- AAX: dołączaj tylko jeśli mam AAX SDK i podpis PACE (pytanie w Fazie 0); AAX SDK nie trafia do repo. Bez podpisu AAX nie załaduje się w produkcyjnym Pro Tools — zweryfikuj i opisz w README.
- Licencje (repo publiczne, więc publikacja = dystrybucja): przed pierwszym release rozstrzygnij zgodność JUCE (AGPLv3 vs licencja komercyjna), VST3 SDK i innych zależności z wybraną licencją projektu; dodaj plik LICENSE i listę licencji zależności.
- Strategia pushowania (autoryzowana, z ograniczeniami): lokalnie commituj często; push do SUB-GEN na branch `dev` (nie `main`) po zakończeniu faz i po moim OK; `main` aktualizuj tylko przez merge po moim OK; nigdy force-push; nie zmieniaj ustawień repo (visibility, branch protection, Actions secrets) bez pytania.
- Release: najpierw jako **draft** (lub pre-release) z kompletem instalatorów; zmieniasz go w publiczny dopiero po moim OK po Fazie 5 i po wynikach walidacji (pluginval/auval/VST3 validator, instalacja na czystych środowiskach).

## 15. Start
Zacznij od Fazy 0: (1) preflight środowiska (macOS, architektura, Xcode/CLT, CMake, Python, wolne miejsce, stan repo SUB-GEN), (2) research (psychoakustyka, algorytmy sub, konkurencja, normy LFE/surround, ograniczenia formatów, licencje, instalatory i CI), (3) zbiorcza lista pytań do mnie z rekomendacjami, (4) zarys Fazy 1. Nie pisz jeszcze kodu wtyczki.
