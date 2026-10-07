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
#include <deque>
#include <string>
#include <vector>

#include "mathlib.h"
#include "hud_iface.h"
#include "cvardef.h"
#include "usercmd.h"
#include "in_buttons.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void        FpChars_Scan(void);
int         FpChars_Count(int tab);
const char *FpChars_Label(int tab, int i);
std::string FpChars_Pick(int tab, int i);
void        FpChars_PickSimon(void);
std::string FpChars_Path(int tab, int i);
const char *FpChars_Simon(void);
bool        FpPreview_Draw3D(const char *path, int x, int y, int w, int h, int screenW, int screenH, float time);
int         FpProps_NumDirs(void);
const char *FpProps_DirName(int d);
int         FpProps_NumModels(int d);
const char *FpProps_ModelName(int d, int i);
int         FpProps_Count(void);
void        FpProps_Spawn(int d, int i);
void        FpProps_RemoveLast(void);
void        FpProps_RemoveAll(void);
void        FpProps_RemoveAimed(void);
void        FpMaps_Scan(void);
int         FpMaps_NumTabs(void);
const char *FpMaps_TabName(int tab);
int         FpMaps_Count(int tab);
const char *FpMaps_Name(int tab, int i);
int         FpMaps_NumGames(void);
const char *FpMaps_GameName(int i);
int         FpMaps_GameMapCount(int i);
bool        FpMaps_GameOn(int i);
void        FpMaps_ToggleGame(int i);
std::string FpMaps_Load(int tab, int i);
void        FpClothes_Select(int n);
const void *FpClothes_Part(int costume, int part, unsigned *texId, int *w, int *h, bool *own);
const char *FpClothes_PartName(int part);
int         FpClothes_NumParts(void);
void        FpClothes_Draw3D(int costume, int x, int y, int w, int h, int screenW, int screenH, float time);

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
	A("Your own hoodie (custom_hoodie.tga)", "setclothes 12 custom_hoodie.tga"),
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
	S("Turn in place (feet stay planted, step round)", "cl_fpbody_turn", "0 1", "Off|On"),
	S("Turn before stepping (degrees)", "cl_fpbody_turnangle", "35 45 55 70 90", ""),
};
static const Item kMenu[] = {
	S("Menu style", "cl_fpmenu_style", "0 1", "Classic (number keys)|Big (mouse)"),
	S("Extras: monster spawning, other games' maps, placing models, other characters", "cl_fpextras", "0 1", "Off|On"),
};
static const Item kSettings[] = {
	O("Graphics", "graphics"), O("Camera and field of view", "camera"), O("Hands and weapon", "hands"),
	O("Body", "body"), O("Menu style and Extras", "menu"),
};

struct Page { const char *id; const char *title; const Item *items; int count; const char *parent; };
#define PAGE(id, title, arr, parent) { id, title, arr, (int)(sizeof(arr) / sizeof(arr[0])), parent }
static const Item kMain[] = {
	O("Weapons", "weapons"), O("Ammo", "ammo"), O("Items", "items"), O("Monsters", "monsters"),
	O("Clothes", "clothes"), O("Player", "player"), O("Maps", "maps"), O("Cheats", "cheats"), O("Settings", "settings"),
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
	PAGE("menu", "Settings", kMenu, "settings"),
};
static const int kNumPages = sizeof(kPages) / sizeof(kPages[0]);
static const Page kCheatsPageEnhanced = PAGE("cheats", "Cheats (Cry of Fear: Enhanced)", kCheatsEnhanced, "main");

// Tabs of the big menu, in order.
static const char *const kTabs[] = { "weapons", "ammo", "items", "monsters", "clothes", "player", "maps", "models", "cheats", "graphics", "camera", "hands", "body", "menu" };
static const char *const kTabNames[] = { "Weapons", "Ammo", "Items", "Monsters", "Clothes", "Player", "Maps", "Models", "Cheats", "Graphics", "Camera", "Hands", "Body", "Settings" };
static const int kNumTabs = sizeof(kTabs) / sizeof(kTabs[0]);

static bool Enhanced(void) { return GetModuleHandleA("xash.dll") != NULL; }

// Extras (Settings > Extras, cl_fpextras): monster spawning, other games' maps,
// placing models and playing as other characters only show up when it's on.
static cvar_t *s_extras;
static bool Extras(void) { return s_extras && s_extras->value != 0.0f; }

