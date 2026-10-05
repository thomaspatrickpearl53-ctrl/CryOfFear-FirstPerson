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
#include <GL/gl.h>

void FpLog(const char *fmt, ...);
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
	GLuint id = 0;
	glGenTextures(1, &id);
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
void FpClothes_NewMap(void)
{
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
