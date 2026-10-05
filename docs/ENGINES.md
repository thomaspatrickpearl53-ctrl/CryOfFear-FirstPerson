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

## Removing everything
`remove_everything.bat` works on both engines. It restores the original `client.dll` and `hl.dll` and deletes the mod's settings, but doesn't touch Cry of Fear: Enhanced. Use Enhanced's own uninstaller for that.
