// F8 > Player: pick the character your first-person body is.
//
// Lists every character model with Simon's kind of skeleton (Bip01 biped bones
// named like his) from Cry of Fear, and, with Extras on, from the other GoldSrc
// games ticked under Maps > Games (their models are copied into
// cryoffear/models/fpchars when picked, and logged for remove_everything).
//
// Animations (cl_fpbody_anims): 0 = Simon's: a copy of the character is built
// with Simon's body animations (walk, run, crouch, jump...) re-targeted onto
// its skeleton - joint rotations from Simon, bone lengths its own, so it keeps
// its proportions. 1 = its own when it has walk/run animations, Simon's otherwise.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "studio.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void    FpLog(const char *fmt, ...);
void    FpGameDir(char *out, size_t size);
int         FpMaps_NumTabs(void);
const char *FpMaps_TabDir(int tab);
const char *FpMaps_TabFolder(int tab);

static const char *kSimon = "models/cutscene/player.mdl";
// The body animations the first-person body looks for (fpbody.cpp).
static const char *kBodySeqs[] = { "idle", "look_idle", "walk", "run", "run2", "sprint", "crouch_idle",
                                   "crawl", "crouch_crouch", "crouch_walk", "jump" };
// Bones a character needs (Simon's names) so his animations and the body's
// hiding of head and arms fit it.
static const char *kNeedBones[] = { "Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Head", "Bip01 L Leg", "Bip01 R Leg",
                                    "Bip01 L Leg1", "Bip01 R Leg1", "Bip01 L Foot", "Bip01 R Foot", "Bip01 L Arm1", "Bip01 R Arm1" };

static cvar_t *s_anims;

struct Char { std::string rel, label; };                // rel: path relative to its game folder
static std::vector<std::vector<Char>> s_tabs;            // per Maps tab
static bool s_scanned;

static std::string Lower(std::string s) { for (auto &c : s) c = (char)tolower((unsigned char)c); return s; }

static std::string CofDir(void)
{
	char dir[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	return dir;                                           // ends with a backslash
}

static bool ReadAll(const std::string &path, std::vector<byte> &data)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data.resize(len > 0 ? len : 0);
	bool ok = len > 0 && fread(data.data(), 1, len, f) == (size_t)len;
	fclose(f);
	return ok;
}

static bool WriteAll(const std::string &path, const void *p, size_t n)
{
	for (size_t i = 3; i < path.size(); i++)
		if (path[i] == '\\' || path[i] == '/') CreateDirectoryA(path.substr(0, i).c_str(), NULL);
	FILE *f = fopen(path.c_str(), "wb");
	bool ok = f && fwrite(p, 1, n, f) == n;
	if (f) fclose(f);
	return ok;
}

// ---------------------------------------------------------------------------
// Listing: only the header, bone and sequence tables are read.
// ---------------------------------------------------------------------------

static bool IsCharacter(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	studiohdr_t h;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(&h.id, "IDST", 4) && h.version == 10 &&
		h.numbones > 0 && h.numbones <= MAXSTUDIOBONES && h.numbodyparts > 0;
	if (ok)
	{
		std::vector<mstudiobone_t> bones(h.numbones);
		fseek(f, h.boneindex, SEEK_SET);
		ok = fread(bones.data(), sizeof(mstudiobone_t), h.numbones, f) == (size_t)h.numbones;
		for (const char *need : kNeedBones)
		{
			if (!ok) break;
			ok = std::any_of(bones.begin(), bones.end(), [&](const mstudiobone_t &b) { return !_stricmp(b.name, need); });
		}
	}
	fclose(f);
	return ok;
}

