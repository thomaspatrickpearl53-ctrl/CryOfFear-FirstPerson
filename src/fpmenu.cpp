// In-game spawn / cheat / settings menu (console command: fp_menu, bound to F8).
//
// Two styles (Settings > Menu style, cvar cl_fpmenu_style):
//   Classic: small list, number keys pick, 8/9 page, 0 back, Esc closes.
//   Big:     large panel with tabs, mouse controlled. The camera stays still:
//            whatever the mouse would turn the view by moves the cursor instead
//            (FpMenu_CreateMove), so it works on any engine. Left click picks /
//            steps a setting up, right click steps it down, the wheel scrolls.
//
// Spawning and cheats go to the server: this mod's hl.dll on the original
// engine (fp_give, fp_spawn, fp_god, ...), Cry of Fear: Enhanced's own cheats
// when running on its engine (give, ent_create, cof_nodamage, ...).

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cvardef.h"
#include "usercmd.h"
#include "in_buttons.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

enum ItemType { ACTION, OPEN, SETTING };

struct Item
{
	ItemType    type;
	const char *label;
	const char *cmd;         // ACTION: command; OPEN: page id; SETTING: cvar name
	const char *values;      // SETTING: space separated values to step through
	const char *names;       // SETTING: matching '|' separated names ("" = show the number)
};

#define A(label, cmd)                 { ACTION, label, cmd, NULL, NULL }
#define O(label, page)                { OPEN, label, page, NULL, NULL }
#define S(label, cvar, values, names) { SETTING, label, cvar, values, names }

