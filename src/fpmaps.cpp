// Maps tab of the F8 menu: Cry of Fear's own maps, plus the maps of any other
// GoldSrc game installed through Steam (Half-Life, Opposing Force, mods...)
// that the player switches on under "Games".
//
// Playing another game's map copies it into cryoffear/maps, together with the
// files it names that Cry of Fear doesn't have (texture WADs, sky images,
// models, sprites, sounds). Nothing of Cry of Fear's is ever overwritten, and
// every copied file is listed in cryoffear/fpmaps_installed.txt so it can be
// removed again (remove_everything.bat does).

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

void FpLog(const char *fmt, ...);
void FpGameDir(char *out, size_t size);

struct Game
{
	std::string name;       // from liblist.gam
	std::string dir;        // full path of the mod folder (…\Half-Life\valve)
	std::string folder;     // "valve"
	std::vector<std::string> maps;
	bool on;
};

static std::vector<Game>        s_games;
static std::vector<std::string> s_cofMaps;
static bool                     s_scanned;

// ---------------------------------------------------------------------------
// Small file helpers
// ---------------------------------------------------------------------------

static std::string GameDir(void)
{
	char dir[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	return dir;                                   // ends with a backslash
}

static bool Exists(const std::string &p)
{
	return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string Lower(std::string s)
{
	for (auto &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

static void MakeDirs(const std::string &path)      // creates the folders of a file path
{
	for (size_t i = 3; i < path.size(); i++)
		if (path[i] == '\\' || path[i] == '/')
			CreateDirectoryA(path.substr(0, i).c_str(), NULL);
}

static std::vector<std::string> List(const std::string &pattern, bool dirs)
{
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return out;
	do
	{
		bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (isDir == dirs && strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
			out.push_back(fd.cFileName);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return out;
}

static bool IsGoldSrcMap(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	int version = 0;
	bool ok = fread(&version, 4, 1, f) == 1 && version == 30;
	fclose(f);
	return ok;
}

static std::vector<std::string> MapsIn(const std::string &modDir)
{
	std::vector<std::string> maps;
	for (auto &f : List(modDir + "\\maps\\*.bsp", false))
		if (IsGoldSrcMap(modDir + "\\maps\\" + f))
			maps.push_back(f.substr(0, f.size() - 4));
	std::sort(maps.begin(), maps.end(), [](const std::string &a, const std::string &b) { return Lower(a) < Lower(b); });
	return maps;
}

// ---------------------------------------------------------------------------
// Finding games
// ---------------------------------------------------------------------------

static std::vector<std::string> SteamLibraries(void)
{
	std::vector<std::string> libs;
	char steam[MAX_PATH] = "";
	DWORD size = sizeof(steam);
	if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, NULL, steam, &size) == ERROR_SUCCESS)
	{
		for (char *p = steam; *p; p++) if (*p == '/') *p = '\\';
		libs.push_back(steam);
		FILE *f = fopen((std::string(steam) + "\\steamapps\\libraryfolders.vdf").c_str(), "rb");
		if (f)
		{
			char line[1024];
			while (fgets(line, sizeof(line), f))
			{
				const char *k = strstr(line, "\"path\"");
				if (!k) continue;
				const char *a = strchr(k + 6, '"');
				const char *b = a ? strchr(a + 1, '"') : NULL;
				if (!a || !b) continue;
				std::string p;
				for (const char *c = a + 1; c < b; c++)
				{
					if (*c == '\\' && c[1] == '\\') c++;           // vdf escapes backslashes
					p += *c;
				}
				if (std::find_if(libs.begin(), libs.end(), [&](const std::string &l) { return Lower(l) == Lower(p); }) == libs.end())
					libs.push_back(p);
			}
			fclose(f);
		}
	}
	return libs;
}

static std::string GameName(const std::string &modDir)
{
	FILE *f = fopen((modDir + "\\liblist.gam").c_str(), "rb");
	if (!f) return "";
	char line[512];
	std::string name;
	while (fgets(line, sizeof(line), f))
	{
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (_strnicmp(p, "game", 4) || (p[4] != ' ' && p[4] != '\t'))
			continue;
		const char *a = strchr(p, '"');
		const char *b = a ? strchr(a + 1, '"') : NULL;
		if (a && b) name.assign(a + 1, b);
		break;
	}
	fclose(f);
	return name;
}

static std::string SettingsFile(void) { return GameDir() + "fpmaps_games.txt"; }

static void LoadChoices(void)
{
	FILE *f = fopen(SettingsFile().c_str(), "rb");
	if (!f) return;
	char line[1024];
	std::set<std::string> on;
	while (fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		if (line[0]) on.insert(Lower(line));
	}
	fclose(f);
	for (auto &g : s_games)
		g.on = on.count(Lower(g.dir)) > 0;
}

static void SaveChoices(void)
{
	FILE *f = fopen(SettingsFile().c_str(), "wb");
	if (!f) return;
	for (auto &g : s_games)
		if (g.on) fprintf(f, "%s\n", g.dir.c_str());
	fclose(f);
}

void FpMaps_Scan(void)
{
	s_scanned = true;
	std::string own = GameDir();
	own = Lower(own.substr(0, own.size() - 1));          // …\cry of fear\cryoffear
	s_cofMaps = MapsIn(own);
	// Maps copied in from other games are listed under their own game, not here.
	FILE *mf = fopen((GameDir() + "fpmaps_installed.txt").c_str(), "rb");
	if (mf)
	{
		char line[1024];
		std::set<std::string> copied;
		while (fgets(line, sizeof(line), mf))
		{
			line[strcspn(line, "\r\n")] = 0;
			std::string l = Lower(line);
			if (l.compare(0, 5, "maps\\") == 0 && l.size() > 9)
				copied.insert(l.substr(5, l.size() - 9));
		}
		fclose(mf);
		s_cofMaps.erase(std::remove_if(s_cofMaps.begin(), s_cofMaps.end(),
			[&](const std::string &m) { return copied.count(Lower(m)) > 0; }), s_cofMaps.end());
	}
	s_games.clear();
	for (auto &lib : SteamLibraries())
	{
		std::string common = lib + "\\steamapps\\common";
		for (auto &app : List(common + "\\*", true))
			for (auto &mod : List(common + "\\" + app + "\\*", true))
			{
				std::string dir = common + "\\" + app + "\\" + mod;
				if (Lower(dir) == own || !Exists(dir + "\\liblist.gam"))
					continue;
				Game g;
				g.dir = dir;
				g.folder = mod;
				g.name = GameName(dir);
				if (g.name.empty()) g.name = mod;
				g.maps = MapsIn(dir);
				g.on = false;
				if (!g.maps.empty())
					s_games.push_back(g);
			}
	}
	// Same name more than once (Half-Life in several Steam apps): add the app folder.
	std::vector<bool> dup(s_games.size());
	for (size_t a = 0; a < s_games.size(); a++)
		for (size_t b = 0; b < s_games.size(); b++)
			if (a != b && s_games[a].name == s_games[b].name)
				dup[a] = true;
	for (size_t a = 0; a < s_games.size(); a++)
		if (dup[a])
		{
			Game &g = s_games[a];
			std::string app = g.dir.substr(0, g.dir.size() - g.folder.size() - 1);
			app = app.substr(app.find_last_of('\\') + 1);
			g.name += " (" + app + ")";
		}
	std::sort(s_games.begin(), s_games.end(), [](const Game &a, const Game &b) { return Lower(a.name) < Lower(b.name); });
	LoadChoices();
	FpLog("maps: %d Cry of Fear maps, %d other GoldSrc games found\n", (int)s_cofMaps.size(), (int)s_games.size());
}

static void Ensure(void) { if (!s_scanned) FpMaps_Scan(); }

// ---------------------------------------------------------------------------
// Menu data. Tab 0 is Cry of Fear; tab t > 0 is the (t-1)th switched-on game.
// ---------------------------------------------------------------------------

static Game *TabGame(int tab)
{
	if (tab <= 0) return NULL;
	int n = 0;
	for (auto &g : s_games)
		if (g.on && ++n == tab)
			return &g;
	return NULL;
}

int FpMaps_NumTabs(void)
{
	Ensure();
	int n = 1;
	for (auto &g : s_games) n += g.on;
	return n;
}

const char *FpMaps_TabName(int tab)
{
	Ensure();
	Game *g = TabGame(tab);
	return g ? g->name.c_str() : "Cry of Fear";
}

int FpMaps_Count(int tab)
{
	Ensure();
	Game *g = TabGame(tab);
	return g ? (int)g->maps.size() : (int)s_cofMaps.size();
}

const char *FpMaps_Name(int tab, int i)
{
	Game *g = TabGame(tab);
	const std::vector<std::string> &v = g ? g->maps : s_cofMaps;
	return i >= 0 && i < (int)v.size() ? v[i].c_str() : "";
}

int FpMaps_NumGames(void) { Ensure(); return (int)s_games.size(); }
const char *FpMaps_GameName(int i) { return i >= 0 && i < (int)s_games.size() ? s_games[i].name.c_str() : ""; }
int FpMaps_GameMapCount(int i) { return i >= 0 && i < (int)s_games.size() ? (int)s_games[i].maps.size() : 0; }
bool FpMaps_GameOn(int i) { return i >= 0 && i < (int)s_games.size() && s_games[i].on; }

void FpMaps_ToggleGame(int i)
{
	if (i < 0 || i >= (int)s_games.size()) return;
	s_games[i].on = !s_games[i].on;
	SaveChoices();
}

// ---------------------------------------------------------------------------
// Installing another game's map
// ---------------------------------------------------------------------------

static std::string ManifestFile(void) { return GameDir() + "fpmaps_installed.txt"; }

// Copy rel (relative to a game folder) into cryoffear if Cry of Fear doesn't have it.
static int CopyMissing(const std::string &rel, const std::vector<std::string> &sources, FILE *manifest)
{
	std::string dst = GameDir() + rel;
	if (Exists(dst))
		return 0;
	for (auto &src : sources)
	{
		std::string s = src + "\\" + rel;
		if (!Exists(s)) continue;
		MakeDirs(dst);
		if (CopyFileA(s.c_str(), dst.c_str(), TRUE))
		{
			if (manifest) fprintf(manifest, "%s\n", rel.c_str());
			return 1;
		}
	}
	return 0;
}

static std::string Slashes(std::string s)
{
	for (auto &c : s) if (c == '/') c = '\\';
	return s;
}

// The map's entity text (lump 0 of a v30 BSP).
static std::string Entities(const std::string &bsp)
{
	FILE *f = fopen(bsp.c_str(), "rb");
	if (!f) return "";
	int head[3] = {};
	std::string text;
	if (fread(head, 4, 3, f) == 3 && head[0] == 30 && head[2] > 0 && head[2] < (16 << 20))
	{
		text.resize(head[2]);
		fseek(f, head[1], SEEK_SET);
		text.resize(fread(&text[0], 1, head[2], f));
	}
	fclose(f);
	return text;
}

// Everything a map's entities point at by file name.
static void Resources(const std::string &ents, std::vector<std::string> &out)
{
	size_t p = 0;
	while ((p = ents.find('"', p)) != std::string::npos)
	{
		size_t k1 = ents.find('"', p + 1);
		if (k1 == std::string::npos) break;
		std::string key = Lower(ents.substr(p + 1, k1 - p - 1));
		size_t v0 = ents.find('"', k1 + 1);
		size_t v1 = v0 == std::string::npos ? v0 : ents.find('"', v0 + 1);
		if (v1 == std::string::npos) break;
		std::string val = ents.substr(v0 + 1, v1 - v0 - 1);
		p = v1 + 1;
		std::string lv = Lower(val);
		if (key == "wad")
		{
			size_t a = 0;
			while (a <= lv.size())
			{
				size_t b = lv.find(';', a);
				std::string w = lv.substr(a, b == std::string::npos ? std::string::npos : b - a);
				size_t slash = w.find_last_of("\\/");
				if (slash != std::string::npos) w = w.substr(slash + 1);
				if (!w.empty()) out.push_back(w);
				if (b == std::string::npos) break;
				a = b + 1;
			}
		}
		else if (key == "skyname")
		{
			static const char *sides[] = { "rt", "lf", "ft", "bk", "up", "dn" };
			for (auto s : sides)
			{
				out.push_back("gfx\\env\\" + lv + s + ".tga");
				out.push_back("gfx\\env\\" + lv + s + ".bmp");
			}
		}
		else if (lv.size() > 4 && (lv.compare(lv.size() - 4, 4, ".mdl") == 0 || lv.compare(lv.size() - 4, 4, ".spr") == 0))
			out.push_back(Slashes(lv));
		else if (lv.size() > 4 && lv.compare(lv.size() - 4, 4, ".wav") == 0)
		{
			std::string s = lv;
			while (!s.empty() && (s[0] == '*' || s[0] == '!')) s = s.substr(1);
			out.push_back("sound\\" + Slashes(s));
		}
	}
}

static std::string InstallMap(Game &g, const std::string &map)
{
	std::string src = g.dir + "\\maps\\" + map + ".bsp";
	std::string name = map;
	std::string dst = GameDir() + "maps\\" + name + ".bsp";
	// A Cry of Fear map with the same name keeps its name; this one gets a prefix.
	if (Exists(dst))
	{
		WIN32_FILE_ATTRIBUTE_DATA a, b;
		bool same = GetFileAttributesExA(src.c_str(), GetFileExInfoStandard, &a) &&
			GetFileAttributesExA(dst.c_str(), GetFileExInfoStandard, &b) &&
			a.nFileSizeLow == b.nFileSizeLow && a.nFileSizeHigh == b.nFileSizeHigh;
		if (!same)
		{
			name = Lower(g.folder) + "_" + map;
			dst = GameDir() + "maps\\" + name + ".bsp";
		}
	}
	FILE *manifest = fopen(ManifestFile().c_str(), "ab");
	if (!Exists(dst) && CopyFileA(src.c_str(), dst.c_str(), TRUE) && manifest)
		fprintf(manifest, "maps\\%s.bsp\n", name.c_str());

	// Where its files can come from: the game itself, then Half-Life (most mods
	// fall back to it).
	std::vector<std::string> sources = { g.dir };
	std::string root = g.dir.substr(0, g.dir.size() - g.folder.size() - 1);
	if (Lower(g.folder) != "valve" && Exists(root + "\\valve"))
		sources.push_back(root + "\\valve");
	for (auto &other : s_games)
		if (Lower(other.folder) == "valve" && std::find(sources.begin(), sources.end(), other.dir) == sources.end())
			sources.push_back(other.dir);

	std::vector<std::string> res;
	Resources(Entities(src), res);
	int copied = 0;
	for (auto &r : res)
		copied += CopyMissing(r, sources, manifest);
	if (manifest) fclose(manifest);
	FpLog("maps: %s from %s -> maps/%s.bsp, %d of %d files it uses copied\n",
		map.c_str(), g.name.c_str(), name.c_str(), copied, (int)res.size());
	return name;
}

// The console command that starts the map ("" if there's nothing to load).
std::string FpMaps_Load(int tab, int i)
{
	Ensure();
	Game *g = TabGame(tab);
	if (!g)
		return i >= 0 && i < (int)s_cofMaps.size() ? "map " + s_cofMaps[i] : "";
	if (i < 0 || i >= (int)g->maps.size())
		return "";
	std::string name = InstallMap(*g, g->maps[i]);
	return "map " + name;
}
