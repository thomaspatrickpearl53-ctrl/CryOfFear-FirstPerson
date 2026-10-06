// F8 > Models (an Extra): place any of Cry of Fear's models in front of you, and
// remove them again.
//
// The models are drawn on the player's side, like the first-person body:
// visual only, not solid. The engine only lets the server load new models
// while a map starts, so server-side props can't be added mid-game.
// They're forgotten on map change.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "r_studioint.h"
#include "studio.h"
#include "ref_params.h"
#include "entity_types.h"
#include "pmtrace.h"
#include "pm_defs.h"

extern cl_enginefunc_t     *eng;
extern engine_studio_api_t  g_studioEng;
void FpLog(const char *fmt, ...);
void FpGameDir(char *out, size_t size);

static const int kMaxProps = 64;

struct Prop { cl_entity_t ent; std::string path; };

static std::vector<std::unique_ptr<Prop>> s_props;
static std::string s_pending;                 // model to place at the next safe point
static float s_eye[3], s_ang[3];              // the view, from V_CalcRefdef
static bool  s_haveView;

static std::vector<std::string> s_dirs;       // "" = models/ itself
static std::vector<std::vector<std::string>> s_files;
static bool s_scanned;

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

static std::string Lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }

static void ScanDir(const std::string &root, const std::string &rel)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((root + rel + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	std::vector<std::string> files, subdirs;
	do
	{
		std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (Lower(n) != "fpbody")                         // the mod's own generated models
				subdirs.push_back(n);
			continue;
		}
		std::string l = Lower(n);
		if (l.size() < 5 || l.compare(l.size() - 4, 4, ".mdl")) continue;
		if (l.compare(0, 2, "v_") == 0) continue;                    // hands/weapon views
		files.push_back(n.substr(0, n.size() - 4));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	// Skip a model's companion files: xxxT.mdl (its textures) and xxx01.mdl
	// (extra animations), when xxx.mdl is there too.
	std::vector<std::string> lower;
	for (auto &f : files) lower.push_back(Lower(f));
	std::vector<std::string> keep;
	for (size_t i = 0; i < files.size(); i++)
	{
		const std::string &b = lower[i];
		std::string stem;
		if (b.size() > 1 && b.back() == 't')
			stem = b.substr(0, b.size() - 1);
		else if (b.size() > 2 && isdigit((unsigned char)b[b.size() - 1]) && isdigit((unsigned char)b[b.size() - 2]))
			stem = b.substr(0, b.size() - 2);
		if (!stem.empty() && std::find(lower.begin(), lower.end(), stem) != lower.end())
			continue;
		keep.push_back(files[i]);
	}
	files.swap(keep);
	if (!files.empty())
	{
		std::sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) { return Lower(a) < Lower(b); });
		std::string d = rel.empty() ? "" : rel.substr(0, rel.size() - 1);
		s_dirs.push_back(d);
		s_files.push_back(files);
	}
	std::sort(subdirs.begin(), subdirs.end());
	for (auto &s : subdirs)
		ScanDir(root, rel + s + "\\");
}

