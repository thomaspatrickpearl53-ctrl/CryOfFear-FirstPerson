# Development notes

Things learned while building this, for anyone working on the code.

## Game facts (Cry of Fear 1.6, Steam)
- **Engine:** GoldSrc, with a closed-source client and server.
- **Client exports:** `cryoffear\cl_dlls\client.dll` uses HL SDK-style C exports (41 of them, no `F` export). Its other exports are VGUI C++ templates that nothing imports, so the wrapper doesn't forward them.
- **Original client:** 1,754,112 bytes.
- **Simon is `models/cutscene/player.mdl`.** It has 36 Bip01 bones and HL-style sequences: `idle`, `walk`, `run`, `crawl`, `crouch_idle` and `jump`. Walk and run have linear movement, which the mod uses to match leg speed to ground speed. `models/player.mdl` is a different character (the cop), not Simon.
- **Player speeds** (from `skill.cfg`): 45 with two-handed weapons, 75 with one-handed, 125 sprinting.
- **The flashlight / phone light** is signalled by the user message `FlashFlags`: 2 bytes, bit 0 of byte 0 = light on. Not by `EF_DIMLIGHT`.
- **Intro camera:** the intro views through a camera entity (viewentity 78). In normal play the viewentity is 1.
- **Where settings are saved:** archived cvars go to `Cry of Fear\config.cfg`, in the root folder, not in `cryoffear\`.
- **Built-in blur:** Cry of Fear's own screen blur (`gl_screenblur`, 32×24 steps) looks blocky, and sharpening exaggerates it.

## Engine and renderer gotchas
1. **Draw hooks don't reach Cry of Fear's renderer.** Hooking `IEngineStudio.StudioDrawPoints` has no effect, so the head and arms are hidden by writing a patched copy of the model (`models/fpbody/...`) instead.
2. **Loading models.** `CL_LoadModel` only finds models the server precached; `IEngineStudio.Mod_ForName(name, 0)` loads any model.
3. **Viewmodel depth.** The viewmodel is drawn into the first 30% of the depth range. Post-processing uses that to tell weapon pixels apart.
4. **Readbacks stall.** Reading back from the GPU synchronously (`glReadPixels`) stalls the frame. Readbacks go through PBOs and are used two frames later. Uniform locations are cached because every `glGetUniformLocation` is a driver round trip.
5. **Wet-floor reflections** (`cl_pp_ssr`) smear on Cry of Fear's cluttered maps, so they're off by default.
6. **Engine table size.** `Cvar_Set` sits late in `cl_enginefunc_t` and may be missing on old engine builds, so `Cvar_SetValue` is used to load numeric settings.

## Testing
- `test/pptest.cpp` builds all shaders and benchmarks a 1080p frame in a hidden GL window. On an RTX 3050 it takes about 2.5–3.5 ms with everything on.
- In game, `fpbody.log` records model loading, visibility changes, the light messages and, every 10 s, the fps and effect cost.
