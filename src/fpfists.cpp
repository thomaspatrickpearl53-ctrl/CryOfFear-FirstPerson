// Fists mode: the nightstick becomes bare fists.
//
// A fists viewmodel is written from the player's own game files: the nightstick
// viewmodel with the stick hidden, plus the two punches from v_actions.mdl (the
// game's own bare-hand punch1 / punch2, same arm rig, compiled 2.5x bigger).
// Every animation is decoded and re-encoded into one model so both sets share
// the nightstick's bones. While the nightstick is out with fists mode on:
//   mouse 1 -> nightstick swing (its damage), shown as punch1
//   mouse 2 -> the same swing, shown as punch2
// Idle, draw, holster, sprint and jump use the nightstick's own animations with
// empty hands. The inventory still lists the nightstick. No server code, so it
// works on any engine.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "r_studioint.h"
#include "studio.h"
#include "usercmd.h"
#include "in_buttons.h"

extern cl_enginefunc_t     *eng;
extern engine_studio_api_t  g_studioEng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void    FpLog(const char *fmt, ...);
void    FpGameDir(char *out, size_t size);

static const char *kStickModel   = "models/weapons/nightstick/v_nightstick.mdl";
static const char *kActionsModel = "models/weapons/v_actions.mdl";
static const char *kFistsModel   = "models/fpbody/v_fists.mdl";
static const char *kPunchNames[] = { "punch1", "punch2" };
static const int   kStickAttackFirst = 3, kStickAttackLast = 5;   // attack1..attack3

static cvar_t  *s_fists;
static model_t *s_model;
static bool     s_tried;
static bool     s_holdingStick;
static int      s_punchSeq[2] = { -1, -1 };   // punch1 / punch2 in the fists model
static int      s_punch;                      // 0: mouse 1, 1: mouse 2

static void Cmd_Fists(void)
{
	float on = (s_fists && s_fists->value != 0.0f) ? 0.0f : 1.0f;
	eng->Cvar_SetValue((char *)"cl_fists", on);
	eng->Con_Printf("Fists mode %s (the nightstick becomes fists: mouse 1 and mouse 2 punch)\n", on != 0.0f ? "ON" : "OFF");
}

void FpFists_Init(void)
{
	s_fists = FpRegister("cl_fists", "0", FCVAR_ARCHIVE);
	eng->pfnAddCommand("fp_fists", Cmd_Fists);
}

void FpFists_NewMap(void)
{
	s_model = NULL;
	s_tried = false;
}

// ---------------------------------------------------------------------------
// Model files
// ---------------------------------------------------------------------------

