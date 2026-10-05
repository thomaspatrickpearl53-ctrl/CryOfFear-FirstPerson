// Cry of Fear server-side cheats: a wrapper hl.dll around the original server
// code (renamed hl_cof.dll). Every export is forwarded (exports_gen.cpp); three
// are intercepted:
//
//   GiveFnptrsToDll   keep the engine API; hand the original a copy whose
//                     NameForFunction/FunctionFromName resolve against the
//                     ORIGINAL dll's exports, so save games keep working
//   GetEntityAPI(2)   wrap ClientCommand (new fp_* commands) and StartFrame
//
// Commands (type in the console or use the fp_menu menu):
//   fp_give <classname>    weapon_*, ammo_*, item_*
//   fp_spawn <classname>   monster_* in front of you
//   fp_god / fp_noclip / fp_notarget / fp_infammo / fp_heal
// sv_cheats is also switched on at map start, which unlocks the engine's own
// god / noclip / notarget.

#include "extdll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

// Cry of Fear's entvars_t has one extra field after light_level (cof\progdefs.h,
// which build.bat puts first on the include path). Writing with the plain SDK
// layout lands every field from sequence on 4 bytes early - e.g. spawnflags
// into groundentity, which crashed the engine.
static_assert(offsetof(entvars_t, sequence) == 0x12C, "Cry of Fear entvars_t layout");
static_assert(offsetof(entvars_t, groundentity) == 0x1A0, "Cry of Fear entvars_t layout");
static_assert(offsetof(entvars_t, flags) == 0x1A8, "Cry of Fear entvars_t layout");
static_assert(offsetof(entvars_t, euser4) == 0x2A4, "Cry of Fear entvars_t layout");
static_assert(offsetof(edict_t, v) == 0x80, "edict_t layout");

extern "C" const int g_numExports;
extern "C" const char *const g_exportNames[];
extern "C" void *g_exportPtrs[];

static HMODULE        g_self, g_orig;
static enginefuncs_t  g_engfuncs;            // the real engine API
static unsigned char  g_engCopy[sizeof(enginefuncs_t) + 512];   // what the original dll gets
static globalvars_t  *gpGlobals;
static DLL_FUNCTIONS  g_dll;                 // the original dll's entity API

#define STRING(offset) ((const char *)(gpGlobals->pStringBase + (unsigned int)(offset)))

// ---------------------------------------------------------------------------
// Loading the original
// ---------------------------------------------------------------------------

struct FuncName { uint32 addr; const char *name; };
static FuncName *g_byAddr;
static int       g_byAddrCount;

static int CompareFuncName(const void *a, const void *b)
{
	uint32 x = ((const FuncName *)a)->addr, y = ((const FuncName *)b)->addr;
	return x < y ? -1 : (x > y ? 1 : 0);
}

extern "C" void FpServer_EnsureLoaded(void)
{
	if (g_orig)
		return;
	char path[MAX_PATH];
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	strcpy(slash ? slash + 1 : path, "hl_cof.dll");
	g_orig = LoadLibraryA(path);
	if (!g_orig)
	{
		MessageBoxA(NULL, "fpserver: could not load cl_dlls\\hl_cof.dll (the original Cry of Fear server code).",
			"Cry of Fear mod", MB_ICONERROR);
		ExitProcess(1);
	}
	g_byAddr = (FuncName *)calloc(g_numExports, sizeof(FuncName));
	for (int i = 0; i < g_numExports; i++)
	{
		g_exportPtrs[i] = (void *)GetProcAddress(g_orig, g_exportNames[i]);
		if (g_exportPtrs[i])
		{
			g_byAddr[g_byAddrCount].addr = (uint32)g_exportPtrs[i];
			g_byAddr[g_byAddrCount].name = g_exportNames[i];
			g_byAddrCount++;
		}
	}
	qsort(g_byAddr, g_byAddrCount, sizeof(FuncName), CompareFuncName);
}

// Save/restore stores think/touch/use functions by export name. The engine only
// knows OUR export table (whose addresses are thunks), so resolve against the
// original dll instead: same names, real addresses.
static uint32 W_FunctionFromName(const char *name)
{
	FARPROC p = GetProcAddress(g_orig, name);
	return p ? (uint32)p : g_engfuncs.pfnFunctionFromName(name);
}

static const char *W_NameForFunction(uint32 function)
{
	int lo = 0, hi = g_byAddrCount - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (g_byAddr[mid].addr == function) return g_byAddr[mid].name;
		if (g_byAddr[mid].addr < function) lo = mid + 1; else hi = mid - 1;
	}
	return g_engfuncs.pfnNameForFunction(function);
}

