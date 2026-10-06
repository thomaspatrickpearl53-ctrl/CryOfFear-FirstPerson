// Cry of Fear costumes for the F8 menu's Clothes tab: switching and previews.
//
// Switching: the game's wardrobe buttons don't only send "setclothes N". They
// also store N in a client global and clear four "texture already loaded"
// flags, and the client reloads the costume textures only when those flags are
// clear. Sending the command alone changes the costume once and never again.
// FpClothes_Select does what the wardrobe does. The globals' addresses are read
// at runtime from the game's own "setclothes 1" button code:
//
//     push  "setclothes 1"
//     mov   dword [costume], 1
//     mov   byte  [flag1], 0   ... four flags
//
// so nothing is hard-coded, and if the code doesn't match (another game
// version) nothing is written and the command is just sent.
//
// Previews: the costume's own texture files (models/costumes/<name>/<name>_
// Hoodie/Jeans/Sleeve/Face.tga). Parts a costume doesn't replace show Simon's
// normal textures, read from his models.

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <math.h>
#include <GL/gl.h>

#include "mathlib.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"

void FpLog(const char *fmt, ...);
bool FpGL_Init(void);
GLuint FpGL_NewTexture(void);
void FpGL_Use(GLuint prog);
void FpGL_ActiveTexture(int unit);
void FpGameDir(char *out, size_t size);

#ifndef GL_BGR_EXT
#define GL_BGR_EXT  0x80E0
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// ---------------------------------------------------------------------------
// Switching
// ---------------------------------------------------------------------------

static int  *s_costume;          // client_cof's current costume number
static byte *s_flags[4];         // its "costume texture loaded" flags
static bool  s_searched;

static bool FindGlobals(void)
{
	if (s_searched)
		return s_costume != NULL;
	s_searched = true;
	HMODULE mod = GetModuleHandleA("client_cof.dll");
	MODULEINFO mi;
	if (!mod || !GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi)))
		return false;
	byte *base = (byte *)mi.lpBaseOfDll, *end = base + mi.SizeOfImage;
	auto inside = [&](DWORD a) { return (byte *)(UINT_PTR)a >= base && (byte *)(UINT_PTR)a < end; };

	static const char kStr[] = "setclothes 1";
	byte *str = NULL;
	for (byte *p = base; p + sizeof(kStr) <= end; p++)
		if (!memcmp(p, kStr, sizeof(kStr)) && (p == base || p[-1] == 0))
		{
			str = p;
			break;
		}
	if (!str)
	{
		FpLog("clothes: game client has no setclothes code\n");
		return false;
	}
	byte push[5] = { 0x68 };
	DWORD strVa = (DWORD)(UINT_PTR)str;
	memcpy(push + 1, &strVa, 4);
	for (byte *p = base; p + 38 <= end; p++)
	{
		if (memcmp(p, push, 5) || p[5] != 0xC7 || p[6] != 0x05)
			continue;
		DWORD costume = *(DWORD *)(p + 7), value = *(DWORD *)(p + 11);
		if (value != 1 || !inside(costume))
			continue;
		byte *q = p + 15;
		DWORD flags[4];
		bool ok = true;
		for (int i = 0; i < 4 && ok; i++, q += 7)
		{
			ok = q[0] == 0xC6 && q[1] == 0x05 && q[6] == 0x00;
			flags[i] = ok ? *(DWORD *)(q + 2) : 0;
			ok = ok && inside(flags[i]);
		}
		if (!ok)
			continue;
		s_costume = (int *)(UINT_PTR)costume;
		for (int i = 0; i < 4; i++)
			s_flags[i] = (byte *)(UINT_PTR)flags[i];
		FpLog("clothes: found the wardrobe state in client_cof.dll\n");
		return true;
	}
	FpLog("clothes: wardrobe code not recognised; switching may only work once\n");
	return false;
}

// Before "setclothes N": what the game's wardrobe button does on the client.
void FpClothes_Select(int n)
{
	if (!FindGlobals())
		return;
	*s_costume = n;
	for (int i = 0; i < 4; i++)
		*s_flags[i] = 0;
}