static bool TabShown(int t)
{
	return Extras() || (strcmp(kTabs[t], "monsters") && strcmp(kTabs[t], "models") && strcmp(kTabs[t], "player"));
}

// Indices of the big menu's tabs that are shown, in order.
static int ShownTabs(int *out)
{
	int n = 0;
	for (int t = 0; t < kNumTabs; t++)
		if (TabShown(t)) out[n++] = t;
	return n;
}

static int MapTabs(void) { return Extras() ? FpMaps_NumTabs() : 1; }

// --- Maps and Games pages: built from what's installed (fpmaps.cpp) -----------

static std::deque<std::string> s_dynText;          // labels/commands of the built pages
static std::vector<Item>       s_mapItems, s_gameItems;
static Page                    s_mapsPage, s_gamesPage;
static int                     s_mapTab;           // 0 = Cry of Fear, then the switched-on games

static const char *Keep(const std::string &s)
{
	s_dynText.push_back(s);
	return s_dynText.back().c_str();
}

static bool BigMenu(void);

static const Page *BuildMaps(void)
{
	if (s_mapTab >= MapTabs()) s_mapTab = 0;
	s_mapItems.clear();
	int n = FpMaps_Count(s_mapTab);
	for (int i = 0; i < n; i++)
	{
		char cmd[32];
		_snprintf(cmd, sizeof(cmd), "fpmap:%d", i);
		s_mapItems.push_back({ ACTION, Keep(FpMaps_Name(s_mapTab, i)), Keep(cmd), NULL, NULL });
	}
	if (s_mapItems.empty())
		s_mapItems.push_back({ ACTION, "(no maps found)", "", NULL, NULL });
	if (!BigMenu() && Extras())                   // the big menu has a Games button instead
		s_mapItems.push_back({ OPEN, "Games...", "games", NULL, NULL });
	s_mapsPage ={ "maps", "Maps", s_mapItems.data(), (int)s_mapItems.size(), "main" };
	return &s_mapsPage;
}

static const Page *BuildGames(void)
{
	s_gameItems.clear();
	int n = FpMaps_NumGames();
	for (int i = 0; i < n; i++)
	{
		char label[200], cmd[32];
		_snprintf(label, sizeof(label), "[%s]  %s   (%d maps)", FpMaps_GameOn(i) ? "x" : "  ", FpMaps_GameName(i), FpMaps_GameMapCount(i));
		label[sizeof(label) - 1] = 0;
		_snprintf(cmd, sizeof(cmd), "fpgame:%d", i);
		s_gameItems.push_back({ ACTION, Keep(label), Keep(cmd), NULL, NULL });
	}
	if (s_gameItems.empty())
		s_gameItems.push_back({ ACTION, "(no other GoldSrc games found in your Steam libraries)", "", NULL, NULL });
	s_gamesPage = { "games", "Games: tick the ones whose maps you want under Maps", s_gameItems.data(), (int)s_gameItems.size(), "maps" };
	return &s_gamesPage;
}

// --- Models pages (fpprops.cpp): folders, then the models in one folder ----------

static std::vector<Item> s_modelItems, s_fileItems, s_mainItems;
static Page              s_modelsPage, s_filesPage, s_mainPage;
static int               s_propDir;

static const Page *BuildModels(void)
{
	s_modelItems.clear();
	char label[96];
	s_modelItems.push_back({ ACTION, "Remove the model you're looking at", "fpprop:aim", NULL, NULL });
	s_modelItems.push_back({ ACTION, "Remove the last one placed", "fpprop:last", NULL, NULL });
	_snprintf(label, sizeof(label), "Remove all placed models (%d)", FpProps_Count());
	s_modelItems.push_back({ ACTION, Keep(label), "fpprop:all", NULL, NULL });
	for (int d = 0; d < FpProps_NumDirs(); d++)
	{
		char cmd[32];
		_snprintf(label, sizeof(label), "%s  (%d)  >", FpProps_DirName(d), FpProps_NumModels(d));
		_snprintf(cmd, sizeof(cmd), "fpdir:%d", d);
		s_modelItems.push_back({ ACTION, Keep(label), Keep(cmd), NULL, NULL });
	}
	s_modelsPage = { "models", "Models: place one in front of you (visual only, not solid)", s_modelItems.data(), (int)s_modelItems.size(), "main" };
	return &s_modelsPage;
}

