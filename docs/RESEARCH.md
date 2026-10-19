# SUB-GEN — RESEARCH (Faza 0)

Data: 2026-10-19 · Status: do akceptacji (bramka Fazy 0) · Brief źródłowy: `docs/PROMPT.md` (v2)

Legenda pewności: **[P]** źródło pierwotne (norma, kod, tekst licencji, API GitHuba, wynik polecenia) · **[W]** wtórne (strona producenta, prasa, streszczenie artykułu) · **[F]** forum / opinia · **[H]** hipoteza do rozstrzygnięcia testem.
Strony pobierane narzędziem WebFetch przechodzą przez automatyczne streszczenie, więc traktuję je jako [W]. Teksty norm (ITU-R BS.775-4, ATSC A/52:2012) wyciągnąłem lokalnie z PDF-ów (PDFKit) i oznaczam jako [P]. Nic poniżej nie jest poradą prawną.

## 1. Najważniejsze ustalenia
1. **Nazwa „SubGen” jest zajęta.** Solid State Logic sprzedaje od 2023 r. wtyczkę SubGen (4-pasmowy generator subharmoniczny, VST/AU/AAX, iLok) [W]. Repo może zostać SUB-GEN, ale nazwa produktu, ID bundla i kody AU powinny być inne.
2. **Repo jest publiczne i puste** [P]. Każdy push jest od razu publiczny; pierwszy push wyznaczy domyślną gałąź.
3. **Licencja to decyzja architektoniczna.** JUCE 9 jest dwulicencyjne (AGPLv3 albo JUCE EULA), a AAX SDK dołączony do JUCE ma baner „komercyjna licencja Avid albo GPLv3” [P]. Ścieżka AGPLv3 pasuje do publicznego repo i dopuszcza AAX bez opłat; ścieżka EULA zabrania łączenia JUCE z kodem objętym copyleftem (§2.3 EULA), więc AAX wymagałby komercyjnej licencji Avid [W/H].
4. **Lokalny toolchain prawdopodobnie wystarczy na VST3/AU bez pełnego Xcode** [H — potwierdzić spike'iem w Fazie 3]: Command Line Tools z clang 17, SDK z AudioUnit/AudioToolbox, `pkgbuild`, `productbuild`, `notarytool`, `auval`. Brakuje Ninja, pluginval, scipy/matplotlib. Wolne miejsce: 17 GiB (system) i 21 GiB (Sam-dysk).
5. **Windows tylko przez CI.** Dla publicznego repo standardowe runnery GitHub są bezpłatne [W]; obraz Windows ma Inno Setup 6.7.1, WiX 3.14, CMake, Ninja [P]; obraz macOS ma Xcode, CMake, Ninja [P].
6. **Istnieje gotowy szkielet CI/CD:** Pamplejuce (MIT, aktywny, JUCE 9.x, CMake, Catch2, pluginval 1.x, podpis i notaryzacja macOS, podpis Windows) [P].
7. **AAX to osobny tor.** JUCE 9 obsługuje stemy AAX do 9.1.6 i ambisonics 1.–7. rzędu [P], ale produkcyjny Pro Tools ładuje tylko wtyczki podpisane (iLok + narzędzia od Avid) [W/F], a lokalnie nie ma Pro Tools.
8. **Normy LFE potwierdzone u źródła** [P]: pasmo do 120 Hz, zakres obsługi 20–120 Hz, konwencje poziomu zależne od branży, LFE często pomijany w downmixie do stereo, ostrzeżenie przed kaskadą LPF (opóźnienie grupowe). Szczegóły w sekcji 7.
9. **Konkurencja ma wspólny zestaw funkcji:** zakres źródłowy (Range), Sub −1/−2, Drive (u Submarine i SSL także z kompensacją), dynamika/kompresor, Dry/Wet, Align i Feed per kanał (LoAir), wyjścia Mono/Stereo/LFE (Pro Subharmonic) [W]. Szczegóły w sekcji 4.
10. **Psychoakustyka nakłada ograniczenia, które korygują brief:** dolna granica wysokości 19–30 Hz [W], lokalizacja basu słaba, lecz niezerowa [W], minimalne opóźnienie detekcji f0 ≈ 1–2 okresy (≈ 25–50 ms dla E1 = 41 Hz) [W]. Szczegóły w sekcji 5.
11. **Podpisy kosztują**; bez nich instalatory działają, lecz z ostrzeżeniami systemowymi [W]. Szczegóły w sekcji 10.
12. **Luki:** brak oficjalnego tekstu Dolby o LFE w obiektach Atmos oraz manuali PDF Waves/Avid (HTTP 403) — wnioski o tych elementach są [W]/[F].

## 2. Środowisko lokalne (preflight) [P]
| Element | Stan |
|---|---|
| macOS | 15.7.3 (24G419), arm64 (Apple Silicon) |
| Dyski | system: 228 GiB, wolne 17 GiB · /Volumes/Sam-dysk: 931 GiB, wolne 21 GiB (98% zajęte) |
| Toolchain | tylko Command Line Tools (Apple clang 17.0.0; brak pełnego Xcode, `xcodebuild` niedostępny); SDK macOS 14/15 z AudioUnit, AudioToolbox, CoreAudioKit, Accelerate; `notarytool`, `pkgbuild`, `productbuild`, `codesign`, `auval` dostępne; Swift 6.1.2 |
| Build | CMake 4.3.4 (Homebrew); brak Ninja; brak pluginval |
| Python | 3.13.7 (python.org) z numpy 2.5.1; brak scipy, matplotlib, soundfile, pedalboard |
| Git / GitHub | git 2.39.5; gh 2.96.0 zalogowany jako ffsfdfhbdg01 (scope: repo, gist, read:org) |
| Hosty audio | Cubase 13, GarageBand; brak Pro Tools, Logic, Reaper, Resolve, Ableton |
| Wtyczki referencyjne | brak konkurencyjnych generatorów sub (nic z Waves/Avid/Brainworx) — brak materiału do A/B |

Wnioski: (a) Windows i AAX nie do przetestowania lokalnie; (b) pełny Xcode (kilkanaście GB) jest niewskazany przy 17–21 GiB wolnego; (c) jedyny lokalny host to Cubase 13 (VST3), więc Cubase jest hostem referencyjnym.

## 3. Repo docelowe [P]
- https://github.com/ffsfdfhbdg01/SUB-GEN — PUBLICZNE, puste (brak domyślnej gałęzi, licencji i opisu), utworzone 2026-10-19.
- Lokalnie: `/Volumes/Sam-dysk/SUB-GEN` (git init, remote `origin` dodany, nic nie wypchnięte).

## 4. Konkurencja — wzorce funkcjonalne [W]
| Produkt | Silnik / idea | Kontrolki i funkcje | Kanały | Wniosek dla SUB-GEN |
|---|---|---|---|---|
| Waves Submarine (2019) | „Organic ReSynthesis”: rozbiór na nośną, wysokość, formanty i obwiednię, odbudowa jako nowe niskie częstotliwości; zero latencji | Range 20–240 Hz; Sub −1/−2; Drive (saturacja dodanych subów); Dynamics (od krótkich transjentów po sustain); Dry/Wet | mono/stereo | Zakres źródłowy ≈ 20–240 Hz jako punkt odniesienia dla „Source range”; Drive i kształt obwiedni są standardem klasy |
| Waves LoAir | generator „pitch” z materiału (wg producenta także polifonicznego) | Frequency (LPF), LoAir (poziom generowanego), Direct, Direct filter, **Align** (opóźnienie dry względem sub), Output / LFE Output, **Feed** (ile L/R, C, Ls/Rs, LFE trafia do silnika) | Mono→Mono, Stereo→Stereo (LFE-only lub stereo+LFE), 5.0→5.1, 5.1→5.1 | Feed per kanał, Align i warianty x.0→x.1 są już w briefie v2 |
| Avid Pro Subharmonic | generator sub dla postprodukcji | selektor zakresu (4 pozycje); osobne poziomy: dolne pasmo syntezy, górne pasmo syntezy, bezpośredni bas; wyjście Mono / Stereo / LFE | 5.1, 6.1, 7.1, 7.1 SDDS; AAX Native/DSP; iLok | Wyjścia Mono/Stereo/LFE jako przełączane tryby — zgodne z briefem v2 |
| SSL SubGen (2023) | „inwersja fali” — bipolarna fala oktawę niżej | 4 niezależne pasma (częstotliwość, szerokość, wzmocnienie, metering); Drive z kompensacją wzmocnienia i trybami Bass / Kick / Pre; kompresor SSL; Mix; filtry HP/LP 12 dB/okt; analizator; Universal 2 | strona nie podaje; sprzedawca: VST/AU/AAX | Wielopasmowość (2–4 pasma źródłowe) i tryby Drive per źródło to kandydaci SHOULD; **nazwa zajęta** |
| Brainworx bx_subsynth (2017) | model dbx 120XP | 3 pasma; blok „Edge” (Smooth/Harsh: kompresja + saturacja); M/S | — | Kompresja + saturacja na torze sub to uznany wzorzec; M/S jako SHOULD |
| dbx 120A / 120XP (hardware) | opatentowana synteza odtwarzająca kształt fali oryginału | wejście 54–110 Hz → synteza 26–56 Hz (zakresy 26–36 i 36–56 Hz); crossover 80/120 Hz; bez syntezy pasmo 20 Hz–20 kHz ±0,5 dB | — | Wzorzec dla silnika DIVIDE |
| MaxxBass / Renaissance Bass | harmoniczne zamiast niesłyszalnej podstawowej | — (publikacja AES 1999, Ben-Tzur i in.; nie pobrana) | — | Wzorzec dla silnika HARMONIC; Submarine pozycjonuje się jako przeciwieństwo (dla dużych subwooferów) |

## 5. Psychoakustyka — fakty i konsekwencje
| Temat | Co ustaliłem | Konsekwencja |
|---|---|---|
| Krzywe równej głośności | ISO 226:2023 zastąpiła edycję 2003; różnice ≤ 0,6 dB, głównie przy 20 Hz (korekta progu słyszenia wg ISO 389-7:2019); dotyczy tonów czystych i słuchaczy 18–25 lat [W] | Analizator/metering z wagą ISO 226:2023; pamiętać, że kontury dotyczą tonów czystych |
| Dolna granica wysokości | ≈ 19 Hz (Jurado, Larrea, Moore 2021) do ≈ 30 Hz (melodie, Pressnitzer i in. 2001); limit ma charakter czasowy (≈ 33 ms); poniżej limitu dyskryminacja pogarsza się z ~1% do ~5–6% [W] | Poniżej ~30 Hz sub jest bardziej „odczuwany” niż „słyszany jako ton”: dokładność śledzenia wysokości jest tam mniej ważna niż obwiednia, faza i kontrola szczytów |
| Lokalizacja niskich częstotliwości | brak jednego progu; reguła praktyczna: crossover 80–100 Hz; praca magisterska Aalto: zmiany kierunku wykrywalne dla tonów > ~63,5 Hz i szumu oktawowego od 31,5 Hz (główna wskazówka: ITD); Theile (AES 1992): crossover nie wyżej niż 100–140 Hz zależnie od ustawienia suba [W] | Sub domyślnie mono jest rozsądny, ale nie twierdzimy, że kierunek jest niesłyszalny; „Bass-mono” z regulowaną częstotliwością (domyślnie ~100 Hz) |
| Wysokość wirtualna | harmoniczne mogą zastąpić brakującą podstawową na małych głośnikach; podejścia NLD (nieliniowe) vs PV (phase vocoder): PV daje precyzyjną kontrolę wag harmonicznych i lepiej radzi sobie ze stanem ustalonym, NLD lepiej z transjentami; istnieją hybrydy rozdzielające transjenty i stan ustalony [W]; analiza Oo/Gan dotyczy doboru nieliniowości (m.in. unikanie symetrii parzystej) [W/H — sprawdzić w pracy źródłowej] | HARMONIC jako hybryda: tor transjentowy NLD + tor stanu ustalonego (syntezowane harmoniczne); dobór krzywych Drive z uwzględnieniem symetrii |
| Opóźnienie detekcji f0 | estymatory audio-to-MIDI mają zwykle minimum ~2 okresy; metody specjalizowane dla basu schodzą do ~1,1 okresu (≈ 27 ms dla 41 Hz), ogólne wymagają ~50 ms [W]; dla E1 (41 Hz) okres = 24,4 ms | DIVIDE (połówka cyklu, zero-crossing) jest jedynym naprawdę niskolatencyjnym podejściem; TRACK potrzebuje lookahead i zgłaszanej latencji (Live vs Mix/Print) |
| Maskowanie wzwyż, headroom, bass-reflex | wiedza ogólna, bez źródła w tej sesji [H] | Bez zmian względem briefu v2 (subsonic, limiter, gate/ducking) |
| Opóźnienie grupowe filtrów LF | BS.775-4 Annex 7 ostrzega przed kaskadą LPF dla kanału LFE [P] | Wybór minimum-phase/linear-phase, umiarkowane domyślne nachylenie LPF w trybie LFE |

## 6. Algorytmy
- **DIVIDE:** detektor przejść przez zero z histerezą, przerzutnik (flip-flop) /2, odwracanie co drugiego cyklu; drugi przerzutnik daje /4; działa dla materiału quasi-monofonicznego z mocną podstawową; wyjście wygładza się filtrem [W]. SSL (inwersja fali) i dbx (odtworzony kształt fali oryginału) opisują ten sam typ działania [W]. Ryzyka: dwuznaczność znaku, DC, chatter bez histerezy, polifonia.
- **TRACK:** YIN / MPM / PLL; ograniczenia opóźnienia jak w sekcji 5; Submarine stosuje bardziej złożoną resyntezę (nośna/wysokość/formanty/obwiednia) [W] — nie kopiujemy, to nasz punkt odniesienia jakości.
- **TRIGGER:** wzorzec to klasyczny „kick sub”: onset → oscylator z obwiednią i pitch-envelope; deterministyczna faza startowa [H — do pomiaru w Fazie 2].
- **HARMONIC:** hybryda NLD + synteza harmonicznych (sekcja 5).
- **Anti-aliasing nieliniowości (Drive/Saturate):** ADAA 1. rzędu (Parker, Zavalishin, Le Bivic — DAFx 2016; Bilbao, Esqueda, Parker, Välimäki — IEEE SPL 2017) różnicuje antypochodną nieliniowości; wnosi ok. 0,5 próbki opóźnienia grupowego, dobrze tłumi aliasy niskoczęstotliwościowe, słabiej wysokoczęstotliwościowe → ADAA + oversampling 2× (Mix) / 4× (Print) [W].
- **Filtry LF i rotacja fazy:** TPT/Cytomic SVF lub kaskady biquadów w double; rotacja fazy przez Hilbert/all-pass [wiedza ogólna, do weryfikacji względem SciPy w Fazie 2].

## 7. LFE i surround
**ITU-R BS.775-4 (12/2022) [P]:**
- W wymianie programów kanał LFE powinien być pasmowo ograniczony do nominalnego pasma (do 120 Hz); w nadawaniu nie wolno przesyłać w nim informacji powyżej 120 Hz (rekomendacje 4–5).
- Annex 7: LFE ma obsługiwać 20–120 Hz; nie może przenosić całej niskoczęstotliwościowej zawartości miksu, jest opcjonalny po stronie odbiorcy i „często nie jest włączany do downmixu 2-kanałowego”, a kanały główne muszą zawierać wszystko, co niezbędne; należy uważać na opóźnienie grupowe LPF, zwłaszcza kaskadowych.
- Poziom: w wymianie programów LFE zapisuje się z offsetem −10 dB, kompensowanym w odtwarzaniu (głośnik LFE +10 dB względem kanałów głównych w paśmie < 120 Hz); przemysł filmowy i DVD-Video wymagają +10 dB przy odtwarzaniu, a muzyka (DVD-Audio, SACD) koduje LFE tak, że offset wynosi 0 dB.
- Równania downmixu 3/2 → 2/0 (Table 2) nie zawierają LFE.

**ATSC A/52:2012 (AC-3 / E-AC-3) [P]:** LFE to opcjonalny pojedynczy kanał o paśmie < 120 Hz, przeznaczony do odtwarzania +10 dB względem kanałów pełnopasmowych; enkoder powinien filtrować LFE przy 120 Hz (typowo filtr eliptyczny 8. rzędu); w E-AC-3 istnieje opcjonalny „LFE mix level” do zmiksowania LFE do L/R przy downmixie.
**Wniosek:** wtyczka nie powinna sama zmieniać poziomu LFE. Powinna oferować konwencje metringu (Film +10 dB / Muzyka 0 dB / Wymiana −10 dB), a domyślny LPF 120 Hz powinien być umiarkowany, bo enkoder doda własny stromy filtr.

**BS.1770 [W]:** kanał LFE nie jest uwzględniany w pomiarze głośności (waga 0) — kilka zgodnych źródeł wtórnych; próby włączenia LFE wymagały arbitralnej korekty ok. −10 dB.
**Atmos [F/W]:** kanał LFE istnieje tylko w bedach; obiekty nie mogą kierować sygnału do LFE (wątki Steinberg; jeden niezweryfikowany głos przeciwny); maksymalny bed to 7.1.2 (dokumentacja Logic Pro). Brak oficjalnego tekstu Dolby w tej sesji.
**Pro Tools [W]:** od 2023.6 Ultimate obsługuje szerokości ścieżek 5.0.2–9.1.6 (Studio do 7.1.6). **JUCE 9.0.4 [P]:** wrapper AAX mapuje stemy do 9.1.6 oraz ambisonics 1.–7. rzędu; `AudioChannelSet` oferuje m.in. mono, stereo, LCR, LRS, LCRS, 5.0–7.1 (także SDDS), 5.x.2/5.x.4, 7.x.2/4/6, 9.x.4/6 (także warianty ITU), quad, pentagonal, hexagonal, octagonal, ambisonic(n), discrete.
**Cubase 13 [W]:** obsługuje konfigurację 7.1.4; host narzuca układ głośników, a wtyczka VST3 musi zadeklarować pasujący układ; kolejność kanałów bywa różna między formatami (iPlug2) → testować w Cubase 13 lokalnie [H].

## 8. Formaty wtyczek, SDK i walidatory
- **JUCE 9.0.4** (2026-10-12) [P]. 9.0.0 przyniosło m.in. parser SVG, czcionki zmienne, nowy CoreAudio, headless CMake [W]. **CLAP: brak w drzewie JUCE 9.0.4** [P]; plan z mapy drogowej 2025 nie został zrealizowany w tej wersji [W] → CLAP tylko przez `clap-juce-extensions` (MIT, aktywne) [P].
- **VST3 SDK:** licencja MIT [P]. **AU:** frameworki w SDK z CLT [P]. **AAX:** SDK dołączone do JUCE (`modules/juce_audio_plugin_client/AAX/SDK`) z bannerem „komercyjna licencja Avid lub GPLv3” (v2.8.0, lipiec 2024) [P]; strona Avid: click-through, do podpisu AAX potrzebny iLok, narzędzia i licencję komercyjną uzyskuje się przez audiosdk@avid.com [W]; wymóg podpisu PACE dla produkcyjnego Pro Tools oraz build deweloperski uruchamiający niepodpisane wtyczki to informacje z forów [F].
- **Walidatory:** pluginval 1.0.4 (12/2024; paczki macOS/Windows/Linux) [P]; `auval` systemowy [P]; clap-validator aktywny [P]; AAX Validator (Avid) wymaga SDK/narzędzi Avid [W].
- **Szkielet projektu:** Pamplejuce (MIT): C++23, JUCE 9.x jako submoduł, CPM, CMake ≥ 3.25, Catch2 3.8.1, cele Tests i Benchmarks, Melatonin Inspector, GitHub Actions (build/test na Linux/macOS/Windows, pluginval, podpis i notaryzacja macOS, podpis Windows przez Azure), plik VERSION, `CLAUDE.md`/`AGENTS.md` z kreatorem konfiguracji [P]. Uwaga: domyślnie testy czerwienieją bez skonfigurowanego podpisu → w SUB-GEN podpis musi być opcjonalny.

## 9. Licencje (nie jest poradą prawną)
| Element | Stan |
|---|---|
| JUCE 9 | AGPLv3 albo JUCE EULA. Poziomy wg juce.com i EULA: **Starter** (bezpłatny; przychód/finansowanie ≤ 20 000 USD w ostatnich 12 mies.), **Indie** (40 USD/mies. lub 800 USD wieczysta; ≤ 300 000 USD), **Pro** (175 USD/mies. lub 3 500 USD; bez limitu), **Educational** (bezpłatny, niekomercyjny) [W]. EULA §1.9: dystrybucja Produktów dozwolona; §2.3: nie wolno robić niczego, co poddaje Framework licencji open-source wymagającej ujawnienia kodu; §1.14 i §3.6: przekroczenie progu → zakup wyższego poziomu albo koniec dystrybucji, plus dopłaty; wymagania „Made with JUCE” nie znalazłem w pobranym tekście [W/H]. Ceny i EULA w JUCE 9 bez zmian względem JUCE 8 [W] |
| AAX SDK | podwójnie: licencja komercyjna Avid albo GPLv3 [P] |
| VST3 SDK | MIT [P] |
| Pamplejuce | MIT [P] |

**Ścieżka A (AGPLv3):** projekt cały na AGPLv3; JUCE na AGPLv3; AAX SDK na GPLv3 (GPLv3 i AGPLv3 można łączyć); VST3 MIT; koszt 0; kod musi być publiczny (pasuje do publicznego repo); AAX możliwy w buildzie, podpis PACE nadal potrzebny do wydania dla Pro Tools.
**Ścieżka B (JUCE EULA):** Starter (≤ 20 tys. USD) lub Indie/Pro; kod może być prywatny albo publiczny na własnej licencji; **bez** kodu na copyleft w tym samym produkcie (EULA §2.3) → AAX tylko z komercyjną licencją Avid; publiczne repo z binariami wymagałoby osobnego repo na źródła, jeśli kod ma pozostać prywatny.
Zalecenie: A, o ile nie planujesz sprzedaży zamkniętego produktu; do rozstrzygnięcia przez Ciebie. Przed pierwszym release przeczytać oryginalny tekst EULA §1.2, §1.9, §2.3 (moje wnioski opierają się na streszczeniu).

## 10. Dystrybucja: CI, instalatory, podpisy
- **GitHub Actions:** w publicznych repo standardowe runnery są bezpłatne i bez limitu minut (obowiązują limity współbieżności) [W]; macOS arm64 (macos-14/15, 26 w podglądzie), Windows x64 (windows-latest/2025/2022) [W].
- **macOS:** `.pkg` z `pkgbuild` + `productbuild` (narzędzia lokalnie i na runnerze). Podpis: Apple Developer Program 99 USD/rok; Developer ID Application (bundle'e), Developer ID Installer (dla `.pkg`); `notarytool` + staple; typowe pułapki: zły typ certyfikatu, niepodpisane zagnieżdżone biblioteki, zmiana bundle'a po podpisie [W]. Bez podpisu: od Sequoia nie działa obejście przez Control-click, użytkownik zatwierdza w Ustawieniach → Prywatność i ochrona („Otwórz mimo to”) [W]; część deweloperów zgłasza odrzucanie także notaryzowanych `.pkg` na Sequoia/Tahoe [F]. Zalecenie awaryjne: README z instrukcją, ewentualnie skrypt instalacyjny.
- **Windows:** Inno Setup (preinstalowany na obrazie Windows 2025: 6.7.1) jest naturalnym wyborem; WiX 3.14 dostępny. Podpis: Azure Artifact Signing (dawniej Trusted Signing) 9,99 USD/mies.; według źródła z kwietnia 2026 dostępny dla firm i osób samozatrudnionych z USA, Kanady, UE i Wielkiej Brytanii; weryfikacja tożsamości od godziny do ok. miesiąca [W]. Bez podpisu: ostrzeżenie SmartScreen.
- **Lokalizacje instalacji (standard hostów):** macOS — `/Library/Audio/Plug-Ins/VST3`, `/Library/Audio/Plug-Ins/Components`, `/Library/Application Support/Avid/Audio/Plug-Ins`; Windows — `%CommonProgramFiles%\VST3`, `%CommonProgramFiles%\Avid\Audio\Plug-Ins` [wiedza ogólna; potwierdzić w Fazie 6].
- **AAX w release:** tylko po uzyskaniu narzędzi podpisu od Avid; do tego czasu AAX jest celem buildu CI, bez dystrybucji.

## 11. Korekty względem briefu v2 (propozycje do akceptacji)
K1. **Nazwa produktu ≠ „SubGen”** (kolizja z SSL); repo i nazwa robocza w kodzie mogą zostać SUB-GEN.
K2. **Sekcja 3 — licencja:** dwie ścieżki (sekcja 9); JUCE 9.0.4 jako wersja bazowa; Pamplejuce jako wzorzec CI; standard C++ do decyzji technicznej w Fazie 1 (propozycja C++20).
K3. **Sekcja 9 — LFE:** (a) domyślny LPF 120 Hz o umiarkowanym nachyleniu (propozycja 24 dB/okt) z opcją stromego (48–96 dB/okt) i opcją linear-phase; usuwam sztywny cel „≥ 60 dB tłumienia oktawę wyżej” jako wymóg domyślny (BS.775 Annex 7 ostrzega przed opóźnieniem grupowym, a enkoder A/52 doda własny filtr eliptyczny 8. rzędu); test tłumienia odnosi się do wybranego nachylenia; (b) poziomy: metering z przełączaną konwencją Film +10 dB / Muzyka 0 dB / Wymiana −10 dB, bez automatycznej zmiany wzmocnienia; (c) nasycenie przed LPF LFE — bez zmian.
K4. **Sekcja 6:** lokalizacja — łagodniejsze sformułowanie (brak jednego progu); dolna granica wysokości 19–30 Hz → priorytet obwiedni, fazy i szczytów poniżej ~30 Hz; „Bass-mono” z regulowaną częstotliwością (domyślnie ~100 Hz).
K5. **Sekcje 5/8 — z konkurencji:** Source range zakotwiczony w 20–240 Hz (Submarine); Drive z kompensacją wzmocnienia i trybami per źródło Bass/Kick/Pre (SSL); kształt dynamiki transjent ↔ sustain (Submarine); wielopasmowość 2–4 pasm źródłowych jako SHOULD (SSL, bx_subsynth); Feed per kanał i Align (LoAir) — zostają MUST.
K6. **Sekcja 10:** wrapper AAX w JUCE 9 obsługuje stemy do 9.1.6; kanał LFE w Atmos tylko w bedach (7.1.2); CLAP przez `clap-juce-extensions` jako SHOULD.
K7. **Sekcje 12/14 — walidacja i dystrybucja:** AAX Validator i wydanie AAX warunkowe wobec narzędzi podpisu Avid; podpisy macOS/Windows opcjonalne w CI; instrukcje obejścia ostrzeżeń w README; Windows w CI jako jedyna weryfikacja, stąd potrzeba wcześniejszego pierwszego pusha.
K8. **Sekcja 3 — środowisko:** budowanie lokalne na samym CLT (bez pełnego Xcode), Ninja do doinstalowania (~0,3 MB), venv Pythona na Sam-dysk (scipy, matplotlib, soundfile; szacunkowo 400–600 MB), pluginval (~25 MB) — po Twojej zgodzie.

## 12. Luki i ryzyka
- Brak oficjalnych tekstów Dolby (Atmos, LFE w obiektach) oraz manuali PDF Waves/Avid (HTTP 403); wnioski z opisów producentów i prasy.
- Streszczenia EULA JUCE i stron producentów pochodzą z automatycznego streszczania; przed decyzją licencyjną czytamy oryginały.
- Warunki uzyskania narzędzi podpisu AAX (audiosdk@avid.com) nieznane; czas i koszt otwarte.
- Wolne miejsce: 17 GiB (system) i 21 GiB (Sam-dysk); duże instalacje wymagają zgody.
- Zdolność CLT do zbudowania universal VST3/AU z CMake+Ninja jest hipotezą [H].
- Patenty: nie badano swobody działania (FTO); technika DIVIDE jest stara (dbx 1970s), patenty z lat 2000. na generatory subharmoniczne istnieją w wynikach wyszukiwania, ale nie analizowano ich ważności [H].
- Wartości ISO 226:2023 wymagają normy (płatna) lub implementacji referencyjnej.
- Latencje i dokładność detektorów f0 oraz progi jakości z briefu — do pomiaru w Fazie 2.

## 13. Źródła
**Konkurencja i produkty:** [Waves Submarine (Clyne Media)](https://clynemedia.com/waves/Submarine/Waves_Submarine.html) · [Submarine (MusicTech)](https://musictech.com/news/gear/waves-audios-submarine-plug-in-generates-deeper-cleaner-sub-bass/) · [Waves LoAir (Clyne Media)](https://clynemedia.com/waves/LoAir/Waves_LoAir.html) · [Avid Pro Subharmonic](https://www.avid.com/plugins/pro-subharmonic) · [Pro Subharmonic (Sweetwater)](https://www.sweetwater.com/store/detail/ProSubharmAAX) · [SSL SubGen (store)](https://store.solidstatelogic.com/plug-ins/subgen) · [SSL SubGen (ProSoundWeb)](https://www.prosoundweb.com/ssl-offering-new-subgen-bass-enhancement-plugin/) · [bx_subsynth](https://www.plugin-alliance.com/en/products/bx_subsynth.html) · [dbx 120A (zZounds)](https://www.zzounds.com/pagearea--61/item--DBX120A) · [dbx 120A (B&H)](https://www.bhphotovideo.com/c/product/261170-REG/dbx_120A_120A_Sub_Harmonic_Bass.html)
**Psychoakustyka i algorytmy:** [ISO 226:2023 (Acoust. Sci. Technol. 2024)](https://doi.org/10.1250/ast.e23.66) · [Virtual bass, DAFx 2020](https://acris.aalto.fi/ws/portalfiles/portal/51973173/Eloi2020_Virtual_bass_DAFx2020.pdf) · [Oo — NLD w systemach virtual bass (NTU)](https://dr.ntu.edu.sg/server/api/core/bitstreams/ac125f2a-2640-47df-b6b1-da7378296748/content) · [Lokalizacja niskich częstotliwości (Aalto)](https://aaltodoc.aalto.fi/items/11a818ce-326d-494b-ac70-3945100fb27d) · [Theile, AES 1992](https://hauptmikrofon.de/theile/1992-3_Subwoofer-concep_92.AES-Preprint3335.pdf) · [Dolna granica wysokości](https://auditory.org/postings/2000/95.html) · [Derrien, DAFx-14](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf) · [f0 dla basu, niskie opóźnienie](https://sol.sbc.org.br/index.php/sbcm/article/view/10433) · [ADAA (DAFx 2016)](https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA) · [ADAA (CCRMA)](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html) · [Vicanek — AA distortion](https://www.vicanek.de/articles/AADistortion.pdf) · [Sub-octave generator](https://doudoroff.com/logic/V7RyM2Qs-sub-octave_generator.html)
**Normy i surround:** [ITU-R BS.775-4](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.775-4-202212-I!!PDF-E.pdf) · [ATSC A/52:2012](https://www.atsc.org/wp-content/uploads/2015/03/A52-201212-17.pdf) · [BS.1770 i LFE (TV Technology)](https://www.tvtechnology.com/opinions/the-effects-of-new-formats-on-loudness) · [Wagi kanałów BS.1770](https://cacophony.js.org/variables/CHANNEL_WEIGHTS.html) · [Atmos: obiekty i LFE (Steinberg)](https://forums.steinberg.net/t/objects-and-lfe/697851) · [Beds i obiekty (Audient)](https://audient.com/ko/tutorial/objects-and-beds-explained/) · [Logic Pro — szerokość bedu](https://support.apple.com/ar-qa/guide/logicpro/lgcp73f9b9ac/mac) · [Pro Tools 2023.6 (SOS)](https://www.soundonsound.com/news/pro-tools-20236-now-available) · [Cubase 13 — konfiguracje surround](https://steinberg.help/cubase_pro/v13/en/cubase_nuendo/topics/surround_sound/surround_sound_surround_configurations_available_r.html) · [iPlug2 — surround](https://mintlify.com/iPlug2/iPlug2/examples/surround)
**Formaty, licencje, CI:** [JUCE 9.0.4](https://github.com/juce-framework/JUCE/releases/tag/9.0.4) · [JUCE LICENSE.md](https://github.com/juce-framework/JUCE/blob/9.0.4/LICENSE.md) · [JUCE — licencje](https://juce.com/get-juce/) · [JUCE 9 EULA](https://juce.com/legal/juce-9-licence/) · [JUCE 9: bez zmian cen i EULA](https://forum.juce.com/t/juce-9-no-pricing-or-eula-changes/69000) · [JUCE — What's new](https://juce.com/releases/whats-new/) · [AAX SDK open-source (JUCE forum)](https://forum.juce.com/t/aax-open-source-release/44017) · [Avid AAX developer](http://developer.avid.com/aax/) · [AAX codesigning (JUCE forum)](https://forum.juce.com/t/aax-codesigning/36700) · [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) · [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) · [clap-validator](https://github.com/free-audio/clap-validator) · [pluginval](https://github.com/Tracktion/pluginval/releases) · [Pamplejuce](https://github.com/sudara/pamplejuce) · [Melatonin: podpis i notaryzacja macOS w CI](https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/) · [Melatonin: Azure Trusted Signing](https://melatonin.dev/blog/code-signing-on-windows-with-azure-trusted-signing/) · [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) · [runner-images](https://github.com/actions/runner-images) · [Apple Developer Forums: notaryzowany pkg](https://developer.apple.com/forums/thread/825798)
