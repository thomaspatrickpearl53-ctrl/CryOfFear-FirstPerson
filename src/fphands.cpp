// F8 > Player > Hands: Simon's, the Doctor's or Sick Simon's arms on every
// weapon.
//
// For each first-person weapon model with Cry of Fear's arm rig (r_dum, l_dum...)
// a copy is written to models/fpbody/: Simon's arm pieces (found by their
// textures) are hidden, and the chosen arms are added on the same arm bones,
// with their own textures, taken from one of the game's own models:
//   Doctor:     models/weapons/v_actions_doctor.mdl
//   Sick Simon: models/weapons/sledgehammer/v_e3cc3_pickaxe.mdl
// Built at the safe point before a frame is drawn, then swapped in for the
// viewmodel like fists mode does.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "r_studioint.h"
#include "studio.h"

extern cl_enginefunc_t     *eng;
extern engine_studio_api_t  g_studioEng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void    FpLog(const char *fmt, ...);
void    FpGameDir(char *out, size_t size);

struct HandSet { const char *name, *donor, *tex[3]; };
static const HandSet kSets[] = {
	{ "simon",  NULL, { NULL } },
	{ "doctor", "models/weapons/v_actions_doctor.mdl", { "sleeve_doc.bmp", "diffuse.bmp", NULL } },
	{ "sick",   "models/weapons/sledgehammer/v_e3cc3_pickaxe.mdl", { "sicksleeve.bmp", "sick_diffuse.bmp", NULL } },
};
static const int kNumSets = sizeof(kSets) / sizeof(kSets[0]);
// Simon's arm pieces in the weapon models
static const char *kSimonArmTex[] = { "fingers.bmp", "glove.bmp", "sleeve.bmp", "hand.bmp" };

static cvar_t *s_hands;
static int     s_builtFor = -1;                      // hand set the cache is for
static std::map<std::string, model_t *> s_cache;     // weapon model name -> its copy (NULL: unusable)
static std::string s_pending;

void FpHands_Init(void)
{
	s_hands = FpRegister("cl_fphands", "0", FCVAR_ARCHIVE);
}

void FpHands_NewMap(void)
{
	s_cache.clear();                                  // models belong to the old map's cache
	s_pending.clear();
}

static int Set(void)
{
	int s = s_hands ? (int)s_hands->value : 0;
	return s > 0 && s < kNumSets ? s : 0;
}