static const Item kWeapons[] = {
	S("Fists mode (nightstick becomes fists, both buttons punch)", "cl_fists", "0 1", "Off|On"),
	A("Glock", "fp_give weapon_glock"), A("VP70", "fp_give weapon_vp70"), A("P345", "fp_give weapon_p345"),
	A("Revolver", "fp_give weapon_revolver"), A("TMP", "fp_give weapon_tmp"), A("MP5", "fp_give weapon_mp5"),
	A("M16", "fp_give weapon_m16"), A("FAMAS", "fp_give weapon_famas"), A("G43", "fp_give weapon_g43"),
	A("Rifle", "fp_give weapon_rifle"), A("Shotgun", "fp_give weapon_shotgun"), A("Switchblade", "fp_give weapon_switchblade"),
	A("Branch", "fp_give weapon_branch"), A("Axe", "fp_give weapon_axe"), A("Sledgehammer", "fp_give weapon_sledgehammer"),
	A("Nightstick", "fp_give weapon_nightstick"), A("Syringe (morphine)", "fp_give weapon_syringe"), A("Flare", "fp_give weapon_flare"),
	A("Lantern", "fp_give weapon_lantern"), A("Flashlight", "fp_give weapon_flashlight"), A("Phone", "fp_give weapon_mobile"),
	A("Camera", "fp_give weapon_camera"),
};
static const Item kAmmo[] = {
	A("Glock ammo", "fp_give ammo_glock"), A("VP70 ammo", "fp_give ammo_vp70"), A("P345 ammo", "fp_give ammo_p345"),
	A("Revolver ammo", "fp_give ammo_revolver"), A("TMP ammo", "fp_give ammo_tmp"), A("M16 ammo", "fp_give ammo_m16"),
	A("G43 ammo", "fp_give ammo_g43"), A("Rifle ammo", "fp_give ammo_rifle"), A("Shotgun shells", "fp_give ammo_shells"),
};
static const Item kItems[] = {
	A("Glock tactical light", "fp_give item_glocktaclight"), A("Night vision", "fp_give item_nightvision"),
	A("Phone battery", "fp_give item_phonebattery"), A("Flashlight battery", "fp_give ammo_flashlightbattery"),
};
static const Item kMonsters[] = {
	A("Slower", "fp_spawn monster_slower"), A("Slower (variant)", "fp_spawn monster_slower3"),
	A("Slower (stuck)", "fp_spawn monster_slowerstuck"), A("Faster", "fp_spawn monster_faster"),
	A("Faceless", "fp_spawn monster_faceless"), A("Faceless (variant)", "fp_spawn monster_facelessv"),
	A("Sewmo", "fp_spawn monster_sewmo"), A("Krypande (crawler)", "fp_spawn monster_krypande"),
	A("Running crazy", "fp_spawn monster_rcrazy"), A("Crazy woman", "fp_spawn monster_crazybitch"),
	A("Saw crazy", "fp_spawn monster_sawcrazy"), A("Sawrunner", "fp_spawn monster_sawrunner"),
	A("Taller", "fp_spawn monster_taller"), A("Stranger", "fp_spawn monster_stranger"),
	A("Suicider", "fp_spawn monster_suicider"), A("Spitter", "fp_spawn monster_spitter"),
	A("Watro", "fp_spawn monster_watro"), A("Child", "fp_spawn monster_child"),
	A("Baby", "fp_spawn monster_baby"), A("Nerd", "fp_spawn monster_nerd"),
	A("Crab", "fp_spawn monster_crab"), A("Twitcher", "fp_spawn monster_twitcher"),
	A("Twitcher 2", "fp_spawn monster_twitcher2"), A("Twitcher 3", "fp_spawn monster_twitcher3"),
	A("Twitcher 4", "fp_spawn monster_twitcher4"), A("Ruben", "fp_spawn monster_ruben"),
	A("Book Simon", "fp_spawn monster_booksimon"), A("Book Simon (sledgehammer)", "fp_spawn monster_booksimonsledgehammer"),
	A("Chainsaw boss", "fp_spawn monster_bosschainsaw"), A("Doctor boss", "fp_spawn monster_doctorboss"),
	A("Sewer boss", "fp_spawn monster_sewerboss"), A("Roof boss", "fp_spawn monster_roofboss"),
};
// Cry of Fear's own costumes, worn without unlocking them: the same "setclothes"
// command the game's wardrobe menu sends (numbers in the game's unlock order).
static const Item kClothes[] = {
	A("Simon's normal clothes", "setclothes 0"),
	A("David Leatherhoff suit", "setclothes 1"), A("ModDB hoodie", "setclothes 2"),
	A("Hello Kitty suit", "setclothes 3"), A("Afraid of Monsters suit", "setclothes 4"),
	A("Camouflage hoodie", "setclothes 5"), A("Half-Life Creations hoodie", "setclothes 6"),
	A("Black Metal suit", "setclothes 7"), A("Team Psykskallar hoodie", "setclothes 8"),
	A("Fuck Anime suit", "setclothes 9"), A("Sick Simon suit", "setclothes 10"),
	A("AoM Twitcher suit", "setclothes 11"),
	A("Your own hoodie (models\\costumes\\custom_hoodie.tga)", "setclothes 12 custom_hoodie.tga"),
};
// Original engine: this mod's server wrapper (hl.dll).
static const Item kCheatsGoldSrc[] = {
	A("God mode (on/off)", "fp_god"), A("Noclip (on/off)", "fp_noclip"),
	A("No target - monsters ignore you (on/off)", "fp_notarget"), A("Infinite ammo (on/off)", "fp_infammo"),
	A("Full health", "fp_heal"), A("Infinite ammo: re-learn", "fp_infammo_reset"),
};
// Cry of Fear: Enhanced: its own cheats. "toggle:" actions remember their state.
static const Item kCheatsEnhanced[] = {
	A("No damage", "toggle:cof_nodamage"), A("Infinite ammo", "toggle:cof_infammo"),
	A("Infinite stamina", "toggle:cof_infstamina"), A("Noclip (on/off)", "noclip"), A("Fly (on/off)", "fly"),
	A("No target - monsters ignore you (on/off)", "notarget"), A("No drowning", "toggle:cof_nodrown"),
	A("Unlock doors on this level", "cof_unlockdoors"), A("Night vision", "cof_nightvision 1"),
};

