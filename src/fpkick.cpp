// Kick (+fp_kick, bound to F): Brutal Half-Life's kick played with Simon's leg.
//
// The leg model, models/fpbody/v_kick.mdl, is made by tools/build_kick.py from
// two models on the player's own PC (Brutal Half-Life's v_squeak.mdl for the
// leg rig and kick animations, Cry of Fear's player.mdl for Simon's leg). If it
// isn't there, the kick does nothing. Visual only: the kick deals no damage.
//
// The leg is drawn as an extra entity placed exactly where the viewmodel is, so
// it moves with the hands and whatever weapon is out stays visible.

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "r_studioint.h"
#include "studio.h"
#include "ref_params.h"
#include "entity_types.h"

extern cl_enginefunc_t     *eng;
extern engine_studio_api_t  g_studioEng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void    FpLog(const char *fmt, ...);
void    FpGameDir(char *out, size_t size);
bool    FpCam_PlayerView(void);

static const char *kKickModel = "models/fpbody/v_kick.mdl";
static const int   kKickSeq   = 0;          // FISTS_KICK

static cvar_t      *s_enable, *s_bound;
static model_t     *s_model;
static bool         s_tried;
static float        s_start = -1.0f;        // client time the kick started
static float        s_length = 0.65f;       // seconds, from the sequence
static int          s_frames = 13;
static bool         s_drawn;                 // added to the frame by FpKick_CreateEntities
static cl_entity_t  s_ent;

static bool ModelFileExists(void)
{
	char dir[MAX_PATH], path[MAX_PATH];
	FpGameDir(dir, sizeof(dir));
	_snprintf(path, sizeof(path), "%s%s", dir, kKickModel);
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void Cmd_KickDown(void)
{
	if (!s_enable || s_enable->value == 0.0f)
		return;
	float now = eng->GetClientTime();
	if (s_start < 0.0f || now - s_start >= s_length)
		s_start = now;
}

static void Cmd_KickUp(void)
{
}

void FpKick_Init(void)
{
	s_enable = FpRegister("cl_fpkick", "1", FCVAR_ARCHIVE);
	s_bound  = FpRegister("cl_fpkick_bound", "0", FCVAR_ARCHIVE);
	eng->pfnAddCommand("+fp_kick", Cmd_KickDown);
	eng->pfnAddCommand("-fp_kick", Cmd_KickUp);
}

void FpKick_NewMap(void)
{
	s_model = NULL;
	s_tried = false;
	s_start = -1.0f;
}

static void LoadModel(void)
{
	if (s_tried)
		return;
	s_tried = true;
	if (!ModelFileExists() || !g_studioEng.Mod_ForName)
		return;
	model_t *m = g_studioEng.Mod_ForName(kKickModel, 0);
	if (!m || m->type != mod_studio)
	{
		FpLog("fpkick: can't load %s\n", kKickModel);
		return;
	}
	studiohdr_t *hdr = (studiohdr_t *)g_studioEng.Mod_Extradata(m);
	if (!hdr || hdr->numseq <= kKickSeq)
		return;
	mstudioseqdesc_t *sd = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + kKickSeq;
	s_frames = sd->numframes > 1 ? sd->numframes : 2;
	s_length = (s_frames - 1) / (sd->fps > 0.0f ? sd->fps : 20.0f);
	s_model = m;

	memset(&s_ent, 0, sizeof(s_ent));
	s_ent.model = m;
	s_ent.curstate.rendermode = kRenderNormal;
	s_ent.curstate.renderamt = 255;
	s_ent.curstate.sequence = kKickSeq;
	for (int i = 0; i < 4; i++) s_ent.curstate.controller[i] = 127;
	for (int i = 0; i < 2; i++) s_ent.curstate.blending[i] = 127;
	FpLog("fpkick: loaded %s (%d frames, %.2f s)\n", kKickModel, s_frames, s_length);
}

// From HUD_CreateEntities (before the frame is drawn, the safe place to load models).
void FpKick_CreateEntities(void)
{
	s_drawn = false;
	if (!s_enable || s_enable->value == 0.0f)
		return;
	LoadModel();
	if (!s_model)
		return;

	// First time the leg model is available: put the kick on F once.
	if (s_bound && s_bound->value == 0.0f)
	{
		eng->pfnClientCmd((char *)"bind f +fp_kick\n");
		eng->Cvar_SetValue((char *)"cl_fpkick_bound", 1.0f);
		eng->Con_Printf("fpkick: kick bound to F\n");
	}

	float now = eng->GetClientTime();
	if (s_start < 0.0f || now - s_start >= s_length || !FpCam_PlayerView())
		return;
	eng->CL_CreateVisibleEntity(ET_NORMAL, &s_ent);
	s_drawn = true;
}

// After V_CalcRefdef and the camera effects: put the leg where the hands are.
void FpKick_CalcRefdef(ref_params_t *pp)
{
	if (!s_drawn)
		return;
	cl_entity_t *vm = eng->GetViewModel();
	if (vm && vm->model)
	{
		VectorCopy(vm->origin, s_ent.origin);
		VectorCopy(vm->angles, s_ent.angles);
	}
	else
	{
		VectorCopy(pp->vieworg, s_ent.origin);
		s_ent.angles[0] = -pp->viewangles[0];     // studio pitch is stored negated
		s_ent.angles[1] = pp->viewangles[1];
		s_ent.angles[2] = pp->viewangles[2];
	}
	float now = eng->GetClientTime();
	float t = (now - s_start) / s_length;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	VectorCopy(s_ent.origin, s_ent.curstate.origin);
	VectorCopy(s_ent.angles, s_ent.curstate.angles);
	s_ent.curstate.frame = t * 255.0f;
	s_ent.curstate.framerate = 0.0f;              // frame is driven here
	s_ent.curstate.animtime = now;
	s_ent.latched.prevsequence = kKickSeq;
	s_ent.latched.prevframe = s_ent.curstate.frame;
	s_ent.latched.prevanimtime = now;
	s_ent.latched.sequencetime = 0.0f;
	VectorCopy(s_ent.origin, s_ent.latched.prevorigin);
	VectorCopy(s_ent.angles, s_ent.latched.prevangles);
}