static bool ReadFile(const std::string &rel, std::vector<byte> &d)
{
	char dir[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	std::string p = std::string(dir) + rel;
	for (auto &c : p) if (c == '/') c = '\\';
	FILE *f = fopen(p.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d.resize(n > 0 ? n : 0);
	bool ok = n > 0 && fread(d.data(), 1, n, f) == (size_t)n;
	fclose(f);
	return ok && d.size() >= sizeof(studiohdr_t) && !memcmp(d.data(), "IDST", 4) && ((studiohdr_t *)d.data())->version == 10;
}

static int BoneIndex(const studiohdr_t *h, const char *name)
{
	const mstudiobone_t *b = (const mstudiobone_t *)((const byte *)h + h->boneindex);
	for (int i = 0; i < h->numbones; i++)
		if (!_stricmp(b[i].name, name)) return i;
	return -1;
}

static const char *TexName(const byte *d, int skinref)
{
	const studiohdr_t *h = (const studiohdr_t *)d;
	if (skinref < 0 || skinref >= h->numskinref) return "";
	short t = ((const short *)(d + h->skinindex))[skinref];
	if (t < 0 || t >= h->numtextures) return "";
	return ((const mstudiotexture_t *)(d + h->textureindex))[t].name;
}

static bool InList(const char *name, const char *const *list, int n)
{
	for (int i = 0; i < n && list[i]; i++)
		if (!_stricmp(name, list[i])) return true;
	return false;
}

template <class T> static void Put(std::vector<byte> &v, const T *p, size_t count)
{
	v.insert(v.end(), (const byte *)p, (const byte *)(p + count));
}
static void Align(std::vector<byte> &v) { while (v.size() % 4) v.push_back(0); }

// Writes models/fpbody/h<set>_<name>.mdl. Returns its path, or "" when the
// weapon has no Simon arms to replace.
static std::string Build(const std::string &weapon, int set)
{
	std::vector<byte> W, D;
	if (!ReadFile(weapon, W) || !ReadFile(kSets[set].donor, D))
		return "";
	studiohdr_t *wh = (studiohdr_t *)W.data();
	const studiohdr_t *dh = (const studiohdr_t *)D.data();
	if (wh->numtextures <= 0 || wh->texturedataindex <= 0 || BoneIndex(wh, "r_dum") < 0 || BoneIndex(wh, "l_dum") < 0)
		return "";

	// The weapon's submodel holding Simon's arms, and those meshes.
	mstudiobodyparts_t *wp = (mstudiobodyparts_t *)(W.data() + wh->bodypartindex);
	int armPart = -1, armModel = -1;
	std::vector<int> armMeshes;
	for (int p = 0; p < wh->numbodyparts && armPart < 0; p++)
	{
		mstudiomodel_t *ms = (mstudiomodel_t *)(W.data() + wp[p].modelindex);
		for (int m = 0; m < wp[p].nummodels && armPart < 0; m++)
		{
			mstudiomesh_t *mesh = (mstudiomesh_t *)(W.data() + ms[m].meshindex);
			for (int k = 0; k < ms[m].nummesh; k++)
				if (InList(TexName(W.data(), mesh[k].skinref), kSimonArmTex, 4))
					armMeshes.push_back(k);
			if (!armMeshes.empty()) { armPart = p; armModel = m; }
		}
	}
	if (armPart < 0)
		return "";

	// The donor's arm meshes (all in its first body part's first model).
	const mstudiobodyparts_t *dp = (const mstudiobodyparts_t *)(D.data() + dh->bodypartindex);
	const mstudiomodel_t *dm = (const mstudiomodel_t *)(D.data() + dp[0].modelindex);
	const mstudiomesh_t *dmesh = (const mstudiomesh_t *)(D.data() + dm->meshindex);
	struct DMesh { int k, normStart; };
	std::vector<DMesh> donorMeshes;
	int normAcc = 0;
	for (int k = 0; k < dm->nummesh; k++)
	{
		if (InList(TexName(D.data(), dmesh[k].skinref), kSets[set].tex, 3))
			donorMeshes.push_back({ k, normAcc });
		normAcc += dmesh[k].numnorms;          // the renderer lights normals mesh by mesh, in order
	}
	if (donorMeshes.empty())
		return "";

	mstudiomodel_t *wm = (mstudiomodel_t *)(W.data() + wp[armPart].modelindex) + armModel;
	int oldVerts = wm->numverts, oldNorms = wm->numnorms, oldMeshes = wm->nummesh;

	// Donor bones onto the weapon's (by name; a missing one falls back to its parent).
	const mstudiobone_t *db = (const mstudiobone_t *)(D.data() + dh->boneindex);
	std::vector<byte> boneMap(dh->numbones, 0);
	for (int i = 0; i < dh->numbones; i++)
	{
		int j = i, w = -1;
		while (j >= 0 && (w = BoneIndex(wh, db[j].name)) < 0) j = db[j].parent;
		boneMap[i] = (byte)(w < 0 ? 0 : w);
	}

	// Donor textures used by its arm meshes, in order.
	std::vector<int> addTex;
	std::map<int, int> newSkinref;               // donor skinref -> new skinref
	for (auto &m : donorMeshes)
	{
		int sr = dmesh[m.k].skinref;
		if (newSkinref.count(sr)) continue;
		short t = ((const short *)(D.data() + dh->skinindex))[sr];
		newSkinref[sr] = wh->numskinref + (int)addTex.size();
		addTex.push_back(t);
	}

	size_t insertAt = (size_t)wh->texturedataindex;
	std::vector<byte> X;                         // inserted at insertAt
	auto here = [&]() { Align(X); return (int)(insertAt + X.size()); };

	// vertices and their bones: the weapon's, then all of the donor's
	int vinfo = here();
	Put(X, W.data() + wm->vertinfoindex, oldVerts);
	for (int v = 0; v < dm->numverts; v++) X.push_back(boneMap[D[dm->vertinfoindex + v]]);
	// Same arm rig, but models are compiled at different sizes (v_actions_doctor
	// is 3.33x the weapons): scale the donor's arms by the forearm length ratio.
	float k = 1.0f;
	{
		int a = BoneIndex(wh, "r_low"), b = BoneIndex(dh, "r_low");
		const mstudiobone_t *wb_ = (const mstudiobone_t *)(W.data() + wh->boneindex);
		if (a >= 0 && b >= 0 && fabsf(db[b].value[0]) > 0.01f)
			k = wb_[a].value[0] / db[b].value[0];
	}
	int vpos = here();
	Put(X, (const vec3_t *)(W.data() + wm->vertindex), oldVerts);
	for (int v = 0; v < dm->numverts; v++)
	{
		const float *p = ((const vec3_t *)(D.data() + dm->vertindex))[v];
		float out[3] = { p[0] * k, p[1] * k, p[2] * k };
		Put(X, out, 3);
	}
	// normals: the weapon's, then each donor arm mesh's own block
	int ninfo = here();
	Put(X, W.data() + wm->norminfoindex, oldNorms);
	int addNorms = 0;
	for (auto &m : donorMeshes)
		for (int n = 0; n < dmesh[m.k].numnorms; n++, addNorms++)
			X.push_back(boneMap[D[dm->norminfoindex + m.normStart + n]]);
	int npos = here();
	Put(X, (const vec3_t *)(W.data() + wm->normindex), oldNorms);
	for (auto &m : donorMeshes)
		Put(X, (const vec3_t *)(D.data() + dm->normindex) + m.normStart, dmesh[m.k].numnorms);

	// an empty triangle list for the hidden arm pieces
	int emptyTris = here();
	short zero = 0;
	Put(X, &zero, 1);

	// donor triangle lists, renumbered
	std::vector<int> triPos;
	int normBase = oldNorms;
	for (auto &m : donorMeshes)
	{
		int pos = here();
		triPos.push_back(pos);
		const short *cmd = (const short *)(D.data() + dmesh[m.k].triindex);
		std::vector<short> out;
		for (;;)
		{
			short count = *cmd++;
			out.push_back(count);
			if (!count) break;
			for (int i = 0, n = count < 0 ? -count : count; i < n; i++, cmd += 4)
			{
				out.push_back((short)(cmd[0] + oldVerts));
				out.push_back((short)(cmd[1] - m.normStart + normBase));
				out.push_back(cmd[2]);
				out.push_back(cmd[3]);
			}
		}
		normBase += dmesh[m.k].numnorms;
		Put(X, out.data(), out.size());
	}

	// meshes: the weapon's (arm pieces emptied), then the donor's arms
	int meshPos = here();
	std::vector<mstudiomesh_t> meshes((mstudiomesh_t *)(W.data() + wm->meshindex), (mstudiomesh_t *)(W.data() + wm->meshindex) + oldMeshes);
	for (int k : armMeshes) { meshes[k].numtris = 0; meshes[k].triindex = emptyTris; }
	for (size_t i = 0; i < donorMeshes.size(); i++)
	{
		mstudiomesh_t m = dmesh[donorMeshes[i].k];
		m.triindex = triPos[i];
		m.skinref = newSkinref[m.skinref];
		m.normindex = npos;
		meshes.push_back(m);
	}
	Put(X, meshes.data(), meshes.size());

	// textures: the weapon's headers, then the donor's (pixels go at the end of the file)
	int texPos = here();
	std::vector<mstudiotexture_t> tex((mstudiotexture_t *)(W.data() + wh->textureindex), (mstudiotexture_t *)(W.data() + wh->textureindex) + wh->numtextures);
	for (int t : addTex) tex.push_back(((const mstudiotexture_t *)(D.data() + dh->textureindex))[t]);
	Put(X, tex.data(), tex.size());
	// skins: every family gets the new textures on the end
	int skinPos = here();
	int oldRefs = wh->numskinref, newRefs = oldRefs + (int)addTex.size();
	for (int fam = 0; fam < wh->numskinfamilies; fam++)
	{
		const short *row = (const short *)(W.data() + wh->skinindex) + fam * oldRefs;
		Put(X, row, oldRefs);
		for (size_t j = 0; j < addTex.size(); j++) { short t = (short)(wh->numtextures + j); Put(X, &t, 1); }
	}
	Align(X);

	// Splice in, then fix every offset into the moved texture pixels.
	int shift = (int)X.size();
	W.insert(W.begin() + insertAt, X.begin(), X.end());
	wh = (studiohdr_t *)W.data();
	mstudiotexture_t *nt = (mstudiotexture_t *)(W.data() + texPos);
	for (int t = 0; t < wh->numtextures; t++)
		if ((size_t)nt[t].index >= insertAt) nt[t].index += shift;
	wh->texturedataindex += shift;
	// donor pixels (and palettes) at the end
	for (size_t j = 0; j < addTex.size(); j++)
	{
		const mstudiotexture_t &src = ((const mstudiotexture_t *)(D.data() + dh->textureindex))[addTex[j]];
		size_t bytes = (size_t)src.width * src.height + 768;
		nt = (mstudiotexture_t *)(W.data() + texPos);
		nt[wh->numtextures + j].index = (int)W.size();
		W.insert(W.end(), D.begin() + src.index, D.begin() + src.index + bytes);
		wh = (studiohdr_t *)W.data();
	}
	wh->textureindex = texPos;
	wh->numtextures += (int)addTex.size();
	wh->skinindex = skinPos;
	wh->numskinref = newRefs;
	wp = (mstudiobodyparts_t *)(W.data() + wh->bodypartindex);
	wm = (mstudiomodel_t *)(W.data() + wp[armPart].modelindex) + armModel;
	wm->numverts = oldVerts + dm->numverts;
	wm->vertinfoindex = vinfo;
	wm->vertindex = vpos;
	wm->numnorms = oldNorms + addNorms;
	wm->norminfoindex = ninfo;
	wm->normindex = npos;
	wm->nummesh = (int)meshes.size();
	wm->meshindex = meshPos;
	wh->length = (int)W.size();

	std::string flat = weapon;
	for (auto &c : flat) if (c == '/' || c == '\\') c = '_';
	std::string out = std::string("models/fpbody/h") + kSets[set].name + "_" + flat;
	char dir[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	std::string full = std::string(dir) + out;
	for (auto &c : full) if (c == '/') c = '\\';
	CreateDirectoryA((std::string(dir) + "models\\fpbody").c_str(), NULL);
	FILE *f = fopen(full.c_str(), "wb");
	bool ok = f && fwrite(W.data(), 1, W.size(), f) == W.size();
	if (f) fclose(f);
	if (!ok) return "";
	FpLog("hands: built %s (%d arm pieces swapped, arms x%.2f)\n", out.c_str(), (int)armMeshes.size(), k);
	return out;
}

// From HUD_CreateEntities (safe to load models): build what the last frame asked for.
void FpHands_Prepare(void)
{
	int set = Set();
	if (set != s_builtFor) { s_cache.clear(); s_pending.clear(); s_builtFor = set; }
	if (s_pending.empty() || !set)
		return;
	std::string name = s_pending;
	s_pending.clear();
	model_t *m = NULL;
	std::string built = Build(name, set);
	if (!built.empty() && g_studioEng.Mod_ForName)
	{
		m = g_studioEng.Mod_ForName(built.c_str(), 0);
		if (m && m->type != mod_studio) m = NULL;
	}
	s_cache[name] = m;
}

// After V_CalcRefdef (and fists mode): show the weapon with the chosen arms.
void FpHands_Frame(void)
{
	int set = Set();
	if (!set) return;
	cl_entity_t *vm = eng->GetViewModel();
	if (!vm || !vm->model || !strstr(vm->model->name, "v_"))
		return;
	std::string name = vm->model->name;
	auto it = s_cache.find(name);
	if (it == s_cache.end())
	{
		if (s_pending.empty()) s_pending = name;
		return;
	}
	if (it->second)
		vm->model = it->second;
}