static bool ReadModel(const char *name, std::vector<byte> &data)
{
	char dir[MAX_PATH], path[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	_snprintf(path, sizeof(path), "%s%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data.resize(len);
	fread(data.data(), 1, len, f);
	fclose(f);
	if (len < (long)sizeof(studiohdr_t))
		return false;
	studiohdr_t *hdr = (studiohdr_t *)data.data();
	if (memcmp(&hdr->id, "IDST", 4) || hdr->version != 10 || hdr->numbones > MAXSTUDIOBONES || hdr->numseqgroups != 1)
		return false;
	mstudioseqgroup_t *grp = (mstudioseqgroup_t *)(data.data() + hdr->seqgroupindex);
	return grp->unused2 == 0;                // animations stored in this file
}

static bool WriteModel(const char *name, const std::vector<byte> &data)
{
	char dir[MAX_PATH], path[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	_snprintf(path, sizeof(path), "%smodels\\fpbody", dir);
	CreateDirectoryA(path, NULL);
	_snprintf(path, sizeof(path), "%s%s", dir, name);
	FILE *f = fopen(path, "wb");
	bool ok = f && fwrite(data.data(), 1, data.size(), f) == data.size();
	if (f) fclose(f);
	return ok;
}

static int FindBone(const std::vector<byte> &data, const char *name)
{
	const studiohdr_t *hdr = (const studiohdr_t *)data.data();
	const mstudiobone_t *bones = (const mstudiobone_t *)(data.data() + hdr->boneindex);
	for (int i = 0; i < hdr->numbones; i++)
		if (!_stricmp(bones[i].name, name))
			return i;
	return -1;
}

static int FindSeq(const std::vector<byte> &data, const char *name)
{
	const studiohdr_t *hdr = (const studiohdr_t *)data.data();
	const mstudioseqdesc_t *seqs = (const mstudioseqdesc_t *)(data.data() + hdr->seqindex);
	for (int i = 0; i < hdr->numseq; i++)
		if (!_stricmp(seqs[i].label, name))
			return i;
	return -1;
}

// ---------------------------------------------------------------------------
// Animation decode / encode (GoldSrc run-length animation values)
// ---------------------------------------------------------------------------

// One sequence decoded to floats: frames x bones x 6 channels.
struct Anim
{
	mstudioseqdesc_t   desc;
	int                frames;
	std::vector<float> v;
	float &at(int f, int b, int c, int nb) { return v[((size_t)f * nb + b) * 6 + c]; }
};

static short AnimValue(const mstudioanim_t *anim, int ch, int frame)
{
	if (!anim->offset[ch])
		return 0;
	const mstudioanimvalue_t *p = (const mstudioanimvalue_t *)((const byte *)anim + anim->offset[ch]);
	int k = frame;
	while (p->num.total <= k)
	{
		k -= p->num.total;
		p += p->num.valid + 1;
	}
	return p->num.valid > k ? p[k + 1].value : p[p->num.valid].value;
}

// Decode a sequence of `src`, mapping its bones onto `dst`'s by name. Positions
// are multiplied by posScale (the two models are compiled at different sizes).
// Bones `src` lacks keep `dst`'s rest pose.
static void Decode(const std::vector<byte> &src, int seq, const std::vector<byte> &dst, float posScale, Anim &out)
{
	const studiohdr_t *sh = (const studiohdr_t *)src.data();
	const studiohdr_t *dh = (const studiohdr_t *)dst.data();
	const mstudiobone_t *sb = (const mstudiobone_t *)(src.data() + sh->boneindex);
	const mstudiobone_t *db = (const mstudiobone_t *)(dst.data() + dh->boneindex);
	const mstudioseqdesc_t *sd = (const mstudioseqdesc_t *)(src.data() + sh->seqindex) + seq;
	const mstudioanim_t *anims = (const mstudioanim_t *)(src.data() + sd->animindex);

	out.desc = *sd;
	out.frames = sd->numframes > 0 ? sd->numframes : 1;
	int nb = dh->numbones;
	out.v.assign((size_t)out.frames * nb * 6, 0.0f);
	for (int b = 0; b < nb; b++)
	{
		int s = FindBone(src, db[b].name);
		for (int f = 0; f < out.frames; f++)
			for (int c = 0; c < 6; c++)
			{
				float val;
				if (s < 0)
					val = db[b].value[c];
				else
				{
					val = sb[s].value[c] + AnimValue(&anims[s], c, f) * sb[s].scale[c];
					if (c < 3)
						val *= posScale;
				}
				out.at(f, b, c, nb) = val;
			}
	}
}

static void Put(std::vector<byte> &data, const void *p, size_t n)
{
	data.insert(data.end(), (const byte *)p, (const byte *)p + n);
}

static void Align4(std::vector<byte> &data)
{
	while (data.size() & 3)
		data.push_back(0);
}

// ---------------------------------------------------------------------------
// Build the fists model
// ---------------------------------------------------------------------------

static bool BuildFists(void)
{
	std::vector<byte> stick, actions;
	if (!ReadModel(kStickModel, stick) || !ReadModel(kActionsModel, actions))
	{
		FpLog("fists: can't read %s / %s\n", kStickModel, kActionsModel);
		return false;
	}

	// Size ratio from the wrist bone's offset along the forearm.
	float posScale = 1.0f;
	int ws = FindBone(stick, "r_wr"), wa = FindBone(actions, "r_wr");
	if (ws >= 0 && wa >= 0)
	{
		const mstudiobone_t *bs = (const mstudiobone_t *)(stick.data() + ((studiohdr_t *)stick.data())->boneindex);
		const mstudiobone_t *ba = (const mstudiobone_t *)(actions.data() + ((studiohdr_t *)actions.data())->boneindex);
		if (fabs(ba[wa].value[0]) > 0.001f)
			posScale = bs[ws].value[0] / ba[wa].value[0];
	}

	std::vector<Anim> anims;
	studiohdr_t *hdr = (studiohdr_t *)stick.data();
	int nb = hdr->numbones;
	for (int i = 0; i < hdr->numseq; i++)
	{
		anims.emplace_back();
		Decode(stick, i, stick, 1.0f, anims.back());
	}
	for (int p = 0; p < 2; p++)
	{
		int s = FindSeq(actions, kPunchNames[p]);
		if (s < 0)
		{
			FpLog("fists: %s has no %s\n", kActionsModel, kPunchNames[p]);
			return false;
		}
		s_punchSeq[p] = (int)anims.size();
		anims.emplace_back();
		Decode(actions, s, stick, posScale, anims.back());
		Anim &a = anims.back();
		a.desc.numevents = a.desc.eventindex = 0;
		a.desc.numpivots = a.desc.pivotindex = 0;
		a.desc.motiontype = a.desc.motionbone = 0;
		VectorClear(a.desc.linearmovement);
		a.desc.entrynode = a.desc.exitnode = a.desc.nodeflags = a.desc.nextseq = 0;
		a.desc.seqgroup = 0;
		a.desc.numblends = 1;
	}

	// Hide the stick: every body part except the hands collapses to its bones.
	mstudiobodyparts_t *parts = (mstudiobodyparts_t *)(stick.data() + hdr->bodypartindex);
	for (int p = 0; p < hdr->numbodyparts; p++)
	{
		mstudiomodel_t *models = (mstudiomodel_t *)(stick.data() + parts[p].modelindex);
		for (int m = 0; m < parts[p].nummodels; m++)
		{
			if (strstr(models[m].name, "hand"))
				continue;
			vec3_t *verts = (vec3_t *)(stick.data() + models[m].vertindex);
			for (int v = 0; v < models[m].numverts; v++)
				VectorClear(verts[v]);
		}
	}

	// New per-channel scales covering every sequence's range around the stick's
	// rest values (bone value[] stays as it is).
	mstudiobone_t *bones = (mstudiobone_t *)(stick.data() + hdr->boneindex);
	for (int b = 0; b < nb; b++)
		for (int c = 0; c < 6; c++)
		{
			float dev = 0.0f;
			for (size_t i = 0; i < anims.size(); i++)
				for (int f = 0; f < anims[i].frames; f++)
					dev = max(dev, (float)fabs(anims[i].at(f, b, c, nb) - bones[b].value[c]));
			bones[b].scale[c] = dev > 1e-6f ? dev / 32000.0f : 1.0f;
		}
	// Appending below moves the buffer, so keep a copy of the bones.
	std::vector<mstudiobone_t> boneCopy(bones, bones + nb);
	bones = boneCopy.data();

	// Re-encode every sequence at the end of the file, then a new sequence table.
	for (size_t i = 0; i < anims.size(); i++)
	{
		Align4(stick);
		size_t base = stick.size();
		std::vector<mstudioanim_t> table(nb);
		std::vector<short> values;
		size_t tableBytes = nb * sizeof(mstudioanim_t);
		for (int b = 0; b < nb; b++)
			for (int c = 0; c < 6; c++)
			{
				std::vector<short> raw(anims[i].frames);
				bool any = false;
				for (int f = 0; f < anims[i].frames; f++)
				{
					float q = (anims[i].at(f, b, c, nb) - bones[b].value[c]) / bones[b].scale[c];
					raw[f] = (short)floorf(q + 0.5f);
					any |= raw[f] != 0;
				}
				if (!any)
				{
					table[b].offset[c] = 0;
					continue;
				}
				size_t off = tableBytes + values.size() * sizeof(short) - b * sizeof(mstudioanim_t);
				if (off > 0xFFFF)
				{
					FpLog("fists: animation too big\n");
					return false;
				}
				table[b].offset[c] = (unsigned short)off;
				for (int f = 0; f < anims[i].frames; f += 255)
				{
					int n = min(255, anims[i].frames - f);
					mstudioanimvalue_t head;
					head.num.valid = head.num.total = (byte)n;
					values.push_back(head.value);
					values.insert(values.end(), raw.begin() + f, raw.begin() + f + n);
				}
			}
		Put(stick, table.data(), tableBytes);
		Put(stick, values.data(), values.size() * sizeof(short));
		anims[i].desc.animindex = (int)base;
	}
	Align4(stick);
	size_t seqBase = stick.size();
	for (size_t i = 0; i < anims.size(); i++)
		Put(stick, &anims[i].desc, sizeof(mstudioseqdesc_t));
	hdr = (studiohdr_t *)stick.data();
	hdr->seqindex = (int)seqBase;
	hdr->numseq = (int)anims.size();
	hdr->length = (int)stick.size();

	if (!WriteModel(kFistsModel, stick))
	{
		FpLog("fists: can't write %s\n", kFistsModel);
		return false;
	}
	FpLog("fists: built %s (%d sequences, punches at %d/%d, size ratio %.3f)\n",
		kFistsModel, hdr->numseq, s_punchSeq[0], s_punchSeq[1], posScale);
	return true;
}

static void LoadFists(void)
{
	if (s_tried)
		return;
	s_tried = true;
	if (!BuildFists() || !g_studioEng.Mod_ForName)
		return;
	model_t *m = g_studioEng.Mod_ForName(kFistsModel, 0);
	s_model = (m && m->type == mod_studio) ? m : NULL;
	if (!s_model)
		FpLog("fists: couldn't load %s\n", kFistsModel);
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------

static bool FistsOn(void)
{
	return s_holdingStick && s_fists && s_fists->value != 0.0f;
}

// After V_CalcRefdef: swap the nightstick for the fists and its swings for punches.
void FpFists_Frame(void)
{
	cl_entity_t *vm = eng->GetViewModel();
	if (vm && vm->model && vm->model == s_model)
		s_holdingStick = true;                  // still ours from last frame
	else
		s_holdingStick = vm && vm->model && strstr(vm->model->name, "v_nightstick.mdl") != NULL;
	if (!FistsOn())
		return;
	LoadFists();
	if (!s_model)
		return;
	vm->model = s_model;
	int seq = vm->curstate.sequence;
	if (seq >= kStickAttackFirst && seq <= kStickAttackLast && s_punchSeq[s_punch] >= 0)
		vm->curstate.sequence = s_punchSeq[s_punch];
}

// Mouse 2 (+attack2, or +attack3 on Cry of Fear: Enhanced) swings too.
void FpFists_CreateMove(usercmd_t *cmd)
{
	if (!FistsOn())
		return;
	const int mouse2 = IN_ATTACK2 | IN_ALT1;
	if (cmd->buttons & IN_ATTACK)
		s_punch = 0;
	else if (cmd->buttons & mouse2)
	{
		cmd->buttons = (cmd->buttons & ~mouse2) | IN_ATTACK;
		s_punch = 1;
	}
}