// ---------------------------------------------------------------------------
// Preview textures
// ---------------------------------------------------------------------------

static const char *const kFolders[] = {
	NULL, "Leatherhoff", "ModDB", "Hello_Kitty", "AoM", "Camo", "HLC",
	"Black_Metal", "Team_Psykskallar", "Awesome", "Sick_Simon", "Twitcher",
};
static const int   kNumCostumes = 13;                 // 0 normal, 1-11, 12 custom
static const char *const kParts[] = { "Hoodie", "Jeans", "Sleeve", "Face" };
static const int   kNumParts = 4;

struct Tex { GLuint id; int w, h; bool tried; bool own; };   // own: the costume's file, not Simon's normal one
static Tex s_tex[kNumCostumes][kNumParts];
static Tex s_default[kNumParts];

static bool ReadFile(const char *rel, std::vector<byte> &out)
{
	char dir[MAX_PATH], path[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	_snprintf(path, sizeof(path), "%s%s", dir, rel);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(len > 0 ? len : 0);
	bool ok = len > 0 && fread(out.data(), 1, len, f) == (size_t)len;
	fclose(f);
	return ok;
}

static GLuint Upload(const byte *rgba, int w, int h)
{
	GLuint id = FpGL_NewTexture();
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	return id;
}

// Truevision TGA: uncompressed (2) or run-length (10), 24 or 32 bit.
static bool LoadTga(const char *rel, Tex &t)
{
	std::vector<byte> f;
	if (!ReadFile(rel, f) || f.size() < 18)
		return false;
	int idLen = f[0], type = f[2], w = f[12] | (f[13] << 8), h = f[14] | (f[15] << 8), bpp = f[16], desc = f[17];
	if ((type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0 || w > 4096 || h > 4096 || f[1] != 0)
		return false;
	int px = bpp / 8;
	size_t p = 18 + idLen;
	std::vector<byte> rgba((size_t)w * h * 4);
	size_t n = 0, total = (size_t)w * h;
	auto put = [&](const byte *s) {
		size_t row = n / w, col = n % w;
		if (!(desc & 0x20)) row = h - 1 - row;            // bottom-up unless the top-left bit is set
		byte *d = &rgba[(row * w + col) * 4];
		d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = px == 4 ? s[3] : 255;
		n++;
	};
	while (n < total)
	{
		if (type == 2)
		{
			if (p + px > f.size()) return false;
			put(&f[p]); p += px;
			continue;
		}
		if (p >= f.size()) return false;
		int hdr = f[p++], count = (hdr & 0x7F) + 1;
		if (hdr & 0x80)
		{
			if (p + px > f.size()) return false;
			for (int i = 0; i < count && n < total; i++) put(&f[p]);
			p += px;
		}
		else
			for (int i = 0; i < count && n < total; i++)
			{
				if (p + px > f.size()) return false;
				put(&f[p]); p += px;
			}
	}
	t.id = Upload(rgba.data(), w, h);
	t.w = w; t.h = h;
	return t.id != 0;
}

// An 8-bit texture inside a studio model, by name.
static bool LoadModelTexture(const char *model, const char *name, Tex &t)
{
	std::vector<byte> d;
	if (!ReadFile(model, d) || d.size() < 0xC0 || memcmp(d.data(), "IDST", 4))
		return false;
	int numtex = *(int *)&d[0xB4], texindex = *(int *)&d[0xB8];
	for (int i = 0; i < numtex; i++)
	{
		size_t o = (size_t)texindex + i * 80;
		if (o + 80 > d.size()) return false;
		if (_stricmp((const char *)&d[o], name))
			continue;
		int w = *(int *)&d[o + 68], h = *(int *)&d[o + 72], index = *(int *)&d[o + 76];
		if (w <= 0 || h <= 0 || (size_t)index + (size_t)w * h + 768 > d.size())
			return false;
		const byte *pix = &d[index], *pal = pix + (size_t)w * h;
		std::vector<byte> rgba((size_t)w * h * 4);
		for (size_t k = 0; k < (size_t)w * h; k++)
		{
			const byte *c = pal + pix[k] * 3;
			rgba[k * 4 + 0] = c[0]; rgba[k * 4 + 1] = c[1]; rgba[k * 4 + 2] = c[2]; rgba[k * 4 + 3] = 255;
		}
		t.id = Upload(rgba.data(), w, h);
		t.w = w; t.h = h;
		return t.id != 0;
	}
	return false;
}

static const Tex *DefaultPart(int part)
{
	Tex &t = s_default[part];
	if (!t.tried)
	{
		t.tried = true;
		switch (part)
		{
		case 0: LoadModelTexture("models/cutscene/player.mdl", "hoodie_grey.bmp", t); break;
		case 1: LoadModelTexture("models/cutscene/player.mdl", "jeans.bmp", t); break;
		case 2: LoadModelTexture("models/weapons/v_actions.mdl", "sleeve.bmp", t); break;
		case 3: LoadModelTexture("models/cutscene/player.mdl", "head_face.bmp", t); break;
		}
	}
	return t.id ? &t : NULL;
}

// Texture of one part of a costume for the preview; *own = false when the costume
// keeps Simon's normal part. NULL if there's nothing to show.
const void *FpClothes_Part(int costume, int part, unsigned *texId, int *w, int *h, bool *own)
{
	if (costume < 0 || costume >= kNumCostumes || part < 0 || part >= kNumParts)
		return NULL;
	Tex &t = s_tex[costume][part];
	if (!t.tried)
	{
		t.tried = true;
		char rel[160];
		if (costume >= 1 && costume <= 11)
		{
			_snprintf(rel, sizeof(rel), "models/costumes/%s/%s_%s.tga", kFolders[costume], kFolders[costume], kParts[part]);
			t.own = LoadTga(rel, t);
		}
		else if (costume == 12 && part == 0)
			t.own = LoadTga("models/costumes/custom_hoodie.tga", t);
	}
	const Tex *use = t.id ? &t : DefaultPart(part);
	if (!use)
		return NULL;
	if (part == 3 && !t.id)
		return NULL;                     // only show a face when the costume has its own
	*texId = use->id; *w = use->w; *h = use->h; *own = use->id == t.id && t.id;
	return use;
}

const char *FpClothes_PartName(int part)
{
	static const char *const names[] = { "Hoodie", "Jeans", "Sleeves", "Face" };
	return part >= 0 && part < kNumParts ? names[part] : "";
}

int FpClothes_NumParts(void) { return kNumParts; }

// Textures belong to the GL context; the renderer can be restarted on video
// changes, so forget them on map load and load them again when needed.
static void Free3D(void);

void FpClothes_NewMap(void)
{
	Free3D();
	for (int c = 0; c < kNumCostumes; c++)
		for (int p = 0; p < kNumParts; p++)
		{
			if (s_tex[c][p].id) glDeleteTextures(1, &s_tex[c][p].id);
			s_tex[c][p] = Tex();
		}
	for (int p = 0; p < kNumParts; p++)
	{
		if (s_default[p].id) glDeleteTextures(1, &s_default[p].id);
		s_default[p] = Tex();
	}
}

// ---------------------------------------------------------------------------
// 3D preview: Simon (models/cutscene/player.mdl) playing his idle animation,
// turning slowly, wearing the costume under the cursor. Drawn by the mod with
// OpenGL inside the menu panel: the game itself can only show the costume
// that's actually worn.
// ---------------------------------------------------------------------------

static const char *kSimonModel = "models/cutscene/player.mdl";

struct Simon
{
	std::vector<byte> data;
	studiohdr_t *hdr = NULL;
	mstudiomodel_t *model = NULL;
	int idleSeq = 0;
	std::vector<GLuint> tex;          // per texture of the model
	std::vector<int> part;            // per texture: costume part it shows (-1 none)
	std::vector<bool> masked;
	float bmin[3], bmax[3];
	bool tried = false, ok = false;
};
static Simon s_simon;

static void Free3D(void)
{
	for (GLuint t : s_simon.tex)
		if (t) glDeleteTextures(1, &t);
	s_simon = Simon();
}

static bool Load3D(void)
{
	Simon &S = s_simon;
	if (S.tried)
		return S.ok;
	S.tried = true;
	if (!ReadFile(kSimonModel, S.data) || S.data.size() < sizeof(studiohdr_t))
		return false;
	S.hdr = (studiohdr_t *)S.data.data();
	studiohdr_t *h = S.hdr;
	mstudioseqgroup_t *grp = (mstudioseqgroup_t *)(S.data.data() + h->seqgroupindex);
	if (memcmp(&h->id, "IDST", 4) || h->version != 10 || h->numbodyparts < 1 || grp->unused2 != 0)
		return false;
	mstudiobodyparts_t *bp = (mstudiobodyparts_t *)(S.data.data() + h->bodypartindex);
	S.model = (mstudiomodel_t *)(S.data.data() + bp[0].modelindex);

	mstudioseqdesc_t *seqs = (mstudioseqdesc_t *)(S.data.data() + h->seqindex);
	for (int i = 0; i < h->numseq; i++)
		if (strstr(seqs[i].label, "idle")) { S.idleSeq = i; break; }

	mstudiotexture_t *tx = (mstudiotexture_t *)(S.data.data() + h->textureindex);
	for (int i = 0; i < h->numtextures; i++)
	{
		int w = tx[i].width, hh = tx[i].height;
		const byte *pix = S.data.data() + tx[i].index, *pal = pix + w * hh;
		bool masked = (tx[i].flags & 0x40) != 0;               // STUDIO_NF_MASKED: index 255 is see-through
		std::vector<byte> rgba((size_t)w * hh * 4);
		for (int k = 0; k < w * hh; k++)
		{
			const byte *c = pal + pix[k] * 3;
			rgba[k * 4] = c[0]; rgba[k * 4 + 1] = c[1]; rgba[k * 4 + 2] = c[2];
			rgba[k * 4 + 3] = masked && pix[k] == 255 ? 0 : 255;
		}
		S.tex.push_back(Upload(rgba.data(), w, hh));
		S.masked.push_back(masked);
		const char *n = tx[i].name;
		S.part.push_back(strstr(n, "hoodie") ? 0 : strstr(n, "jeans") ? 1 : strstr(n, "face") ? 3 : -1);
	}
	for (int k = 0; k < 3; k++) { S.bmin[k] = 1e9f; S.bmax[k] = -1e9f; }
	S.ok = true;
	return true;
}

static void AngleQuat(const float *a, float *q)
{
	float sr = sinf(a[0] * 0.5f), cr = cosf(a[0] * 0.5f);
	float sp = sinf(a[1] * 0.5f), cp = cosf(a[1] * 0.5f);
	float sy = sinf(a[2] * 0.5f), cy = cosf(a[2] * 0.5f);
	q[0] = sr * cp * cy - cr * sp * sy;
	q[1] = cr * sp * cy + sr * cp * sy;
	q[2] = cr * cp * sy - sr * sp * cy;
	q[3] = cr * cp * cy + sr * sp * sy;
}

static void QuatMatrix(const float *q, const float *pos, float m[3][4])
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0][0] = 1 - 2 * y * y - 2 * z * z; m[0][1] = 2 * x * y - 2 * w * z;     m[0][2] = 2 * x * z + 2 * w * y;     m[0][3] = pos[0];
	m[1][0] = 2 * x * y + 2 * w * z;     m[1][1] = 1 - 2 * x * x - 2 * z * z; m[1][2] = 2 * y * z - 2 * w * x;     m[1][3] = pos[1];
	m[2][0] = 2 * x * z - 2 * w * y;     m[2][1] = 2 * y * z + 2 * w * x;     m[2][2] = 1 - 2 * x * x - 2 * y * y; m[2][3] = pos[2];
}

