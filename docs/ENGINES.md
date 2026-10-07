# Engines: original Cry of Fear vs Cry of Fear: Enhanced

The mod works on both:
- **The original Cry of Fear engine** (GoldSrc), exactly as Steam installs the game.
- **[Cry of Fear: Enhanced](https://github.com/hajdawery/cof-enhanced)**, a community patch that runs the game on the Xash3D engine. Tested with 0.4.0-test4.

`install.bat` detects which one you have and sets things up for it. This page covers what's different and what to do when you switch.

## What you get on each engine

| | Original engine | Cry of Fear: Enhanced |
|---|---|---|
| Body, camera, weapon motion | yes | yes |
| Graphics effects | yes | yes |
| Typical fps (RTX 3050, 1080p, all effects) | 60–100 (engine capped at `fps_max`) | about 200 |
| F8 spawn/cheat menu | yes, this mod's cheats | yes, Enhanced's cheats |
| Fists mode | yes | yes |
| Files the mod replaces | `client.dll` and `hl.dll` | `client.dll` only |

### Cheats in the F8 menu
**Both engines:** give any weapon, ammo or item, and spawn any monster.

**Original engine** (cheats from this mod's `hl.dll`):
- god mode, noclip, no target, full health;
- infinite ammo: it learns your weapon's magazine from your first two shots. If it ever locks onto the wrong number, use "Infinite ammo: re-learn".

**Cry of Fear: Enhanced** (its own cheats):
- no damage, infinite ammo, infinite stamina, noclip, fly, no target, no drowning, unlock doors, night vision;
- the menu turns on `sv_cheats` for you;
- Enhanced's full list, including endings and tapes, is in its `cof-enhanced\CHEATS.md`.

Enhanced only enables its cheats if the game's `hl.dll` is the original. That's why the mod doesn't install its own `hl.dll` there.

## If you change engines

### You install Cry of Fear: Enhanced *after* this mod
Run this mod's **`install.bat` again**. It notices Enhanced and puts the original `hl.dll` back, so Enhanced's cheats work. Until you do, Enhanced's cheats report the game as "not recognised".

### You remove Cry of Fear: Enhanced
Run Enhanced's own uninstaller first, then run this mod's **`install.bat` again**. On the original engine it installs the mod's `hl.dll`, so the F8 menu's cheats work.

### Steam verified or updated the game
Steam puts back the original `client.dll` (and `hl.dll`). Run **`install.bat`** again. Your mod settings are kept.

## Engine-specific notes
- **Flashlight haze:** on Enhanced, open areas let the beam reach further. It's capped so it can't turn white, but if it's still too strong for your taste, lower `cl_pp_volumetric`.
- **Background recorders:** Medal or Discord clips can tank the original engine's fps whenever the mouse moves. Close them if you see that. `fp_bench` helps tell whether the mod is to blame.
- **Running as:** with Enhanced the game runs as `CoFLaunchApp.exe`, not `cof.exe`. The mod's scripts check for both before touching files.
- **Detecting Enhanced:** `install.bat` looks for `xash.dll` in the game folder. Enhanced's uninstaller leaves its `cof-enhanced` folder behind, and that alone doesn't count.
- **A second copy of the game** (for example one engine in Steam's folder and the other elsewhere): Steam and `cof.exe` always start the copy in Steam's library. Start the other copy with its own `Play Modded.bat`.

## Linux and Steam Deck (Proton)
Cry of Fear is a Windows game; on Linux it runs through Steam's **Proton** (Properties → Compatibility → force a Proton version). The mod runs inside it like the game's own DLLs.

1. Extract the `CoF-FirstPerson` folder anywhere (inside the Cry of Fear folder is easiest).
2. In a terminal in that folder: `chmod +x *.sh && ./install.sh`
   It finds the game in your Steam libraries (normal, Flatpak, Snap and SD-card libraries), or pass the folder: `./install.sh "/path/to/Cry of Fear"`.
3. Start Cry of Fear from Steam.

`install.sh` also applies the **Wine/Proton graphics fix** on the original engine. Cry of Fear's own `opengl32.dll` (its renderer wrapper) clashes with Wine's, and gives up on drivers that lack an old function. The fix uses it as `opengp32.dll` with that check skipped, and points the engine and client at it. It's the same 14-byte change as the community "CoF fix" zip, made to your own files by `tools/cof_glfix.py` (Python 3), which checks every file before and after. `--no-glfix` skips it; `python3 tools/cof_glfix.py "<game>" --undo` undoes it. The mod draws with whichever renderer the engine loaded.

### Steam Deck controls
- **Can't move?** Pick a controller layout for Cry of Fear in Steam (Steam button → Controller settings) so the sticks and buttons are mapped to the game's movement keys. The mod's menu works with any layout, but walking needs one.
- **F8 menu:** press **both sticks in (L3 + R3)** to open and close it. Then: D-pad or left stick to move, **A** to pick, **left / right** or **X** to change a setting, **B** to go back, **LB / RB** to switch tabs, **LT / RT** for the game tabs on Maps and Player, **Y** for Games / Done, **Start** to close. The right trackpad or stick still moves the mouse cursor. While the menu is open you stand still.
- It reads the controller through Steam Input's Xbox controller, so it works with any of Steam's controller layouts. Turn it off with `cl_fpmenu_pad 0` (F8 → Settings) if you use L3 + R3 together in the game. You can also map a back button (L4/R4) to **F8** in Steam's controller settings.
- **Graphics:** on a Steam Deck the mod switches to lighter effects by itself the first time it runs. F8 → Graphics → **Preset: full** puts the PC defaults back, **Preset: Steam Deck** the lighter ones again.

`./uninstall.sh` and `./remove_everything.sh` work like their `.bat` versions (`remove_everything.sh` also undoes the graphics fix). The Source map converter needs the Half-Life SDK's Windows compilers, so it's Windows only for now.

## Removing everything
`remove_everything.bat` works on both engines. It restores the original `client.dll` and `hl.dll` and deletes the mod's settings, but doesn't touch Cry of Fear: Enhanced. Use Enhanced's own uninstaller for that.
