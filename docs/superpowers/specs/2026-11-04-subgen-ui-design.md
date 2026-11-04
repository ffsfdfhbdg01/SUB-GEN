# SUB-GEN — projekt interfejsu (Faza 4, edytor)

Status: decyzje projektowe podjęte przez asystenta na prośbę użytkownika („sam za mnie decyduj”), 2026-11-04. Uzupełnia SPEC_IO_UI §7; nie zmienia SPEC ani rejestru parametrów.

## 1. Cel
Zastąpić `GenericAudioProcessorEditor` własnym edytorem: minimalistycznym, czytelnym, w duchu FabFilter (duży wyświetlacz, bezpośrednia manipulacja na wykresie, kontekstowe kontrolki) i Waves (wyraźne główne pokrętło, panel mierników, szybkie przełączniki). Kontrolki używane w każdej sesji są widoczne zawsze; reszta (około 110 ze 141 parametrów) mieszka w wysuwanym panelu „Advanced” z zakładkami.

**Nie-cele (na później w Fazie 4):** przeglądarka i zapis presetów, A/B, motyw jasny, wariant wysokiego kontrastu, krótkie nazwy hosta, tekst wartości parametrów po stronie hosta, oscyloskop i korelometr.
**Anty-cele:** ściana pokręteł jak w edytorze generycznym; jakikolwiek koszt w wątku audio przy zamkniętym edytorze; alokacje w `paint`.

## 2. Tożsamość wizualna
- Temat: głębokość i ciśnienie (granat głębokiej wody) + ciepło nasycenia (bursztyn/żar). Żar pojawia się tylko tam, gdzie powstaje sub: krzywa sub na wykresie, łuk Drive, miernik Sub.
- Paleta: tło `#101826`, panel `#162131`, panel podniesiony `#1D2A3D`, linie `#2A3A51`; tekst `#DDE4EE`, tekst drugorzędny `#8796AB`; sub/akcent `#F2A33A` → żar `#FF6A3D` (kolor łuku Drive rośnie z wartością); dry `#7F9CC0`; suma `#E9EEF5`; ostrzeżenie `#FF5D5D`; OK `#5BD1A5`.
- Typografia: systemowy bezszeregowy krój (SF na macOS, Segoe UI na Windows), etykiety w zapisie zdaniowym (bez wersalików), wartości cyframi tabelarycznymi.
- Jedno „głośne” miejsce: duże pokrętło **Drive** ze świecącym łukiem; wszystko inne cienkie i ciche.

## 3. Układ (1000 × 620 przy 100 %, skalowanie 75–200 % przy stałym aspekcie)
```
┌ pasek górny 40: SUB-GEN │ ↶ ↷ │ preset │ ostrzeżenie │ Quality [Live|Mix|Print] │ skala │ ⏻ ┐
├ wyświetlacz 210: widmo 10–500 Hz (dry, sub, suma), pasmo źródła z uchwytami lo/hi,      │
│                 odczyt wysokości Sub −1 (Hz, nuta, cent); po prawej mierniki In/Sub/Out/GR│
├ sekcje główne 334: Source │ Generator │ Sub (Drive 150 px) │ Output                     │
├ pasek Advanced 36: Source · Engine · Dynamics · EQ · Filters · Output · LFE        [▲]  ┘
```
Kliknięcie zakładki wysuwa panel (180 ms) nad sekcjami głównymi; wyświetlacz zostaje widoczny. Zakładka EQ dodatkowo rysuje na wyświetlaczu krzywą EQ z przeciągalnymi węzłami pasm.

## 4. Podział kontrolek
| Sekcja główna | Kontrolki |
|---|---|
| Pasek górny | undo/redo, nazwa presetu, ostrzeżenia, `quality`, skala, `bypass` |
| Source | `range_lo`, `range_hi` (także uchwyty na wykresie), `det_thresh`, `src_mode` |
| Generator | `engine` (przełącznik segmentowy), `sub1_level`, `sub2_level`, `shape` |
| Sub | **`drive`**, `drive_mode`, `sub_level`, `boost_gain`, `sub_pol`, przycisk Auto-Align |
| Output | `sum_mode`, `dry_level` lub `mix` (kontekstowo wg `sum_mode`), `out_trim`, `wet_only`, `out_mode` |

| Zakładka Advanced | Parametry |
|---|---|
| Source | `src_gain`, `range_slope`, `feed_*` (5), `sc_use`, `sc_gain` |
| Engine | `base_mode`, `base_freq`, `follow`, `env_attack`, `env_release`, `dynamics`, `conf_gate` + grupa aktywnego silnika (`trk_*`/`phase_gen`, `trig_*`, `harm_*`, `hyb_*`) |
| Dynamics | `drive_balance`, `drive_tone`, `drive_auto`, `drive_pos`, `boost_type/freq/follow/q`, `gate_*`, `comp_*` |
| EQ | `eqN_*` (5 pasm) |
| Filters | `sub_hp_*`, `sub_lp_*`, `filt_type`, `filt_phase`, `lim_*`, `sub_rot`, `align_obj`, `align_lock` |
| Output | `mix_law`, `align_ms` (+ Auto), `route_*` (poziom, opóźnienie, polaryzacja), `sub_width` |
| LFE | `lfe_*` |
Każdy z 141 parametrów ma dokładnie jedno miejsce w UI (test). Grupa silnika zależy od `engine`; Divide pokazuje tylko wspólne.

## 5. Zachowanie kontrolek (SPEC_IO_UI §7.1)
Przeciąganie pionowe; Shift = precyzja ×0,1; podwójne kliknięcie lub Alt-klik = wartość domyślna; kółko = krok; prawy klik → menu „Enter value…”, „Reset to default”; tooltip z nazwą, wartością i zakresem z jednostkami; etykiety dostępności i fokus klawiatury. Wartości formatowane z jednostkami („−6.0 dB”, „Off”, „45.0 Hz”, „1.20 kHz”, „350 ms”).
Undo/redo: tylko gesty użytkownika w edytorze (nie automatyka hosta), menedżer w procesorze, ok. 100 kroków; Cmd/Ctrl-Z, Shift-Cmd/Ctrl-Z.

## 6. Dane dla UI
`UiFeed` w procesorze: cztery tory float (S1 dry, sub po limiterze, suma kanałów Front, szczyt wyjścia) w `AbstractFifo` + GR kompresora i limitera jako atomiki. Zapis tylko przy otwartym edytorze. Widmo liczone w wątku UI: FFT 16384, Hann, nakładanie 75 %, wygładzanie 1/6 okt., 0 dB = sinus pełnej skali. Mierniki: szczyt + RMS 300 ms, clip (W-06).

## 7. Pozostawione wykonawcy
Dokładne odstępy, krzywe animacji, rozmieszczenie w panelach Advanced, rysunek ikon.