static const Page *BuildModelFiles(void)
{
	s_fileItems.clear();
	s_fileItems.push_back({ OPEN, "<  Back to folders", "models", NULL, NULL });
	for (int i = 0; i < FpProps_NumModels(s_propDir); i++)
	{
		char cmd[32];
		_snprintf(cmd, sizeof(cmd), "fpprop:%d", i);
		s_fileItems.push_back({ ACTION, Keep(FpProps_ModelName(s_propDir, i)), Keep(cmd), NULL, NULL });
	}
	s_filesPage = { "modelfiles", Keep(std::string("Models: ") + FpProps_DirName(s_propDir)), s_fileItems.data(), (int)s_fileItems.size(), "models" };
	return &s_filesPage;
}

// --- Player page (fpchars.cpp): characters for the first-person body ---------

static std::vector<Item> s_charItems;
static Page              s_playerPage;
static int               s_charTab;          // like the Maps tabs: 0 = Cry of Fear, then ticked games

static int MapTabs(void);

static const Page *BuildPlayer(void)
{
	if (s_charTab >= MapTabs()) s_charTab = 0;
	s_charItems.clear();
	s_charItems.push_back({ SETTING, "Body animations", "cl_fpbody_anims", "0 1", "Simon's|Their own (if they have them)" });
	s_charItems.push_back({ ACTION, "Simon (normal)", "fpchar:simon", NULL, NULL });
	for (int i = 0; i < FpChars_Count(s_charTab); i++)
	{
		char cmd[32];
		_snprintf(cmd, sizeof(cmd), "fpchar:%d", i);
		s_charItems.push_back({ ACTION, Keep(FpChars_Label(s_charTab, i)), Keep(cmd), NULL, NULL });
	}
	const char *cur = eng->pfnGetCvarString ? eng->pfnGetCvarString((char *)"cl_fpbody_simon") : "";
	std::string title = std::string("Player: your body is ") + (cur && cur[0] ? cur : "Simon");
	s_playerPage = { "player", Keep(title), s_charItems.data(), (int)s_charItems.size(), "main" };
	return &s_playerPage;
}

// Classic menu's first page, without the Extras when they're off.
static const Page *BuildMain(void)
{
	s_mainItems.clear();
	for (const Item &it : kMain)
	{
		if (!Extras() && (!strcmp(it.cmd, "monsters") || !strcmp(it.cmd, "player"))) continue;
		s_mainItems.push_back(it);
		if (Extras() && !strcmp(it.cmd, "maps"))
			s_mainItems.push_back({ OPEN, "Models", "models", NULL, NULL });
	}
	s_mainPage = { "main", "SPAWN MENU", s_mainItems.data(), (int)s_mainItems.size(), NULL };
	return &s_mainPage;
}

static bool MapsPage(void);
static bool GamesPage(void);

static const Page *FindPage(const char *id)
{
	if (!strcmp(id, "main"))
		return BuildMain();
	if (!strcmp(id, "maps"))
		return BuildMaps();
	if (!strcmp(id, "games"))
		return BuildGames();
	if (!strcmp(id, "player"))
		return BuildPlayer();
	if (!strcmp(id, "models"))
		return BuildModels();
	if (!strcmp(id, "modelfiles"))
		return BuildModelFiles();
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

static bool PlayerPage(void) { return s_page && !strcmp(s_page->id, "player"); }
static bool MapsPage(void) { return s_page && !strcmp(s_page->id, "maps"); }
static bool GamesPage(void) { return s_page && !strcmp(s_page->id, "games"); }

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
	s_extras = FpRegister("cl_fpextras", "0", FCVAR_ARCHIVE);
	s_style = FpRegister("cl_fpmenu_style", "1", FCVAR_ARCHIVE);   // 1 = big mouse menu
}

// ---------------------------------------------------------------------------
// Doing things
// ---------------------------------------------------------------------------

static int s_clothesPick;          // last costume picked, shown when nothing is hovered