void FpProps_Scan(void)
{
	s_scanned = true;
	s_dirs.clear();
	s_files.clear();
	char dir[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	ScanDir(std::string(dir) + "models\\", "");
	// folders in alphabetical order (models/ itself first)
	std::vector<size_t> order(s_dirs.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = i;
	std::sort(order.begin(), order.end(), [](size_t a, size_t b) { return Lower(s_dirs[a]) < Lower(s_dirs[b]); });
	std::vector<std::string> d;
	std::vector<std::vector<std::string>> f;
	for (size_t i : order) { d.push_back(s_dirs[i]); f.push_back(s_files[i]); }
	s_dirs.swap(d);
	s_files.swap(f);
}

int FpProps_NumDirs(void) { if (!s_scanned) FpProps_Scan(); return (int)s_dirs.size(); }
const char *FpProps_DirName(int d) { return d >= 0 && d < (int)s_dirs.size() ? (s_dirs[d].empty() ? "(models)" : s_dirs[d].c_str()) : ""; }
int FpProps_NumModels(int d) { return d >= 0 && d < (int)s_files.size() ? (int)s_files[d].size() : 0; }
const char *FpProps_ModelName(int d, int i) { return FpProps_NumModels(d) > i && i >= 0 ? s_files[d][i].c_str() : ""; }
int FpProps_Count(void) { return (int)s_props.size(); }

// ---------------------------------------------------------------------------
// Placing and removing
// ---------------------------------------------------------------------------

void FpProps_Spawn(int d, int i)
{
	if (FpProps_NumModels(d) <= i || i < 0) return;
	std::string rel = s_dirs[d].empty() ? s_files[d][i] : s_dirs[d] + "\\" + s_files[d][i];
	for (auto &c : rel) if (c == '\\') c = '/';
	s_pending = "models/" + rel + ".mdl";
}

void FpProps_RemoveLast(void) { if (!s_props.empty()) s_props.pop_back(); }
void FpProps_RemoveAll(void) { s_props.clear(); }

static void Forward(const float *ang, float *f)
{
	float p = ang[0] * (float)M_PI / 180.0f, y = ang[1] * (float)M_PI / 180.0f;
	f[0] = cosf(p) * cosf(y); f[1] = cosf(p) * sinf(y); f[2] = -sinf(p);
}

// The placed model closest to where you're looking (within about 20 degrees).
void FpProps_RemoveAimed(void)
{
	if (!s_haveView) return;
	float f[3];
	Forward(s_ang, f);
	int best = -1;
	float bestDot = cosf(20.0f * (float)M_PI / 180.0f);
	for (size_t i = 0; i < s_props.size(); i++)
	{
		const float *o = s_props[i]->ent.origin;
		float d[3] = { o[0] - s_eye[0], o[1] - s_eye[1], o[2] + 24.0f - s_eye[2] };
		float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (len < 1.0f) { best = (int)i; break; }
		float dot = (d[0] * f[0] + d[1] * f[1] + d[2] * f[2]) / len;
		if (dot > bestDot) { bestDot = dot; best = (int)i; }
	}
	if (best >= 0)
		s_props.erase(s_props.begin() + best);
}

static void Place(model_t *m, const std::string &path)
{
	float f[3], end[3], spot[3];
	Forward(s_ang, f);
	for (int k = 0; k < 3; k++) end[k] = s_eye[k] + f[k] * 160.0f;
	pmtrace_t *tr = eng->PM_TraceLine ? eng->PM_TraceLine(s_eye, end, PM_STUDIO_BOX, 2, -1) : NULL;
	float frac = tr ? tr->fraction : 1.0f;
	for (int k = 0; k < 3; k++) spot[k] = s_eye[k] + f[k] * (160.0f * frac - 24.0f);
	// down onto the floor
	float down[3] = { spot[0], spot[1], spot[2] - 512.0f };
	tr = eng->PM_TraceLine ? eng->PM_TraceLine(spot, down, PM_STUDIO_BOX, 2, -1) : NULL;
	if (tr && tr->fraction < 1.0f)
		spot[2] = tr->endpos[2];

	std::unique_ptr<Prop> p(new Prop());
	cl_entity_t &e = p->ent;
	memset(&e, 0, sizeof(e));
	e.model = m;
	VectorCopy(spot, e.origin);
	e.angles[1] = s_ang[1] + 180.0f;                            // facing you
	VectorCopy(e.origin, e.curstate.origin);
	VectorCopy(e.angles, e.curstate.angles);
	float now = eng->GetClientTime();
	e.curstate.rendermode = kRenderNormal;
	e.curstate.renderamt = 255;
	e.curstate.sequence = 0;
	e.curstate.framerate = 1.0f;
	e.curstate.animtime = now;
	for (int k = 0; k < 4; k++) e.curstate.controller[k] = 127;
	for (int k = 0; k < 2; k++) e.curstate.blending[k] = 127;
	e.latched.prevanimtime = now;
	VectorCopy(e.origin, e.latched.prevorigin);
	VectorCopy(e.angles, e.latched.prevangles);
	e.index = 0;
	p->path = path;
	if ((int)s_props.size() >= kMaxProps)
		s_props.erase(s_props.begin());
	s_props.push_back(std::move(p));
}

void FpProps_NewMap(void)
{
	s_props.clear();          // the models belong to the old map's model cache
	s_pending.clear();
	s_haveView = false;
}

// After V_CalcRefdef: where the player is looking.
void FpProps_CalcRefdef(ref_params_t *pp)
{
	VectorCopy(pp->vieworg, s_eye);
	VectorCopy(pp->viewangles, s_ang);
	s_haveView = true;
}

// From HUD_CreateEntities (the safe place to load models): place what was
// picked, then add every placed model to the frame.
void FpProps_CreateEntities(void)
{
	if (!s_pending.empty() && s_haveView)
	{
		std::string path = s_pending;
		s_pending.clear();
		char dir[MAX_PATH], full[MAX_PATH];
		FpGameDir(dir, sizeof(dir));
		_snprintf(full, sizeof(full), "%s%s", dir, path.c_str());
		model_t *m = NULL;
		if (GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES && g_studioEng.Mod_ForName)
			m = g_studioEng.Mod_ForName(path.c_str(), 0);
		if (m && m->type == mod_studio)
		{
			Place(m, path);
			FpLog("models: placed %s\n", path.c_str());
		}
		else
			FpLog("models: can't load %s\n", path.c_str());
	}
	for (auto &p : s_props)
		eng->CL_CreateVisibleEntity(ET_NORMAL, &p->ent);
}