static void ScanGame(const std::string &gameDir, std::vector<Char> &out, const std::string &rel)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((gameDir + rel + "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	std::vector<std::string> files, dirs;
	do
	{
		std::string n = fd.cFileName;
		if (n == "." || n == "..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			std::string l = Lower(n);
			if (l != "fpbody" && l != "fpchars") dirs.push_back(n);
			continue;
		}
		std::string l = Lower(n);
		if (l.size() > 4 && l.compare(l.size() - 4, 4, ".mdl") == 0 && l.compare(0, 2, "v_") && l.compare(0, 2, "p_") && l.compare(0, 2, "w_"))
			files.push_back(n);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	for (auto &n : files)
	{
		// companions of another model (xxxT.mdl textures, xxx01.mdl animations) aren't characters
		std::string l = Lower(n.substr(0, n.size() - 4));
		std::string stem = l.back() == 't' ? l.substr(0, l.size() - 1) :
			(l.size() > 2 && isdigit((unsigned char)l[l.size() - 1]) && isdigit((unsigned char)l[l.size() - 2])) ? l.substr(0, l.size() - 2) : "";
		if (!stem.empty() && std::any_of(files.begin(), files.end(), [&](const std::string &o) { return Lower(o) == stem + ".mdl"; }))
			continue;
		if (IsCharacter(gameDir + rel + n))
		{
			std::string r = rel + n;
			for (auto &c : r) if (c == '\\') c = '/';
			std::string label = r.substr(7, r.size() - 11);   // without "models/" and ".mdl"
			out.push_back({ r, label });
		}
	}
	std::sort(dirs.begin(), dirs.end());
	for (auto &d : dirs)
		ScanGame(gameDir, out, rel + d + "\\");
}

void FpChars_Scan(void)
{
	s_scanned = true;
	s_tabs.assign(FpMaps_NumTabs(), {});
	for (int t = 0; t < (int)s_tabs.size(); t++)
	{
		const char *dir = FpMaps_TabDir(t);
		std::string g = dir ? std::string(dir) + "\\" : CofDir();
		ScanGame(g, s_tabs[t], "models\\");
		std::sort(s_tabs[t].begin(), s_tabs[t].end(), [](const Char &a, const Char &b) { return Lower(a.label) < Lower(b.label); });
	}
	FpLog("chars: %d Cry of Fear characters\n", s_tabs.empty() ? 0 : (int)s_tabs[0].size());
}

int FpChars_Count(int tab) { if (!s_scanned) FpChars_Scan(); return tab >= 0 && tab < (int)s_tabs.size() ? (int)s_tabs[tab].size() : 0; }
const char *FpChars_Label(int tab, int i) { return i >= 0 && i < FpChars_Count(tab) ? s_tabs[tab][i].label.c_str() : ""; }

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

// Copies a model's companion files (xxxT.mdl, xxx01.mdl...) next to a copy of it.
static void CopyCompanions(const std::string &srcFull, const std::string &dstFull, FILE *manifest, const std::string &dstRel)
{
	std::string s = srcFull.substr(0, srcFull.size() - 4), d = dstFull.substr(0, dstFull.size() - 4);
	std::string r = dstRel.substr(0, dstRel.size() - 4);
	const char *suffixes[] = { "t", "T", "01", "02", "03", "04", "05", "06", "07", "08", "09" };
	for (const char *suf : suffixes)
	{
		std::string from = s + suf + ".mdl";
		if (GetFileAttributesA(from.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		std::string to = d + Lower(suf) + ".mdl";
		if (CopyFileA(from.c_str(), to.c_str(), FALSE) && manifest)
			fprintf(manifest, "%s\n", (r + Lower(suf) + ".mdl").c_str());
	}
}

// Makes the character usable by the engine (copied in from another game if
// needed) and sets it as the body: returns its cryoffear-relative path.
std::string FpChars_Pick(int tab, int i)
{
	if (tab < 0 || i < 0 || i >= FpChars_Count(tab))
		return "";
	const Char &c = s_tabs[tab][i];
	std::string rel = c.rel;
	const char *dir = FpMaps_TabDir(tab);
	if (dir)
	{
		std::string src = std::string(dir) + "\\" + c.rel;
		std::string flat = c.rel.substr(7);
		for (auto &ch : flat) if (ch == '/') ch = '_';
		rel = std::string("models/fpchars/") + FpMaps_TabFolder(tab) + "/" + flat;
		std::string dst = CofDir() + rel;
		for (auto &ch : dst) if (ch == '/') ch = '\\';
		if (GetFileAttributesA(dst.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			std::vector<byte> data;
			FILE *manifest = fopen((CofDir() + "fpmaps_installed.txt").c_str(), "ab");
			std::string relWin = rel;
			for (auto &ch : relWin) if (ch == '/') ch = '\\';
			if (ReadAll(src, data) && WriteAll(dst, data.data(), data.size()) && manifest)
				fprintf(manifest, "%s\n", relWin.c_str());
			CopyCompanions(src, dst, manifest, relWin);
			if (manifest) fclose(manifest);
		}
	}
	eng->pfnClientCmd((char *)("cl_fpbody_simon \"" + rel + "\"\n").c_str());
	FpLog("chars: body is now %s\n", rel.c_str());
	return rel;
}

void FpChars_PickSimon(void)
{
	eng->pfnClientCmd((char *)("cl_fpbody_simon \"" + std::string(kSimon) + "\"\n").c_str());
}

// ---------------------------------------------------------------------------
// Simon's animations on another skeleton
// ---------------------------------------------------------------------------

static short AnimValue(const mstudioanim_t *anim, int ch, int frame)
{
	if (!anim->offset[ch]) return 0;
	const mstudioanimvalue_t *p = (const mstudioanimvalue_t *)((const byte *)anim + anim->offset[ch]);
	int k = frame;
	while (p->num.total <= k)
	{
		k -= p->num.total;
		p += p->num.valid + 1;
		if (!p->num.total) return 0;
	}
	return p->num.valid > k ? p[k + 1].value : p[p->num.valid].value;
}

static int BoneByName(const studiohdr_t *h, const char *name)
{
	const mstudiobone_t *b = (const mstudiobone_t *)((const byte *)h + h->boneindex);
	for (int i = 0; i < h->numbones; i++)
		if (!_stricmp(b[i].name, name)) return i;
	return -1;
}

static int SeqByName(const studiohdr_t *h, const char *name)
{
	const mstudioseqdesc_t *s = (const mstudioseqdesc_t *)((const byte *)h + h->seqindex);
	for (int i = 0; i < h->numseq; i++)
		if (!_stricmp(s[i].label, name)) return i;
	return -1;
}

static float LegLength(const studiohdr_t *h)
{
	const mstudiobone_t *b = (const mstudiobone_t *)((const byte *)h + h->boneindex);
	float len = 0;
	for (const char *n : { "Bip01 L Leg1", "Bip01 L Foot" })
	{
		int i = BoneByName(h, n);
		if (i >= 0) len += sqrtf(b[i].value[0] * b[i].value[0] + b[i].value[1] * b[i].value[1] + b[i].value[2] * b[i].value[2]);
	}
	return len;
}

// --- rotations (GoldSrc angles: roll x, pitch y, yaw z; R = Rz Ry Rx) ---------

struct M3 { float m[3][3]; };

static M3 Rot(const float *a)
{
	float sr = sinf(a[0] * 0.5f), cr = cosf(a[0] * 0.5f);
	float sp = sinf(a[1] * 0.5f), cp = cosf(a[1] * 0.5f);
	float sy = sinf(a[2] * 0.5f), cy = cosf(a[2] * 0.5f);
	float x = sr * cp * cy - cr * sp * sy, y = cr * sp * cy + sr * cp * sy;
	float z = cr * cp * sy - sr * sp * cy, w = cr * cp * cy + sr * sp * sy;
	M3 r;
	r.m[0][0] = 1 - 2 * y * y - 2 * z * z; r.m[0][1] = 2 * x * y - 2 * w * z;     r.m[0][2] = 2 * x * z + 2 * w * y;
	r.m[1][0] = 2 * x * y + 2 * w * z;     r.m[1][1] = 1 - 2 * x * x - 2 * z * z; r.m[1][2] = 2 * y * z - 2 * w * x;
	r.m[2][0] = 2 * x * z - 2 * w * y;     r.m[2][1] = 2 * y * z + 2 * w * x;     r.m[2][2] = 1 - 2 * x * x - 2 * y * y;
	return r;
}

static M3 Mul(const M3 &a, const M3 &b)
{
	M3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
	return r;
}

static M3 T(const M3 &a)
{
	M3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a.m[j][i];
	return r;
}

static void Angles(const M3 &r, float *a)
{
	float s = -r.m[2][0];
	a[1] = asinf(s > 1 ? 1 : s < -1 ? -1 : s);
	a[0] = atan2f(r.m[2][1], r.m[2][2]);
	a[2] = atan2f(r.m[1][0], r.m[0][0]);
}

// World (model space) rotation of every bone from its local rotations.
static void Globals(const mstudiobone_t *b, int nb, const std::vector<M3> &local, std::vector<M3> &out)
{
	out.resize(nb);
	for (int i = 0; i < nb; i++)
		out[i] = b[i].parent < 0 ? local[i] : Mul(out[b[i].parent], local[i]);
}

static bool HasOwnWalk(const studiohdr_t *h)
{
	return SeqByName(h, "walk") >= 0 && (SeqByName(h, "run") >= 0 || SeqByName(h, "idle") >= 0 || SeqByName(h, "look_idle") >= 0);
}

// Builds models/fpbody/rt_<name>.mdl: the character with Simon's body
// animations. Returns its path, or "" (then the character is used as it is).
static std::string Retarget(const std::string &charRel)
{
	std::vector<byte> C, S;
	std::string cofDir = CofDir();
	if (!ReadAll(cofDir + charRel, C) || !ReadAll(cofDir + kSimon, S) || C.size() < sizeof(studiohdr_t))
		return "";
	studiohdr_t *ch = (studiohdr_t *)C.data(), *sh = (studiohdr_t *)S.data();
	if (memcmp(&ch->id, "IDST", 4) || ch->version != 10 || ((mstudioseqgroup_t *)(S.data() + sh->seqgroupindex))->unused2)
		return "";
	const mstudiobone_t *cb = (const mstudiobone_t *)(C.data() + ch->boneindex);
	const mstudiobone_t *sb = (const mstudiobone_t *)(S.data() + sh->boneindex);
	int nb = ch->numbones;
	float k = LegLength(sh) > 1 ? LegLength(ch) / LegLength(sh) : 1.0f;   // body size compared with Simon

	std::vector<int> map(nb);
	for (int b = 0; b < nb; b++) map[b] = BoneByName(sh, cb[b].name);

	// Rigs differ in how each joint's local axes point, so motion is moved over
	// in world space: each joint gets the same world-space turn, away from its
	// rest pose, that Simon's has, then that is put back into the character's
	// own local axes.
	int ns = sh->numbones;
	std::vector<M3> sLocRest(ns), cLocRest(nb), sRest, cRest, sNow, cNow(nb);
	for (int i = 0; i < ns; i++) sLocRest[i] = Rot(sb[i].value + 3);
	for (int i = 0; i < nb; i++) cLocRest[i] = Rot(cb[i].value + 3);
	Globals(sb, ns, sLocRest, sRest);
	Globals(cb, nb, cLocRest, cRest);

	struct Seq { mstudioseqdesc_t desc; int frames; std::vector<float> v; };
	std::vector<Seq> seqs;
	for (const char *name : kBodySeqs)
	{
		int si = SeqByName(sh, name);
		if (si < 0) continue;
		const mstudioseqdesc_t *sd = (const mstudioseqdesc_t *)(S.data() + sh->seqindex) + si;
		const mstudioanim_t *anim = (const mstudioanim_t *)(S.data() + sd->animindex);
		Seq q;
		q.desc = *sd;
		q.desc.numevents = q.desc.eventindex = 0;
		q.desc.numpivots = q.desc.pivotindex = 0;
		q.desc.seqgroup = 0;
		q.desc.numblends = 1;
		q.desc.entrynode = q.desc.exitnode = q.desc.nodeflags = q.desc.nextseq = 0;
		q.frames = sd->numframes > 0 ? sd->numframes : 1;
		q.v.resize((size_t)q.frames * nb * 6);
		for (int f = 0; f < q.frames; f++)
		{
			// Simon at this frame: local values, then world rotations
			std::vector<float> sv((size_t)ns * 6);
			std::vector<M3> sLoc(ns);
			for (int s = 0; s < ns; s++)
			{
				for (int c = 0; c < 6; c++)
					sv[s * 6 + c] = sb[s].value[c] + AnimValue(&anim[s], c, f) * sb[s].scale[c];
				sLoc[s] = Rot(&sv[s * 6 + 3]);
			}
			Globals(sb, ns, sLoc, sNow);
			for (int b = 0; b < nb; b++)
			{
				int s = map[b], p = cb[b].parent;
				M3 g = s >= 0 ? Mul(Mul(sNow[s], T(sRest[s])), cRest[b])                 // Simon's world turn
				              : (p >= 0 ? Mul(cNow[p], Rot(cb[b].value + 3)) : Rot(cb[b].value + 3));
				cNow[b] = g;
				float a[3];
				Angles(p >= 0 ? Mul(T(cNow[p]), g) : g, a);
				float *out = &q.v[((size_t)f * nb + b) * 6];
				for (int c = 0; c < 3; c++)
				{
					out[c] = cb[b].value[c];                     // the character's own bone lengths
					if (p < 0 && s >= 0)                         // the root moves like Simon's, scaled
						out[c] += (sv[s * 6 + c] - sb[s].value[c]) * k;
				}
				out[3] = a[0]; out[4] = a[1]; out[5] = a[2];
			}
		}
		seqs.push_back(std::move(q));
	}
	if (seqs.empty())
		return "";

	// Per-channel scales covering every frame around the rest values.
	mstudiobone_t *bones = (mstudiobone_t *)(C.data() + ch->boneindex);
	std::vector<float> rest(nb * 6), scale(nb * 6);
	for (int b = 0; b < nb; b++)
		for (int c = 0; c < 6; c++)
		{
			float dev = 0;
			for (auto &q : seqs)
				for (int f = 0; f < q.frames; f++)
					dev = fmaxf(dev, fabsf(q.v[((size_t)f * nb + b) * 6 + c] - bones[b].value[c]));
			rest[b * 6 + c] = bones[b].value[c];
			scale[b * 6 + c] = bones[b].scale[c] = dev > 1e-6f ? dev / 32000.0f : 1.0f;
		}

	// New animations and sequence table go just before the texture pixels
	// (GoldSrc keeps only the part of a model before texturedataindex).
	size_t insertAt = (ch->numtextures > 0 && ch->texturedataindex > 0) ? (size_t)ch->texturedataindex : C.size();
	std::vector<byte> extra;
	auto align = [&]() { while (extra.size() % 4) extra.push_back(0); };
	for (auto &q : seqs)
	{
		align();
		size_t base = insertAt + extra.size();
		size_t tableBytes = (size_t)nb * sizeof(mstudioanim_t);
		std::vector<mstudioanim_t> table(nb);
		std::vector<short> values;
		for (int b = 0; b < nb; b++)
			for (int c = 0; c < 6; c++)
			{
				std::vector<short> raw(q.frames);
				bool any = false;
				for (int f = 0; f < q.frames; f++)
				{
					float x = (q.v[((size_t)f * nb + b) * 6 + c] - rest[b * 6 + c]) / scale[b * 6 + c];
					raw[f] = (short)floorf(x + 0.5f);
					any |= raw[f] != 0;
				}
				table[b].offset[c] = 0;
				if (!any) continue;
				size_t off = tableBytes + values.size() * sizeof(short) - b * sizeof(mstudioanim_t);
				if (off > 0xFFFF) return "";
				table[b].offset[c] = (unsigned short)off;
				for (int f = 0; f < q.frames; f += 255)
				{
					int n = (std::min)(255, q.frames - f);
					mstudioanimvalue_t head;
					head.num.valid = head.num.total = (byte)n;
					values.push_back(head.value);
					values.insert(values.end(), raw.begin() + f, raw.begin() + f + n);
				}
			}
		extra.insert(extra.end(), (byte *)table.data(), (byte *)table.data() + tableBytes);
		extra.insert(extra.end(), (byte *)values.data(), (byte *)values.data() + values.size() * sizeof(short));
		q.desc.animindex = (int)base;
	}
	align();
	size_t seqBase = insertAt + extra.size();
	for (auto &q : seqs)
		extra.insert(extra.end(), (byte *)&q.desc, (byte *)&q.desc + sizeof(q.desc));
	align();
	// one sequence group, its animations in this file
	size_t groupBase = insertAt + extra.size();
	mstudioseqgroup_t grp = {};
	strcpy(grp.label, "default");
	extra.insert(extra.end(), (byte *)&grp, (byte *)&grp + sizeof(grp));
	align();

	int shift = (int)extra.size();
	C.insert(C.begin() + insertAt, extra.begin(), extra.end());
	ch = (studiohdr_t *)C.data();
	if (ch->numtextures > 0 && (size_t)ch->texturedataindex >= insertAt)
	{
		ch->texturedataindex += shift;
		mstudiotexture_t *tex = (mstudiotexture_t *)(C.data() + ch->textureindex);
		for (int t = 0; t < ch->numtextures; t++)
			if ((size_t)tex[t].index >= insertAt) tex[t].index += shift;
	}
	ch->seqindex = (int)seqBase;
	ch->numseq = (int)seqs.size();
	ch->seqgroupindex = (int)groupBase;
	ch->numseqgroups = 1;
	ch->numtransitions = 0;
	ch->length = (int)C.size();

	std::string flat = charRel.substr(7, charRel.size() - 11);
	for (auto &c : flat) if (c == '/' || c == '\\') c = '_';
	std::string outRel = "models/fpbody/rt_" + flat + ".mdl";
	std::string outFull = cofDir + outRel;
	for (auto &c : outFull) if (c == '/') c = '\\';
	if (!WriteAll(outFull, C.data(), C.size()))
		return "";
	std::string srcFull = cofDir + charRel;
	for (auto &c : srcFull) if (c == '/') c = '\\';
	CopyCompanions(srcFull, outFull, NULL, outRel);           // its texture file, if separate
	FpLog("chars: built %s with Simon's animations (%d sequences, size x%.2f)\n", outRel.c_str(), (int)seqs.size(), k);
	return outRel;
}

void FpChars_Init(void)
{
	s_anims = FpRegister("cl_fpbody_anims", "0", FCVAR_ARCHIVE);
}

float FpChars_AnimsMode(void) { return s_anims ? s_anims->value : 0.0f; }

// From the body code, before it builds the body: the model to actually use.
std::string FpChars_Prepare(const char *src)
{
	if (!src || !_stricmp(src, kSimon))
		return src ? src : "";
	if (s_anims && s_anims->value != 0.0f)
	{
		std::vector<byte> d;
		if (ReadAll(CofDir() + src, d) && d.size() >= sizeof(studiohdr_t) && HasOwnWalk((studiohdr_t *)d.data()))
			return src;                                        // its own animations
	}
	std::string rt = Retarget(src);
	return rt.empty() ? src : rt;
}

// The body code copies its built model next to the source; models whose
// textures are in a separate xxxT.mdl need that file under the new name too.
void FpChars_CopyCompanions(const char *srcRel, const char *dstRel)
{
	std::string s = CofDir() + srcRel, d = CofDir() + dstRel;
	for (auto &c : s) if (c == '/') c = '\\';
	for (auto &c : d) if (c == '/') c = '\\';
	CopyCompanions(s, d, NULL, dstRel);
}