static void Send(const char *cmd)
{
	char buf[192];
	if (!cmd[0])
		return;
	if (!strncmp(cmd, "fpmap:", 6))
	{
		std::string c = FpMaps_Load(s_mapTab, atoi(cmd + 6));
		if (!c.empty())
		{
			s_open = false;
			eng->pfnClientCmd((char *)(c + "\n").c_str());
		}
		return;
	}
	if (!strncmp(cmd, "fpchar:", 7))
	{
		if (!strcmp(cmd + 7, "simon")) FpChars_PickSimon();
		else FpChars_Pick(s_charTab, atoi(cmd + 7));
		return;
	}
	if (!strncmp(cmd, "fpdir:", 6))
	{
		s_propDir = atoi(cmd + 6);
		s_page = BuildModelFiles();
		s_offset = 0;
		return;
	}
	if (!strncmp(cmd, "fpprop:", 7))
	{
		const char *a = cmd + 7;
		if (!strcmp(a, "aim")) FpProps_RemoveAimed();
		else if (!strcmp(a, "last")) FpProps_RemoveLast();
		else if (!strcmp(a, "all")) FpProps_RemoveAll();
		else
		{
			FpProps_Spawn(s_propDir, atoi(a));
			s_open = false;                       // close so you can see it
			return;
		}
		if (!strcmp(s_page->id, "models")) s_page = BuildModels();   // refresh the count
		return;
	}
	if (!strncmp(cmd, "fpgame:", 7))
	{
		FpMaps_ToggleGame(atoi(cmd + 7));
		s_page = BuildGames();
		return;
	}
	if (!strncmp(cmd, "setclothes ", 11))
	{
		s_clothesPick = atoi(cmd + 11);
		FpClothes_Select(s_clothesPick);   // what the game's wardrobe does before sending it
	}
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
				if (tab >= 0)
				{
					if (!strcmp(kTabs[tab], "maps")) FpMaps_Scan();           // pick up newly added maps
					if (!strcmp(kTabs[tab], "player")) FpChars_Scan();        // and characters
					Open(kTabs[tab]);
				}
				else if (row == -2) s_open = false;                        // close button
				else if (row == -3) Open(MapsPage() ? "games" : "maps");   // Games / Done
				else if (row <= -100 && PlayerPage()) { s_charTab = -100 - row; Open("player"); }
				else if (row <= -100) { s_mapTab = -100 - row; Open("maps"); }
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

// listR: right edge of the item rows (the Clothes page keeps the right part for a preview).
// stripY: Maps page's row of game tabs; btnY: the Games / Done button (0 = none).
struct BigLayout { int x, y, w, h, tabW, rowH, listX, listY, listR, rows, closeX, closeY, closeS, stripY, btnY, btnW; };

static bool ClothesPage(void) { return s_page && !strcmp(s_page->id, "clothes"); }

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
	L.listR = L.x + L.w - 16;
	if (ClothesPage() || PlayerPage())                          // room for the 3D preview
		L.listR = L.listX + (L.x + L.w - 16 - L.listX) * 45 / 100;
	L.stripY = L.btnY = L.btnW = 0;
	if ((MapsPage() || PlayerPage()) && Extras())              // row of game tabs (an Extra)
	{
		L.stripY = L.listY;
		L.listY += L.rowH + 6;
		L.rows = (L.y + L.h - 16 - L.listY) / L.rowH;
	}
	if ((MapsPage() && Extras()) || GamesPage())             // Games / Done button
	{
		int helpH = L.rowH;                               // keep the help line clear
		L.btnY = L.y + L.h - helpH - 8 - L.rowH;
		L.btnW = 180;
		L.rows = (L.btnY - 6 - L.listY) / L.rowH;
	}
	return L;
}

// Maps page: x extent of game tab t in the strip.
static void StripTab(const BigLayout &L, int t, int *x0, int *x1)
{
	int x = L.listX;
	for (int i = 0; i <= t; i++)
	{
		int w = TextWidth(FpMaps_TabName(i)) + 28;
		if (i == t) { *x0 = x; *x1 = x + w; return; }
		x += w + 6;
	}
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
		int shown[32], ns = ShownTabs(shown);
		int t = (cy - (L.y + 12)) / L.rowH;
		if (t >= 0 && t < ns) { *tab = shown[t]; return -1; }
	}
	if (L.btnY && cx >= L.listX && cx < L.listX + L.btnW && cy >= L.btnY && cy < L.btnY + L.rowH - 3)
		return -3;                                                       // Games / Done
	if (L.stripY && cy >= L.stripY && cy < L.stripY + L.rowH - 3)
		for (int t = 0; t < MapTabs(); t++)
		{
			int x0, x1;
			StripTab(L, t, &x0, &x1);
			if (cx >= x0 && cx < x1) return -100 - t;                    // game tab
		}
	if (cx >= L.listX && cx < L.listR && cy >= L.listY)
	{
		int r = (cy - L.listY) / L.rowH;
		if (r >= 0 && r < L.rows) return r;
	}
	return -1;
}

