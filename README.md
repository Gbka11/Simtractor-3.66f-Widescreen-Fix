# SimTractor 3.66f – Widescreen Fix

**Author:** Sponge (Gbka)  
**Game:** SimTractor V3.66f (32-bit), original game files  
**Requires:** [ASI Loader for SimTractor (d3d9.dll)](https://github.com/Gbka11/ASI-loader-for-SimTractor-V3.66f)

🇬🇧 [English](#english) · 🇵🇱 [Polski](#polski)

---

## English

### What it does
- Removes the black bars and uses the full screen (e.g. 1920x1080, windowed 1280x720).
- Hor+ field of view: the view gets wider on the sides, the image is not stretched.
- Menu and HUD stay in a centered 4:3 area (corner icons can be anchored to the screen edge).
- Scales 2D text (chat, button labels, top log, multiplayer lobby) with the resolution.
- Everything is done in memory – no game files are changed.

### Install
1. Install the ASI Loader (`d3d9.dll` next to `SimTractor.exe`).
2. Copy `widescreen.asi` and `widescreen.ini` to the `mods` folder of the game.
3. Edit `widescreen.ini`: `Width`/`Height` (fullscreen), `WindowWidth`/`WindowHeight` (windowed).
4. Start the game.

### Uninstall
Delete `mods\widescreen.asi` and `mods\widescreen.ini`.

### Settings (`widescreen.ini`)
| Key | Meaning |
|---|---|
| `Width`, `Height` | fullscreen resolution |
| `WindowWidth`, `WindowHeight` | windowed size (0 = same as fullscreen) |
| `HorPlus` | 1 = Hor+ FOV, 0 = original FOV |
| `RemoveLetterbox` | 1 = remove black bars |
| `UiAspect`, `UiCenter` | menu/HUD area aspect and centering |
| `TextScale`, `TextScaleList` | 2D text scale; entry formats are described in the ini |
| `Log`, `LogUi` | write `widescreen.log` / verbose UI log (for bug reports) |

### Troubleshooting
- Set `Log=1` (and `LogUi=1` for UI/text problems) and send `widescreen.log`.
- If the log says a file has a different version, only V3.66f with original files is supported.
- Text too big/small: edit `TextScale` / `TextScaleList`.
- Known minor issue: a few white lines in the multiplayer lobby are not repositioned.

### Build
Open **x86 Native Tools Command Prompt for VS** (32-bit) in the project folder:
```
cl /nologo /LD /O2 /MT src\widescreen.cpp /Fe:mods\widescreen.asi user32.lib
```
or run `build.bat`.

---
## ☕ Support 
If my mods helped you, you can buy me a coffee: [ko-fi.com/gbka1](https://ko-fi.com/gbka1)

## Polski

### Co robi
- Usuwa czarne pasy i wykorzystuje cały ekran (np. 1920x1080, w oknie 1280x720).
- FOV Hor+: widok poszerza się na boki, obraz nie jest rozciągany.
- Menu i HUD zostają w wyśrodkowanym obszarze 4:3 (ikony w rogach można przyczepić do krawędzi ekranu).
- Skaluje tekst 2D (czat, etykiety przycisków, górny log, lobby multiplayer) z rozdzielczością.
- Wszystko dzieje się w pamięci – żadne pliki gry nie są zmieniane.

### Instalacja
1. Zainstaluj ASI Loader (`d3d9.dll` obok `SimTractor.exe`).
2. Skopiuj `widescreen.asi` i `widescreen.ini` do folderu `mods` w grze.
3. Edytuj `widescreen.ini`: `Width`/`Height` (pełny ekran), `WindowWidth`/`WindowHeight` (okno).
4. Uruchom grę.

### Odinstalowanie
Usuń `mods\widescreen.asi` i `mods\widescreen.ini`.

### Ustawienia (`widescreen.ini`)
Opis kluczy jak w tabeli powyżej. Formaty wpisów `TextScaleList` są opisane w samym ini.

### Rozwiązywanie problemów
- Ustaw `Log=1` (i `LogUi=1` przy problemach z UI/tekstem) i wyślij `widescreen.log`.
- Jeśli log mówi o innej wersji pliku – wspierana jest tylko V3.66f z oryginalnymi plikami.
- Tekst za duży/mały: edytuj `TextScale` / `TextScaleList`.
- Znany drobiazg: kilka białych linii w lobby multiplayer nie jest przesuniętych.

### Kompilacja
Otwórz **x86 Native Tools Command Prompt for VS** (32-bitowy) w folderze projektu:
```
cl /nologo /LD /O2 /MT src\widescreen.cpp /Fe:mods\widescreen.asi user32.lib
```
albo uruchom `build.bat`.

## ☕ Wsparcie
Jeśli moje mody Ci się przydały, możesz postawić mi kawę: [ko-fi.com/gbka1](https://ko-fi.com/gbka1)