#define OFF_LOW_MED_HIGH "Off|Low|Medium|High"
static const Item kGraphics[] = {
	S("All graphics effects", "cl_pp", "0 1", "Off|On"),
	S("Ambient occlusion", "cl_pp_ssao", "0 0.3 0.6 1 1.5", "Off|Low|Medium|High|Max"),
	S("Contact shadows", "cl_pp_contact", "0 0.4 0.8 1.2", OFF_LOW_MED_HIGH),
	S("Bounce light", "cl_pp_gi", "0 0.3 0.6 1", OFF_LOW_MED_HIGH),
	S("Bloom", "cl_pp_bloom", "0 0.35 0.7 1 1.5", "Off|Low|Medium|High|Max"),
	S("Light shafts", "cl_pp_shafts", "0 0.35 0.7 1", OFF_LOW_MED_HIGH),
	S("Flashlight haze", "cl_pp_volumetric", "0 0.5 1 1.5 2.5", "Off|Low|Medium|High|Max"),
	S("Flashlight shadows", "cl_pp_flashshadows", "0 0.4 0.8 1.2", OFF_LOW_MED_HIGH),
	S("Dust in the beam", "cl_pp_dust", "0 0.5 1 2", OFF_LOW_MED_HIGH),
	S("Motion blur", "cl_pp_motionblur", "0 0.25 0.5 1", OFF_LOW_MED_HIGH),
	S("Aiming blur", "cl_pp_dof", "0 1", "Off|On"),
	S("Distance fog", "cl_pp_fog", "0 0.4 0.8", "Off|Light|Thick"),
	S("Brightness", "cl_pp_exposure", "0.8 1 1.2 1.5", "Dark|Normal|Bright|Brighter"),
	S("Eye adaptation", "cl_pp_adapt", "0 1", "Off|On"),
	S("Sharpening", "cl_pp_sharpen", "0 0.4 1", "Off|Low|High"),
	S("Anti-aliasing", "cl_pp_aa", "0 1", "Off|On"),
	S("Texture sharpness (next map)", "cl_pp_aniso", "0 4 8 16", "Off|4x|8x|16x"),
	S("Film grain", "cl_pp_grain", "0 0.02 0.04 0.08", OFF_LOW_MED_HIGH),
	S("Vignette", "cl_pp_vignette", "0 0.25 0.5", "Off|Low|High"),
	S("Lens dirt", "cl_pp_lensdirt", "0 0.3 0.6", "Off|Low|High"),
	S("Colour fringing", "cl_pp_ca", "0 0.4 0.8", "Off|Low|High"),
	S("Damage screen effect", "cl_pp_hurt", "0 1", "Off|On"),
};
static const Item kCamera[] = {
	S("Camera effects", "cl_fpcam", "0 1", "Off|On"),
	S("Head bob", "cl_fpcam_bob", "0 0.5 1 1.5", OFF_LOW_MED_HIGH),
	S("Footstep impacts", "cl_fpcam_impact", "0 0.5 1 1.5", OFF_LOW_MED_HIGH),
	S("View lag on turns", "cl_fpcam_lag", "0 0.5 1", "Off|Low|Normal"),
	S("Lean into turns", "cl_fpcam_tilt", "0 0.5 1 1.5", OFF_LOW_MED_HIGH),
	S("Look up/down limit", "cl_fpcam_pitchmax", "70 80 89", ""),
	S("Wider view when sprinting (%)", "cl_fpfov_sprint", "0 3 6 10", ""),
	S("Zoom when aiming (%)", "cl_fpfov_ads", "0 4 8 15", ""),
	S("Field-of-view changes", "cl_fpfov", "0 1", "Off|On"),
};
static const Item kHands[] = {
	S("Hand/weapon effects", "cl_fpvm", "0 1", "Off|On"),
	S("Weapon sway", "cl_fpvm_sway", "0 0.5 1 1.5", OFF_LOW_MED_HIGH),
	S("Breathing", "cl_fpvm_breath", "0 1 2", "Off|Normal|Strong"),
	S("Sprint / jump / fire movement", "cl_fpvm_action", "0 1", "Off|On"),
	S("Fists mode (nightstick becomes fists, both buttons punch)", "cl_fists", "0 1", "Off|On"),
};
static const Item kBody[] = {
	S("Body and legs", "cl_fpbody", "0 1", "Off|On"),
	S("Body arms", "cl_fpbody_arms", "0 1", "Hidden|Shown"),
	S("Body distance behind camera", "cl_fpbody_offset", "10 15 20 25", ""),
};
static const Item kMenu[] = {
	S("Menu style", "cl_fpmenu_style", "0 1", "Classic (number keys)|Big (mouse)"),
};
static const Item kSettings[] = {
	O("Graphics", "graphics"), O("Camera and field of view", "camera"), O("Hands and weapon", "hands"),
	O("Body", "body"), O("Menu style", "menu"),
};