// Clothes page: Simon wearing the hovered (or last picked) costume, and its textures.
static void DrawClothesPreview(const BigLayout &L, int rowHover, int charH)
{
	int idx = (rowHover >= 0 && s_offset + rowHover < s_page->count) ? s_offset + rowHover : -1;
	if (idx < 0)
		for (int i = 0; i < s_page->count; i++)
			if (atoi(s_page->items[i].cmd + 11) == s_clothesPick) { idx = i; break; }
	if (idx < 0)
		return;
	const Item &it = s_page->items[idx];
	int costume = atoi(it.cmd + 11);

	int px = L.listR + 16, pw = L.x + L.w - 16 - px;
	int py = L.listY, ph = L.rows * L.rowH;
	BeginShapes();
	Rect(px, py, pw, ph, 0, 0, 0, 0.35f);
	EndShapes();
	Text(px + 12, py + 8, it.label, 1.0f, 0.75f, 0.45f);

	struct Cell { unsigned tex; int w, h; bool own; int part; } cells[4];
	int n = 0;
	for (int p = 0; p < FpClothes_NumParts() && n < 4; p++)
	{
		Cell c;
		if (FpClothes_Part(costume, p, &c.tex, &c.w, &c.h, &c.own))
		{
			c.part = p;
			cells[n++] = c;
		}
	}
	if (!n)
	{
		Text(px + 12, py + 16 + charH * 2, "No preview: texture files not found", 0.7f, 0.7f, 0.7f);
		return;
	}
	// Simon in the costume on top, the costume's textures in one small row below.
	int gap = 12, top = py + charH + 20, labelH = charH + 8;
	int cols = n, rowsN = 1;
	int cell = (pw - gap * (cols + 1)) / cols;
	int maxCell = ph * 22 / 100;
	if (cell > maxCell) cell = maxCell;
	if (cell < 16)
		return;
	int swatchY = py + ph - gap - labelH - cell;
	FpClothes_Draw3D(costume, px + 8, top, pw - 16, swatchY - gap - top, s_scrW, s_scrH, eng->GetClientTime());
	top = swatchY;
	int gridW = cols * cell + (cols - 1) * gap;
	int x0 = px + (pw - gridW) / 2;

	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1, 1, 1, 1);
	for (int i = 0; i < n; i++)
	{
		int cx = x0 + (i % cols) * (cell + gap), cy = top + (i / cols) * (cell + labelH + gap);
		// keep the texture's shape inside the square
		int dw = cell, dh = cell;
		if (cells[i].w > cells[i].h) dh = cell * cells[i].h / cells[i].w;
		else if (cells[i].h > cells[i].w) dw = cell * cells[i].w / cells[i].h;
		int dx = cx + (cell - dw) / 2, dy = cy + (cell - dh) / 2;
		glBindTexture(GL_TEXTURE_2D, cells[i].tex);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2i(dx, dy);
		glTexCoord2f(1, 0); glVertex2i(dx + dw, dy);
		glTexCoord2f(1, 1); glVertex2i(dx + dw, dy + dh);
		glTexCoord2f(0, 1); glVertex2i(dx, dy + dh);
		glEnd();
	}
	glPopAttrib();
	for (int i = 0; i < n; i++)
	{
		int cx = x0 + (i % cols) * (cell + gap), cy = top + (i / cols) * (cell + labelH + gap);
		char label[48];
		_snprintf(label, sizeof(label), cells[i].own || costume == 0 ? "%s" : "%s (normal)", FpClothes_PartName(cells[i].part));
		label[sizeof(label) - 1] = 0;
		Text(cx + (cell - TextWidth(label)) / 2, cy + cell + 4, label,
			cells[i].own || costume == 0 ? 0.92f : 0.55f, cells[i].own || costume == 0 ? 0.92f : 0.55f, cells[i].own || costume == 0 ? 0.92f : 0.55f);
	}
}