static void Concat(const float a[3][4], const float b[3][4], float out[3][4])
{
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 3; c++)
			out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
		out[r][3] = a[r][0] * b[0][3] + a[r][1] * b[1][3] + a[r][2] * b[2][3] + a[r][3];
	}
}

static short AnimVal(const mstudioanim_t *anim, int ch, int frame)
{
	if (!anim->offset[ch])
		return 0;
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

// Bone transforms of the idle animation at time t.
static void Pose(float t, std::vector<float> &bones)
{
	Simon &S = s_simon;
	studiohdr_t *h = S.hdr;
	mstudiobone_t *b = (mstudiobone_t *)(S.data.data() + h->boneindex);
	mstudioseqdesc_t *sd = (mstudioseqdesc_t *)(S.data.data() + h->seqindex) + S.idleSeq;
	mstudioanim_t *anim = (mstudioanim_t *)(S.data.data() + sd->animindex);
	int frames = sd->numframes > 1 ? sd->numframes : 1;
	int frame = (int)(t * (sd->fps > 0 ? sd->fps : 10.0f)) % frames;
	bones.resize((size_t)h->numbones * 12);
	for (int i = 0; i < h->numbones; i++)
	{
		float v[6];
		for (int c = 0; c < 6; c++)
			v[c] = b[i].value[c] + AnimVal(&anim[i], c, frame) * b[i].scale[c];
		float q[4], local[3][4];
		AngleQuat(v + 3, q);
		QuatMatrix(q, v, local);
		float (*m)[4] = (float (*)[4])&bones[(size_t)i * 12];
		if (b[i].parent >= 0)
			Concat((float (*)[4])&bones[(size_t)b[i].parent * 12], local, m);
		else
			memcpy(m, local, sizeof(local));
	}
}

// Draws Simon in the costume into the screen rectangle (x, y, w, h), menu pixels.
void FpClothes_Draw3D(int costume, int x, int y, int w, int h, int screenW, int screenH, float time)
{
	if (!Load3D() || w < 32 || h < 32)
		return;
	Simon &S = s_simon;
	mstudiomodel_t *m = S.model;
	std::vector<float> bones;
	Pose(time, bones);

	// Skin: every vertex and normal follows its one bone.
	const byte *vbone = S.data.data() + m->vertinfoindex, *nbone = S.data.data() + m->norminfoindex;
	const vec3_t *verts = (const vec3_t *)(S.data.data() + m->vertindex);
	const vec3_t *norms = (const vec3_t *)(S.data.data() + m->normindex);
	std::vector<float> pv((size_t)m->numverts * 3), pn((size_t)m->numnorms * 3);
	float yaw = time * 0.6f, cy = cosf(yaw), sy = sinf(yaw);
	float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
	for (int i = 0; i < m->numverts; i++)
	{
		const float (*b)[4] = (const float (*)[4])&bones[(size_t)vbone[i] * 12];
		const float *p = verts[i];
		float wx = b[0][0] * p[0] + b[0][1] * p[1] + b[0][2] * p[2] + b[0][3];
		float wy = b[1][0] * p[0] + b[1][1] * p[1] + b[1][2] * p[2] + b[1][3];
		float wz = b[2][0] * p[0] + b[2][1] * p[1] + b[2][2] * p[2] + b[2][3];
		// turn around the vertical axis, then to eye space: x = model left, y = up, z = toward the camera
		float rx = wx * cy - wy * sy, ry = wx * sy + wy * cy;
		pv[i * 3] = ry; pv[i * 3 + 1] = wz; pv[i * 3 + 2] = rx;
		for (int k = 0; k < 3; k++) { lo[k] = fminf(lo[k], pv[i * 3 + k]); hi[k] = fmaxf(hi[k], pv[i * 3 + k]); }
	}
	for (int i = 0; i < m->numnorms; i++)
	{
		const float (*b)[4] = (const float (*)[4])&bones[(size_t)nbone[i] * 12];
		const float *n = norms[i];
		float wx = b[0][0] * n[0] + b[0][1] * n[1] + b[0][2] * n[2];
		float wy = b[1][0] * n[0] + b[1][1] * n[1] + b[1][2] * n[2];
		float wz = b[2][0] * n[0] + b[2][1] * n[1] + b[2][2] * n[2];
		float rx = wx * cy - wy * sy, ry = wx * sy + wy * cy;
		pn[i * 3] = ry; pn[i * 3 + 1] = wz; pn[i * 3 + 2] = rx;
	}
	// Fit the standing model in the box (a stable height so turning doesn't zoom).
	if (S.bmin[0] > S.bmax[0])
		for (int k = 0; k < 3; k++) { S.bmin[k] = lo[k]; S.bmax[k] = hi[k]; }
	float height = S.bmax[1] - S.bmin[1], centreY = (S.bmax[1] + S.bmin[1]) * 0.5f;
	float aspect = (float)w / h, fovTan = 0.42f;
	float dist = height * 0.56f / fovTan;            // the model fills about 90% of the height

	// The menu works in its own pixel size; GL wants window pixels, bottom-up.
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	float sx = (float)vp[2] / screenW, syy = (float)vp[3] / screenH;
	int gx = vp[0] + (int)(x * sx), gw = (int)(w * sx), gh = (int)(h * syy);
	int gy = vp[1] + vp[3] - (int)((y + h) * syy);

	glPushAttrib(GL_ALL_ATTRIB_BITS);
	if (FpGL_Init())                // plain fixed-function drawing, texture unit 0
	{
		FpGL_Use(0);
		FpGL_ActiveTexture(0);
	}
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
	float n = 4.0f, f = dist + 200.0f;
	glFrustum(-fovTan * aspect * n, fovTan * aspect * n, -fovTan * n, fovTan * n, n, f);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
	glTranslatef(0, -centreY, -dist);
	glViewport(gx, gy, gw, gh);
	glEnable(GL_SCISSOR_TEST);
	glScissor(gx, gy, gw, gh);
	// GoldSrc flips its depth range and test every other frame (gl_ztrick) and may
	// leave other state changed: draw with a plain, normal setup.
	glDepthRange(0.0, 1.0);
	glClearDepth(1.0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDisable(GL_STENCIL_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_SMOOTH);
	glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	// Depth writes must be on for the clear to do anything (the HUD pass often
	// has them off), or the map's depth shows through and cuts the model.
	glDepthMask(GL_TRUE);
	glClearColor(0.05f, 0.05f, 0.055f, 1.0f);       // solid backdrop: the map doesn't show through
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glAlphaFunc(GL_GREATER, 0.5f);

	const float L[3] = { 0.45f, 0.55f, 0.70f };              // light from the upper front
	mstudiotexture_t *tx = (mstudiotexture_t *)(S.data.data() + S.hdr->textureindex);
	const short *skins = (const short *)(S.data.data() + S.hdr->skinindex);
	mstudiomesh_t *meshes = (mstudiomesh_t *)(S.data.data() + m->meshindex);
	for (int k = 0; k < m->nummesh; k++)
	{
		int ti = skins[meshes[k].skinref];
		if (ti < 0 || ti >= (int)S.tex.size())
			continue;
		GLuint tex = S.tex[ti];
		if (S.part[ti] >= 0)
		{
			unsigned id; int tw, th; bool own;
			if (FpClothes_Part(costume, S.part[ti], &id, &tw, &th, &own) && own)
				tex = id;                                     // the costume's own texture
		}
		glBindTexture(GL_TEXTURE_2D, tex);
		if (S.masked[ti]) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
		float iw = 1.0f / tx[ti].width, ih = 1.0f / tx[ti].height;
		const short *cmd = (const short *)(S.data.data() + meshes[k].triindex);
		int count;
		while ((count = *cmd++) != 0)
		{
			glBegin(count < 0 ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP);
			for (int c = count < 0 ? -count : count; c > 0; c--, cmd += 4)
			{
				const float *nn = &pn[(size_t)cmd[1] * 3];
				float lit = 0.5f + 0.75f * fmaxf(0.0f, nn[0] * L[0] + nn[1] * L[1] + nn[2] * L[2]);
				if (lit > 1.0f) lit = 1.0f;
				glColor4f(lit, lit, lit, 1.0f);
				glTexCoord2f(cmd[2] * iw, cmd[3] * ih);
				const float *p = &pv[(size_t)cmd[0] * 3];
				glVertex3f(p[0], p[1], p[2]);
			}
			glEnd();
		}
	}

	glMatrixMode(GL_TEXTURE); glPopMatrix();
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW); glPopMatrix();
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glPopAttrib();
}