// ---------------------------------------------------------------------------
// Cheats
// ---------------------------------------------------------------------------

static bool  g_god, g_infAmmo;
static float g_lastTime = -1.0f;

static void Print(edict_t *player, const char *fmt, ...)
{
	char msg[256];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(msg, sizeof(msg) - 2, fmt, ap);
	va_end(ap);
	msg[sizeof(msg) - 2] = 0;
	strcat(msg, "\n");
	g_engfuncs.pfnClientPrintf(player, print_console, msg);
	g_engfuncs.pfnClientPrintf(player, print_center, msg);
}

static bool StartsWith(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

static edict_t *Create(const char *classname, const float *origin, float yaw)
{
	edict_t *e = g_engfuncs.pfnCreateNamedEntity(g_engfuncs.pfnAllocString(classname));
	if (!e || e->free)
		return NULL;
	e->v.origin[0] = origin[0]; e->v.origin[1] = origin[1]; e->v.origin[2] = origin[2];
	e->v.angles[1] = yaw;
	if (g_dll.pfnSpawn(e) == -1)
	{
		g_engfuncs.pfnRemoveEntity(e);
		return NULL;
	}
	return e;
}

// Has the item been taken: removed, hidden, or attached to the player?
static bool PickedUp(edict_t *e, edict_t *player)
{
	return e->free || (e->v.flags & FL_KILLME) || (e->v.effects & EF_NODRAW) ||
		e->v.owner == player || e->v.aiment == player || e->v.movetype == MOVETYPE_FOLLOW;
}

static void Give(edict_t *player, const char *classname)
{
	if (!StartsWith(classname, "weapon_") && !StartsWith(classname, "ammo_") && !StartsWith(classname, "item_"))
	{
		Print(player, "fp_give: only weapon_*, ammo_* and item_* (got '%s')", classname);
		return;
	}
	edict_t *e = Create(classname, player->v.origin, player->v.angles[1]);
	if (!e)
	{
		Print(player, "fp_give: unknown item '%s'", classname);
		return;
	}
	e->v.spawnflags |= (1 << 30);   // SF_NORESPAWN
	// Pick it up exactly once: touch, like the game's own GiveNamedItem, and "use"
	// (how Cry of Fear picks up most things) only if the touch left it lying
	// there. Picking a weapon up twice puts it in the inventory list twice,
	// which crashes the engine when it's drawn.
	g_dll.pfnTouch(e, player);
	if (!PickedUp(e, player))
		g_dll.pfnUse(e, player);
	Print(player, "Gave %s", classname + (strchr(classname, '_') ? strchr(classname, '_') - classname + 1 : 0));
}

static void SpawnMonster(edict_t *player, const char *classname)
{
	if (!StartsWith(classname, "monster_"))
	{
		Print(player, "fp_spawn: only monster_* (got '%s')", classname);
		return;
	}
	// A spot in front of the player, at their height; monsters drop to the floor themselves.
	g_engfuncs.pfnMakeVectors(player->v.v_angle);
	float eye[3], end[3], spot[3];
	for (int i = 0; i < 3; i++)
	{
		eye[i] = player->v.origin[i] + player->v.view_ofs[i];
		end[i] = eye[i] + gpGlobals->v_forward[i] * 192.0f;
	}
	TraceResult tr;
	g_engfuncs.pfnTraceLine(eye, end, 0, player, &tr);
	for (int i = 0; i < 3; i++)
		spot[i] = tr.vecEndPos[i] - gpGlobals->v_forward[i] * 40.0f;
	spot[2] = player->v.origin[2];
	edict_t *e = Create(classname, spot, player->v.angles[1] + 180.0f);
	if (!e)
		Print(player, "fp_spawn: couldn't spawn '%s'", classname);
	else
		Print(player, "Spawned %s", classname + 8);
}

// ---------------------------------------------------------------------------
// Infinite ammo. Cry of Fear's weapon classes aren't public, so the magazine
// counter is found by watching the active weapon: the value that drops by
// exactly 1 each time a shot is fired. Once found (two shots), it is held at
// its full value. The learned offset is shared by all weapons, which use the
// same base class.
// ---------------------------------------------------------------------------

#define SCAN_INTS 256                         // first 1 KB of each weapon's private data
static int  g_clipOffset = -1;               // int index of the magazine counter
static int  g_hits[SCAN_INTS];
static int  g_snap[2048][SCAN_INTS];         // per entity index: last frame's private data
static bool g_haveSnap[2048];
static int  g_fullClip[2048];                // per entity index: full magazine seen

static void ResetAmmoTracking(void)
{
	memset(g_haveSnap, 0, sizeof(g_haveSnap));
	memset(g_fullClip, 0, sizeof(g_fullClip));
	memset(g_hits, 0, sizeof(g_hits));
}

static bool ReadInts(void *base, int *out)
{
	__try { memcpy(out, base, SCAN_INTS * sizeof(int)); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void LoadLearnedOffset(void)
{
	char path[MAX_PATH];
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	strcpy(slash ? slash + 1 : path, "..\\fpcheats.cfg");
	FILE *f = fopen(path, "r");
	if (f) { if (fscanf(f, "clip_offset %d", &g_clipOffset) != 1) g_clipOffset = -1; fclose(f); }
}

static void SaveLearnedOffset(void)
{
	char path[MAX_PATH];
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	strcpy(slash ? slash + 1 : path, "..\\fpcheats.cfg");
	FILE *f = fopen(path, "w");
	if (f) { fprintf(f, "clip_offset %d\n", g_clipOffset); fclose(f); }
}

static void InfiniteAmmo(edict_t *player)
{
	bool firing = (player->v.button & (IN_ATTACK | IN_ATTACK2)) != 0;
	int maxEnts = gpGlobals->maxEntities < 2048 ? gpGlobals->maxEntities : 2048;
	for (int idx = 1; idx < maxEnts; idx++)
	{
		edict_t *w = g_engfuncs.pfnPEntityOfEntIndex(idx);
		if (!w || w->free || w->v.owner != player || !w->pvPrivateData)
			continue;
		if (!StartsWith(STRING(w->v.classname), "weapon_"))
			continue;
		int cur[SCAN_INTS];
		if (!ReadInts(w->pvPrivateData, cur))
			continue;

		if (g_clipOffset < 0)
		{
			// Learning: only while the trigger is held. The weapon being fired is the
			// one whose values change; the others stay the same.
			if (firing && g_haveSnap[idx])
			{
				for (int i = 0; i < SCAN_INTS; i++)
					if (g_snap[idx][i] - cur[i] == 1 && cur[i] >= 0 && cur[i] < 300)
						g_hits[i]++;
				int best = -1;
				for (int i = 0; i < SCAN_INTS; i++)
					if (g_hits[i] >= 2 && (best < 0 || g_hits[i] > g_hits[best]))
						best = i;
				if (best >= 0)
				{
					g_clipOffset = best;
					g_fullClip[idx] = cur[best] + g_hits[best];
					SaveLearnedOffset();
					Print(player, "Infinite ammo locked on");
				}
			}
			memcpy(g_snap[idx], cur, sizeof(cur));
			g_haveSnap[idx] = true;
			continue;
		}

		int v = cur[g_clipOffset];
		if (v < 0 || v > 300)
			continue;                              // not a magazine for this weapon (melee etc.)
		if (v > g_fullClip[idx])
			g_fullClip[idx] = v;                   // first sight or after a reload
		if (v < g_fullClip[idx])
		{
			__try { ((int *)w->pvPrivateData)[g_clipOffset] = g_fullClip[idx]; }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
	}
}

// ---------------------------------------------------------------------------
// Wrapped entity API
// ---------------------------------------------------------------------------

static void W_ClientCommand(edict_t *player)
{
	const char *cmd = g_engfuncs.pfnCmd_Argv(0);
	const char *arg = g_engfuncs.pfnCmd_Argc() > 1 ? g_engfuncs.pfnCmd_Argv(1) : "";
	if (!_stricmp(cmd, "fp_give"))          { Give(player, arg); return; }
	if (!_stricmp(cmd, "fp_spawn"))         { SpawnMonster(player, arg); return; }
	if (!_stricmp(cmd, "fp_god"))
	{
		g_god = !g_god;
		player->v.flags = g_god ? (player->v.flags | FL_GODMODE) : (player->v.flags & ~FL_GODMODE);
		player->v.takedamage = g_god ? DAMAGE_NO : DAMAGE_AIM;
		Print(player, "God mode %s", g_god ? "ON" : "OFF");
		return;
	}
	if (!_stricmp(cmd, "fp_noclip"))
	{
		bool on = player->v.movetype != MOVETYPE_NOCLIP;
		player->v.movetype = on ? MOVETYPE_NOCLIP : MOVETYPE_WALK;
		Print(player, "Noclip %s", on ? "ON" : "OFF");
		return;
	}
	if (!_stricmp(cmd, "fp_notarget"))
	{
		player->v.flags ^= FL_NOTARGET;
		Print(player, "No target %s", (player->v.flags & FL_NOTARGET) ? "ON (monsters ignore you)" : "OFF");
		return;
	}
	if (!_stricmp(cmd, "fp_infammo"))
	{
		g_infAmmo = !g_infAmmo;
		Print(player, g_infAmmo ? (g_clipOffset >= 0 ? "Infinite ammo ON" : "Infinite ammo ON - fire two shots to lock it on")
		                        : "Infinite ammo OFF");
		return;
	}
	if (!_stricmp(cmd, "fp_infammo_reset"))
	{
		g_clipOffset = -1;
		ResetAmmoTracking();
		SaveLearnedOffset();
		Print(player, "Infinite ammo will re-learn: fire two shots");
		return;
	}
	if (!_stricmp(cmd, "fp_heal"))
	{
		player->v.health = player->v.max_health > 0 ? player->v.max_health : 100.0f;
		Print(player, "Healed");
		return;
	}
	g_dll.pfnClientCommand(player);
}

static void W_StartFrame(void)
{
	g_dll.pfnStartFrame();

	// New map (the clock starts again): unlock the engine's own cheat commands
	// and forget per-entity ammo data (entity numbers are reused).
	if (gpGlobals->time < g_lastTime || g_lastTime < 0.0f)
	{
		if (g_engfuncs.pfnCVarGetFloat("fp_unlock_cheats") != 0.0f && g_engfuncs.pfnCVarGetFloat("sv_cheats") == 0.0f)
			g_engfuncs.pfnCVarSetFloat("sv_cheats", 1.0f);
		ResetAmmoTracking();
	}
	g_lastTime = gpGlobals->time;

	edict_t *player = g_engfuncs.pfnPEntityOfEntIndex(1);
	if (!player || player->free || !player->pvPrivateData)
		return;
	if (g_god)
	{
		player->v.flags |= FL_GODMODE;
		player->v.takedamage = DAMAGE_NO;
		float full = player->v.max_health > 0 ? player->v.max_health : 100.0f;
		if (player->v.health > 0 && player->v.health < full)
			player->v.health = full;
	}
	if (g_infAmmo)
		InfiniteAmmo(player);
}

static cvar_t g_unlockCvar = { "fp_unlock_cheats", "1", FCVAR_ARCHIVE, 1.0f, NULL };

static void W_GameInit(void)
{
	g_engfuncs.pfnCVarRegister(&g_unlockCvar);
	LoadLearnedOffset();
	g_dll.pfnGameInit();
}

static void PatchTable(DLL_FUNCTIONS *t)
{
	memcpy(&g_dll, t, sizeof(g_dll));
	t->pfnClientCommand = W_ClientCommand;
	t->pfnStartFrame = W_StartFrame;
	t->pfnGameInit = W_GameInit;
}

// ---------------------------------------------------------------------------
// Exports the engine calls by name
// ---------------------------------------------------------------------------

typedef void (WINAPI *GiveFnptrsToDll_t)(enginefuncs_t *, globalvars_t *);
typedef int (*GetEntityAPI_t)(DLL_FUNCTIONS *, int);
typedef int (*GetEntityAPI2_t)(DLL_FUNCTIONS *, int *);

extern "C" void WINAPI W_GiveFnptrsToDll(enginefuncs_t *pengfuncs, globalvars_t *pGlobals)
{
	FpServer_EnsureLoaded();
	memcpy(&g_engfuncs, pengfuncs, sizeof(g_engfuncs));
	gpGlobals = pGlobals;
	memcpy(g_engCopy, pengfuncs, sizeof(g_engCopy));
	((enginefuncs_t *)g_engCopy)->pfnFunctionFromName = W_FunctionFromName;
	((enginefuncs_t *)g_engCopy)->pfnNameForFunction = W_NameForFunction;
	((GiveFnptrsToDll_t)GetProcAddress(g_orig, "GiveFnptrsToDll"))((enginefuncs_t *)g_engCopy, pGlobals);
}

extern "C" int W_GetEntityAPI(DLL_FUNCTIONS *table, int version)
{
	FpServer_EnsureLoaded();
	GetEntityAPI_t f = (GetEntityAPI_t)GetProcAddress(g_orig, "GetEntityAPI");
	int r = f ? f(table, version) : 0;
	if (r) PatchTable(table);
	return r;
}

extern "C" int W_GetEntityAPI2(DLL_FUNCTIONS *table, int *version)
{
	FpServer_EnsureLoaded();
	GetEntityAPI2_t f = (GetEntityAPI2_t)GetProcAddress(g_orig, "GetEntityAPI2");
	int r = f ? f(table, version) : 0;
	if (r) PatchTable(table);
	return r;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = hinst;
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}