// Player page: the character under the cursor (or the one you are), turning in 3D.
static void DrawPlayerPreview(const BigLayout &L, int rowHover, int charH)
{
	std::string path, name;
	const Item *it = rowHover >= 0 && s_offset + rowHover < s_page->count ? &s_page->items[s_offset + rowHover] : NULL;
	if (it && it->type == ACTION && !strcmp(it->cmd, "fpchar:simon"))
		path = FpChars_Simon(), name = it->label;
	else if (it && it->type == ACTION && !strncmp(it->cmd, "fpchar:", 7))
		path = FpChars_Path(s_charTab, atoi(it->cmd + 7)), name = it->label;
	else
	{
		const char *cur = eng->pfnGetCvarString ? eng->pfnGetCvarString((char *)"cl_fpbody_simon") : "";
		path = cur && cur[0] ? cur : FpChars_Simon();
		name = "You: " + path;
	}
	int px = L.listR + 16, pw = L.x + L.w - 16 - px;
	int py = L.listY, ph = L.rows * L.rowH;
	BeginShapes();
	Rect(px, py, pw, ph, 0, 0, 0, 0.35f);
	EndShapes();
	Text(px + 12, py + 8, name.c_str(), 1.0f, 0.75f, 0.45f);
	int top = py + charH + 20;
	if (!FpPreview_Draw3D(path.c_str(), px + 8, top, pw - 16, py + ph - 8 - top, s_scrW, s_scrH, eng->GetClientTime()))
		Text(px + 12, py + 16 + charH * 2, "No preview: model can't be read", 0.7f, 0.7f, 0.7f);
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
	int shown[32], ns = ShownTabs(shown);
	for (int i = 0; i < ns; i++)
	{
		int t = shown[i];
		int ty = L.y + 12 + i * L.rowH;
		bool current = !strcmp(s_page->id, kTabs[t]) || (GamesPage() && !strcmp(kTabs[t], "maps")) ||
			(!strcmp(s_page->id, "modelfiles") && !strcmp(kTabs[t], "models")) ||
			(!strcmp(kTabs[t], "cheats") && s_page == &kCheatsPageEnhanced);
		if (current)            Rect(L.x, ty, L.tabW, L.rowH - 2, 0.55f, 0.1f, 0.08f, 0.85f);
		else if (t == tabHover) Rect(L.x, ty, L.tabW, L.rowH - 2, 1, 1, 1, 0.08f);
	}
	for (int r = 0; r < L.rows && s_offset + r < s_page->count; r++)
	{
		int ry = L.listY + r * L.rowH;
		Rect(L.listX, ry, L.listR - L.listX, L.rowH - 3, 1, 1, 1, r == rowHover ? 0.14f : 0.04f);
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
	if (L.stripY)                                                              // game tabs
		for (int t = 0; t < MapTabs(); t++)
		{
			int x0, x1;
			StripTab(L, t, &x0, &x1);
			if (t == (PlayerPage() ? s_charTab : s_mapTab)) Rect(x0, L.stripY, x1 - x0, L.rowH - 3, 0.55f, 0.1f, 0.08f, 0.85f);
			else                          Rect(x0, L.stripY, x1 - x0, L.rowH - 3, 1, 1, 1, rowHover == -100 - t ? 0.16f : 0.06f);
		}
	if (L.btnY)                                                                // Games / Done
		Rect(L.listX, L.btnY, L.btnW, L.rowH - 3, 0.75f, 0.12f, 0.1f, rowHover == -3 ? 0.95f : 0.6f);
	EndShapes();
	if (L.stripY)
		for (int t = 0; t < MapTabs(); t++)
		{
			int x0, x1;
			StripTab(L, t, &x0, &x1);
			Text(x0 + 14, L.stripY + textOff, FpMaps_TabName(t), 0.95f, 0.95f, 0.95f);
		}
	if (L.btnY)
	{
		const char *b = MapsPage() ? "Games" : "Done";
		Text(L.listX + (L.btnW - TextWidth(b)) / 2, L.btnY + textOff, b, 1, 1, 1);
	}

	Text(L.listX, L.y + 14, s_page->title, 1.0f, 0.4f, 0.35f);
	Text(L.closeX + (L.closeS - TextWidth("X")) / 2, L.closeY + (L.closeS - charH) / 2, "X", 1, 1, 1);
	for (int i = 0; i < ns; i++)
		Text(L.x + 16, L.y + 12 + i * L.rowH + textOff, kTabNames[shown[i]], 0.92f, 0.92f, 0.92f);
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
			Text(L.listR - 14 - TextWidth(shown), ry, shown, 1.0f, 0.75f, 0.45f);   // inside its row (pages with a preview are narrower)
		}
	}
	if (ClothesPage())
		DrawClothesPreview(L, rowHover, charH);
	if (PlayerPage())
		DrawPlayerPreview(L, rowHover, charH);
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
	if (PlayerPage() && !Extras())                         // Extras switched off while it was open
		Open(BigMenu() ? "weapons" : "main");
	if (BigMenu()) DrawBig(charH);
	else           DrawClassic(charH);
}