struct Page { const char *id; const char *title; const Item *items; int count; const char *parent; };
#define PAGE(id, title, arr, parent) { id, title, arr, (int)(sizeof(arr) / sizeof(arr[0])), parent }
static const Item kMain[] = {
	O("Weapons", "weapons"), O("Ammo", "ammo"), O("Items", "items"), O("Monsters", "monsters"),
	O("Clothes", "clothes"), O("Cheats", "cheats"), O("Settings", "settings"),
};
static const Page kPages[] = {
	PAGE("main", "SPAWN MENU", kMain, NULL),
	PAGE("weapons", "Weapons", kWeapons, "main"),
	PAGE("ammo", "Ammo", kAmmo, "main"),
	PAGE("items", "Items", kItems, "main"),
	PAGE("monsters", "Monsters", kMonsters, "main"),
	PAGE("clothes", "Clothes", kClothes, "main"),
	PAGE("cheats", "Cheats", kCheatsGoldSrc, "main"),
	PAGE("settings", "Settings", kSettings, "main"),
	PAGE("graphics", "Graphics", kGraphics, "settings"),
	PAGE("camera", "Camera and field of view", kCamera, "settings"),
	PAGE("hands", "Hands and weapon", kHands, "settings"),
	PAGE("body", "Body", kBody, "settings"),
	PAGE("menu", "Menu style", kMenu, "settings"),
};
static const int kNumPages = sizeof(kPages) / sizeof(kPages[0]);
static const Page kCheatsPageEnhanced = PAGE("cheats", "Cheats (Cry of Fear: Enhanced)", kCheatsEnhanced, "main");

// Tabs of the big menu, in order.
static const char *const kTabs[] = { "weapons", "ammo", "items", "monsters", "clothes", "cheats", "graphics", "camera", "hands", "body", "menu" };
static const char *const kTabNames[] = { "Weapons", "Ammo", "Items", "Monsters", "Clothes", "Cheats", "Graphics", "Camera", "Hands", "Body", "Menu" };
static const int kNumTabs = sizeof(kTabs) / sizeof(kTabs[0]);

static bool Enhanced(void) { return GetModuleHandleA("xash.dll") != NULL; }

