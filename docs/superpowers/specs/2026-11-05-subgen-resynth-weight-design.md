# SUB-GEN — domyślne generowanie suba w stylu resyntezy + „Weight” (0.2)

Status: na prośbę użytkownika (2026-11-05): „opcja domyślnie ustawiona do generowania sub w taki sam sposób” jak Waves Submarine i Analog Obsession LOVEND, oraz zgłoszenie: „wtyczka niestabilna, sygnał niestabilny przy bardziej złożonym wejściu”. Plan przedstawiony i zaakceptowany w rozmowie.

## 1. Co robią wzorce (z publicznych opisów)
- **Submarine (Waves):** sub dodawany równolegle (Wet/Dry). Odczyt wysokości, formantu i obwiedni źródła w paśmie Range (20–240 Hz, strome filtry), z których budowane są dwa generatory: −1 i −2 oktawy, oba domyślnie włączone. Do tego Drive (saturacja suba) i Dynamics (od krótkich transjentów do długich nut). Najlepszy na pojedynczych śladach; na całym miksie recenzje słyszą artefakty.
- **LOVEND (Analog Obsession):** to wzmacniacz basu z harmonicznymi, nie generator subharmonicznych. Frequency 60–180 Hz, Boost do 10 dB, Output ±10 dB, po wczytaniu już z lekką wypukłością. Wersja 3 ma nowe DSP, a recenzje porównują jej charakter do podbicia Pulteca. Dokładny algorytm nie jest publiczny.

Nie odtwarzamy ich kodu, tylko zachowanie opisane wyżej.

## 2. Diagnoza niestabilności (pomiar przez procesor, wet only)
| Sygnał | Divide (domyślny w 0.1): wyciszony / bramka | Track Locked (0.1) poza wysokością |
|---|---|---|
| linia basu (saw, 8 harmonicznych) | 5 % / 17 % | 59 % |
| bas + stopa | 27 % / 59 % | 46 % |
| miks (bas, stopa, akord, szum) | 40 % / 85 % | 49 % |
| bas w dwudźwięku (A1+E2) | bramka 99,8 % (sub milczy) | bramka 99,8 % |
Przyczyny:
1. **Divide** to dzielnik na przejściach przez zero. Harmoniczne, stopa i drugi dźwięk w paśmie dokładają przejść, więc pewność (regularność okresów) spada, bramka tnie, a przerzutnik przeskakuje oktawy. To granica tej architektury.
2. **Track Locked:** PLL startowała tylko przy wykrytym początku nuty. W legato nie dogania skoku poza zasięgiem pętli (błąd).
3. **Bramka pewności** wycisza sub przy każdym spadku pewności, a Submarine w tym czasie brzmi dalej.

## 3. Decyzje
- **Naprawa Track legato** (SPEC §6.3): PLL przejmuje nową częstotliwość po 20 ms niezgodności ≥ 1 półton.
- **Hold Pitch** (`pitch_hold`, domyślnie on, SPEC §6.3): sub, który zabrzmiał, trzyma bramkę i ostatnią pewną wysokość, dopóki gra źródło.
- **Domyślne:** silnik Track (Locked) + Hold Pitch, Sub −2 −6 dB (oba generatory jak w Submarine). Divide zostaje do wyboru dla prostego materiału jednogłosowego.
- **Weight** (`weight_amount` 0–10 dB, domyślnie +3 dB; `weight_freq` 60–180 Hz, domyślnie 80 Hz), w duchu LOVEND, na dry przed sumowaniem, nie w bypassie i nie w kanale LFE:
  - dwa band-passy w szeregu (Q 1) dają b, a sygnał dostaje (g − 1)·b, czyli dokładnie +A dB w środku pasma i 12 dB/okt w obie strony;
  - harmoniczne b z asymetrycznego tanh (parzyste i nieparzyste), po filtrze 10 Hz zdejmującym DC, w udziale 0,6·(g − 1);
  - 0 dB to dokładna tożsamość.
- **Zgodność:** nowe parametry mają hint wersji 2 (PARAMETERS §2.15). Stan zapisany bez nich wczytuje wartości neutralne (Hold off, Weight 0 dB), więc sesje z 0.1 brzmią jak dawniej.
- **UI:**
  - Weight i Weight freq w sekcji Output;
  - Output mode na stronie Advanced → Output;
  - Hold Pitch w grupie Track strony Engine.

## 4. Wynik (pomiar jak w §2, nowe domyślne)
| Sygnał | wyciszenia | bramka | poza wysokością |
|---|---|---|---|
| linia basu | 0,0 % | 0 % | 12,5 % (dojście ok. 60–100 ms na nutę) |
| bas + stopa | 0,4 % | 0 % | 20,8 % |
| miks | 0,1 % | 0 % | 26,0 % |
| dwudźwięk | sub milczy | — | — |

## 5. Otwarte
- Dwudźwięk w paśmie (A1+E2) nie daje pewnej wysokości, więc sub nie startuje. Wymaga detekcji najniższego dźwięku (osobne zadanie).
- Przy stopie na basie Locked chwilowo podąża za przemiataniem stopy, bo jej ramki są pewne. Do rozważenia: preferowanie ciągłości wysokości nuty.