static const Page *FindPage(const char *id)
{
	if (!strcmp(id, "cheats") && Enhanced())
		return &kCheatsPageEnhanced;
	for (int i = 0; i < kNumPages; i++)
		if (!strcmp(kPages[i].id, id))
			return &kPages[i];
	return &kPages[0];
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static cvar_t *s_bound, *s_style;
static bool   s_open;
static const Page *s_page;
static int    s_offset;               // first item shown
static float  s_cursorX, s_cursorY;   // big menu cursor (pixels)
static float  s_frozen[3];            // view angles held while the big menu is open
static bool   s_haveFrozen;
static int    s_scrW = 1920, s_scrH = 1080;

static struct { const char *name; bool on; } s_toggles[8];

static bool *ToggleState(const char *name)
{
	for (int i = 0; i < 8; i++)
	{
		if (s_toggles[i].name && !strcmp(s_toggles[i].name, name)) return &s_toggles[i].on;
		if (!s_toggles[i].name) { s_toggles[i].name = name; s_toggles[i].on = false; return &s_toggles[i].on; }
	}
	return &s_toggles[7].on;
}

static bool BigMenu(void) { return s_style && s_style->value != 0.0f; }

static void Open(const char *id)
{
	s_page = FindPage(id);
	s_offset = 0;
}

static void Cmd_Menu(void)
{
	s_open = !s_open;
	Open(BigMenu() ? "weapons" : "main");
	s_cursorX = s_scrW * 0.5f;
	s_cursorY = s_scrH * 0.5f;
	s_haveFrozen = false;
}

void FpMenu_Init(void)
{
	eng->pfnAddCommand("fp_menu", Cmd_Menu);
	s_bound = FpRegister("cl_fpmenu_bound", "0", FCVAR_ARCHIVE);
	s_style = FpRegister("cl_fpmenu_style", "1", FCVAR_ARCHIVE);   // 1 = big mouse menu
}

// ---------------------------------------------------------------------------
// Doing things
// ---------------------------------------------------------------------------

static void Send(const char *cmd)
{
	char buf[192];
	if (!Enhanced())
		_snprintf(buf, sizeof(buf), "%s\n", cmd);
	else if (!strncmp(cmd, "fp_give ", 8))
		_snprintf(buf, sizeof(buf), "sv_cheats 1; give %s\n", cmd + 8);
	else if (!strncmp(cmd, "fp_spawn ", 9))
		_snprintf(buf, sizeof(buf), "sv_cheats 1; sv_enttools_enable 1; ent_create %s\n", cmd + 9);
	else if (!strncmp(cmd, "toggle:", 7))
	{
		bool *on = ToggleState(cmd + 7);
		*on = !*on;
		_snprintf(buf, sizeof(buf), "sv_cheats 1; %s %d\n", cmd + 7, *on ? 1 : 0);
	}
	else
		_snprintf(buf, sizeof(buf), "sv_cheats 1; %s\n", cmd);
	buf[sizeof(buf) - 1] = 0;
	eng->pfnClientCmd(buf);
}

static int ParseValues(const char *list, float *out, int max)
{
	int n = 0;
	const char *p = list;
	while (*p && n < max)
	{
		char *end;
		out[n++] = (float)strtod(p, &end);
		if (end == p) break;
		p = end;
		while (*p == ' ') p++;
	}
	return n;
}

static float CvarValue(const char *name)
{
	return eng->pfnGetCvarFloat ? eng->pfnGetCvarFloat((char *)name) : 0.0f;
}

// Index of the listed value closest to the setting's current value.
static int SettingIndex(const Item &it, float *values, int *count)
{
	*count = ParseValues(it.values, values, 16);
	float cur = CvarValue(it.cmd);
	int best = 0;
	for (int i = 1; i < *count; i++)
		if (fabsf(values[i] - cur) < fabsf(values[best] - cur))
			best = i;
	return best;
}

static void SettingText(const Item &it, char *out, int size)
{
	float values[16];
	int count, idx = SettingIndex(it, values, &count);
	if (it.names && it.names[0])
	{
		const char *p = it.names;
		for (int i = 0; i < idx && p; i++)
		{
			p = strchr(p, '|');
			if (p) p++;
		}
		if (p)
		{
			const char *end = strchr(p, '|');
			int len = end ? (int)(end - p) : (int)strlen(p);
			_snprintf(out, size, "%.*s", len, p);
			out[size - 1] = 0;
			return;
		}
	}
	_snprintf(out, size, "%g", values[idx]);
}

static void StepSetting(const Item &it, int dir)
{
	float values[16];
	int count, idx = SettingIndex(it, values, &count);
	if (count <= 0) return;
	idx = (idx + dir + count) % count;
	eng->Cvar_SetValue((char *)it.cmd, values[idx]);
}

static void Activate(const Item &it, int dir)
{
	if (it.type == ACTION) Send(it.cmd);
	else if (it.type == OPEN) Open(it.cmd);
	else StepSetting(it, dir);
}

static void Back(void)
{
	if (s_page->parent) Open(s_page->parent);
	else s_open = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

#define K_ESCAPE     27
#define K_MWHEELDOWN 239
#define K_MWHEELUP   240
#define K_MOUSE1     241
#define K_MOUSE2     242

static const int kPerPage = 7;
static int BigRows(void);
static int BigHit(int *tab);

// Returns 0 when the menu used the key (so the game doesn't act on it as well).
int FpMenu_Key(int down, int keynum)
{
	if (!s_open)
		return 1;
	if (keynum == K_ESCAPE)
	{
		if (down) s_open = false;
		return 0;
	}

	if (BigMenu())
	{
		if (keynum == K_MWHEELUP || keynum == K_MWHEELDOWN)
		{
			if (down)
			{
				int rows = BigRows();
				s_offset += keynum == K_MWHEELDOWN ? 1 : -1;
				if (s_offset > s_page->count - rows) s_offset = s_page->count - rows;
				if (s_offset < 0) s_offset = 0;
			}
			return 0;
		}
		if (keynum == K_MOUSE1 || keynum == K_MOUSE2)
		{
			if (down)
			{
				int tab = -1;
				int row = BigHit(&tab);
				if (tab >= 0) Open(kTabs[tab]);
				else if (row == -2) s_open = false;                        // close button
				else if (row >= 0 && s_offset + row < s_page->count)
					Activate(s_page->items[s_offset + row], keynum == K_MOUSE1 ? 1 : -1);
			}
			return 0;
		}
		return 1;
	}

	if (keynum < '0' || keynum > '9')
		return 1;
	if (!down)
		return 0;
	int n = keynum - '0';
	if (n == 0) { Back(); return 0; }
	if (n == 8) { if (s_offset >= kPerPage) s_offset -= kPerPage; return 0; }
	if (n == 9) { if (s_offset + kPerPage < s_page->count) s_offset += kPerPage; return 0; }
	int i = s_offset + n - 1;
	if (n <= kPerPage && i < s_page->count)
		Activate(s_page->items[i], 1);
	return 0;
}

// Big menu: hold the view still and turn mouse movement into cursor movement.
void FpMenu_CreateMove(usercmd_t *cmd)
{
	if (!s_open || !BigMenu())
	{
		s_haveFrozen = false;
		return;
	}
	float ang[3];
	eng->GetViewAngles(ang);
	if (!s_haveFrozen)
	{
		VectorCopy(ang, s_frozen);
		s_haveFrozen = true;
	}
	float dyaw = ang[1] - s_frozen[1];
	while (dyaw > 180.0f) dyaw -= 360.0f;
	while (dyaw < -180.0f) dyaw += 360.0f;
	float dpitch = ang[0] - s_frozen[0];
	float pxPerDeg = s_scrH / 70.0f;
	s_cursorX -= dyaw * pxPerDeg;
	s_cursorY += dpitch * pxPerDeg;
	if (s_cursorX < 0) s_cursorX = 0;
	if (s_cursorY < 0) s_cursorY = 0;
	if (s_cursorX > s_scrW - 1) s_cursorX = (float)(s_scrW - 1);
	if (s_cursorY > s_scrH - 1) s_cursorY = (float)(s_scrH - 1);
	eng->SetViewAngles(s_frozen);
	VectorCopy(s_frozen, cmd->viewangles);
	cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void Rect(int x, int y, int w, int h, float r, float g, float b, float a)
{
	glColor4f(r, g, b, a);
	glBegin(GL_QUADS);
	glVertex2i(x, y); glVertex2i(x + w, y); glVertex2i(x + w, y + h); glVertex2i(x, y + h);
	glEnd();
}

static void BeginShapes(void)
{
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void EndShapes(void) { glPopAttrib(); }

static void Text(int x, int y, const char *s, float r, float g, float b)
{
	eng->pfnDrawSetTextColor(r, g, b);
	eng->pfnDrawConsoleString(x, y, (char *)s);
}

static int TextWidth(const char *s)
{
	int w = 0, h = 0;
	if (eng->pfnDrawConsoleStringLen) eng->pfnDrawConsoleStringLen(s, &w, &h);
	return w;
}

static void ItemLabel(const Item &it, char *out, int size)
{
	if (it.type == ACTION && !strncmp(it.cmd, "toggle:", 7))
		_snprintf(out, size, "%s  [%s]", it.label, *ToggleState(it.cmd + 7) ? "ON" : "OFF");
	else if (it.type == OPEN)
		_snprintf(out, size, "%s  >", it.label);
	else
		_snprintf(out, size, "%s", it.label);
	out[size - 1] = 0;
}

// --- Classic ---------------------------------------------------------------

static void DrawClassic(int charH)
{
	int line = charH + 6;
	int x = 40, y = s_scrH / 4, w = 470;
	static char buf[16][160];
	int n = 0;
	int pages = (s_page->count + kPerPage - 1) / kPerPage;
	if (pages > 1) _snprintf(buf[n++], 160, "%s  (%d/%d)", s_page->title, s_offset / kPerPage + 1, pages);
	else           _snprintf(buf[n++], 160, "%s", s_page->title);
	for (int i = 0; i < kPerPage && s_offset + i < s_page->count; i++)
	{
		const Item &it = s_page->items[s_offset + i];
		char label[120];
		ItemLabel(it, label, sizeof(label));
		if (it.type == SETTING)
		{
			char val[48];
			SettingText(it, val, sizeof(val));
			_snprintf(buf[n++], 160, "%d. %s: %s", i + 1, label, val);
		}
		else
			_snprintf(buf[n++], 160, "%d. %s", i + 1, label);
	}
	if (s_offset > 0) _snprintf(buf[n++], 160, "8. Previous page");
	if (s_offset + kPerPage < s_page->count) _snprintf(buf[n++], 160, "9. Next page");
	_snprintf(buf[n++], 160, s_page->parent ? "0. Back" : "0. Close");

	BeginShapes();
	Rect(x - 14, y - 12, w, n * line + 20, 0, 0, 0, 0.72f);
	Rect(x - 14, y - 12, w, 3, 0.75f, 0.12f, 0.1f, 0.9f);
	EndShapes();
	for (int i = 0; i < n; i++)
		Text(x, y + i * line, buf[i], i == 0 ? 1.0f : 0.92f, i == 0 ? 0.35f : 0.92f, i == 0 ? 0.3f : 0.92f);
}

// --- Big (mouse) -------------------------------------------------------------

struct BigLayout { int x, y, w, h, tabW, rowH, listX, listY, rows, closeX, closeY, closeS; };

static BigLayout Layout(void)
{
	BigLayout L;
	L.w = (int)(s_scrW * 0.62f); if (L.w < 700) L.w = 700;
	L.h = (int)(s_scrH * 0.70f);
	L.x = (s_scrW - L.w) / 2;
	L.y = (s_scrH - L.h) / 2;
	L.tabW = (int)(L.w * 0.22f);
	L.rowH = s_scrH / 22; if (L.rowH < 28) L.rowH = 28;
	L.listX = L.x + L.tabW + 20;
	L.listY = L.y + L.rowH + 24;
	L.rows = (L.y + L.h - 16 - L.listY) / L.rowH;
	L.closeS = L.rowH - 8;
	L.closeX = L.x + L.w - L.closeS - 10;
	L.closeY = L.y + 8;
	return L;
}

static int BigRows(void) { return Layout().rows; }

// Row under the cursor (-1 none, -2 close button); *tab = tab under the cursor or -1.
static int BigHit(int *tab)
{
	BigLayout L = Layout();
	int cx = (int)s_cursorX, cy = (int)s_cursorY;
	*tab = -1;
	if (cx >= L.closeX && cx < L.closeX + L.closeS && cy >= L.closeY && cy < L.closeY + L.closeS)
		return -2;
	if (cx >= L.x && cx < L.x + L.tabW && cy >= L.y + 12)
	{
		int t = (cy - (L.y + 12)) / L.rowH;
		if (t >= 0 && t < kNumTabs) { *tab = t; return -1; }
	}
	if (cx >= L.listX && cx < L.x + L.w - 16 && cy >= L.listY)
	{
		int r = (cy - L.listY) / L.rowH;
		if (r >= 0 && r < L.rows) return r;
	}
	return -1;
}

static void DrawBig(int charH)
{
	BigLayout L = Layout();
	int tabHover = -1;
	int rowHover = BigHit(&tabHover);
	int textOff = (L.rowH - charH) / 2;

	BeginShapes();
	Rect(L.x, L.y, L.w, L.h, 0.03f, 0.03f, 0.035f, 0.88f);                 // panel
	Rect(L.x, L.y, L.w, 3, 0.75f, 0.12f, 0.1f, 0.95f);                      // accent
	Rect(L.x, L.y, L.tabW, L.h, 0.0f, 0.0f, 0.0f, 0.35f);                   // tab column
	for (int t = 0; t < kNumTabs; t++)
	{
		int ty = L.y + 12 + t * L.rowH;
		bool current = s_page == FindPage(kTabs[t]);
		if (current)            Rect(L.x, ty, L.tabW, L.rowH - 2, 0.55f, 0.1f, 0.08f, 0.85f);
		else if (t == tabHover) Rect(L.x, ty, L.tabW, L.rowH - 2, 1, 1, 1, 0.08f);
	}
	for (int r = 0; r < L.rows && s_offset + r < s_page->count; r++)
	{
		int ry = L.listY + r * L.rowH;
		Rect(L.listX, ry, L.x + L.w - 16 - L.listX, L.rowH - 3, 1, 1, 1, r == rowHover ? 0.14f : 0.04f);
	}
	Rect(L.closeX, L.closeY, L.closeS, L.closeS, 0.75f, 0.12f, 0.1f, rowHover == -2 ? 0.95f : 0.55f);
	if (s_page->count > L.rows)                                                // scroll bar
	{
		int barH = L.rows * L.rowH;
		int knobH = barH * L.rows / s_page->count;
		int knobY = L.listY + (barH - knobH) * s_offset / (s_page->count - L.rows);
		Rect(L.x + L.w - 10, L.listY, 4, barH, 1, 1, 1, 0.08f);
		Rect(L.x + L.w - 10, knobY, 4, knobH, 0.75f, 0.12f, 0.1f, 0.9f);
	}
	EndShapes();

	Text(L.listX, L.y + 14, s_page->title, 1.0f, 0.4f, 0.35f);
	Text(L.closeX + (L.closeS - TextWidth("X")) / 2, L.closeY + (L.closeS - charH) / 2, "X", 1, 1, 1);
	for (int t = 0; t < kNumTabs; t++)
		Text(L.x + 16, L.y + 12 + t * L.rowH + textOff, kTabNames[t], 0.92f, 0.92f, 0.92f);
	for (int r = 0; r < L.rows && s_offset + r < s_page->count; r++)
	{
		const Item &it = s_page->items[s_offset + r];
		int ry = L.listY + r * L.rowH + textOff;
		char label[120];
		ItemLabel(it, label, sizeof(label));
		Text(L.listX + 14, ry, label, 0.95f, 0.95f, 0.95f);
		if (it.type == SETTING)
		{
			char val[64], shown[80];
			SettingText(it, val, sizeof(val));
			_snprintf(shown, sizeof(shown), "<  %s  >", val);
			shown[sizeof(shown) - 1] = 0;
			Text(L.x + L.w - 30 - TextWidth(shown), ry, shown, 1.0f, 0.75f, 0.45f);
		}
	}
	Text(L.listX, L.y + L.h - charH - 8,
		"Left click: pick / next    Right click: previous    Wheel: scroll    F8 / Esc: close", 0.55f, 0.55f, 0.55f);

	// Cursor: a small arrow with a dark outline.
	BeginShapes();
	int cx = (int)s_cursorX, cy = (int)s_cursorY, s = L.rowH / 2 + 4;
	glColor4f(0, 0, 0, 0.8f);
	glBegin(GL_TRIANGLES);
	glVertex2i(cx - 1, cy - 2); glVertex2i(cx - 1, cy + s + 2); glVertex2i(cx + s * 2 / 3 + 2, cy + s * 2 / 3 + 1);
	glEnd();
	glColor4f(1, 1, 1, 1);
	glBegin(GL_TRIANGLES);
	glVertex2i(cx, cy); glVertex2i(cx, cy + s); glVertex2i(cx + s * 2 / 3, cy + s * 2 / 3);
	glEnd();
	EndShapes();
}

void FpMenu_Draw(void)
{
	// First run: put the menu on F8 once (players can rebind it afterwards).
	static bool checkedBind;
	if (!checkedBind && s_bound)
	{
		checkedBind = true;
		if (s_bound->value == 0.0f)
		{
			eng->pfnClientCmd((char *)"bind F8 fp_menu\n");
			eng->Cvar_SetValue((char *)"cl_fpmenu_bound", 1.0f);
			eng->Con_Printf("fp_menu: spawn/cheat/settings menu bound to F8\n");
		}
	}
	if (!s_open)
		return;
	SCREENINFO si;
	si.iSize = sizeof(si);
	eng->pfnGetScreenInfo(&si);
	s_scrW = si.iWidth > 0 ? si.iWidth : 1920;
	s_scrH = si.iHeight > 0 ? si.iHeight : 1080;
	int charH = si.iCharHeight > 0 ? si.iCharHeight : 16;
	if (!s_page)
		Open("main");
	// The style can change from inside the menu: the big one has no main/settings list.
	if (BigMenu() && (s_page == FindPage("main") || s_page == FindPage("settings")))
		Open("graphics");
	if (BigMenu()) DrawBig(charH);
	else           DrawClassic(charH);
}
