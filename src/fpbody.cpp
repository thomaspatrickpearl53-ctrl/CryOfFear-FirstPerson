// Cry of Fear first-person body (MMod-style legs) using Simon's own player.mdl.
//
// This builds a wrapper client.dll. The real Cry of Fear client is renamed to
// client_cof.dll and every engine entry point is forwarded to it. Three entry
// points are intercepted:
//   HUD_CreateEntities          - add a copy of Simon to the render list
//   V_CalcRefdef                - place/animate that copy under the camera
//   HUD_GetStudioModelInterface - hook engine StudioDrawPoints so the copy's
//                                 head (and arms) are collapsed before drawing
//
// Nothing in the game files is modified apart from the client.dll swap.

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "mathlib.h"        // vec3_t, VectorCopy
#include "hud_iface.h"      // cl_enginefunc_t (pulls cdll_int.h)
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "ref_params.h"
#include "r_studioint.h"
#include "studio.h"
#include "entity_types.h"
#include "usercmd.h"

void FpCam_Init(void);
void FpCam_CreateMove(usercmd_t *cmd);
void FpCam_CalcRefdef(ref_params_t *pp);
int  FpCam_UpdateClientData(client_data_t *cdata, float time, int changed);
void FpPost_Init(void);
void FpPost_Capture3D(void);
void FpPost_Render(float time);
static void Cmd_Bench(void);
void FpMenu_Init(void);
void FpMenu_Draw(void);
int  FpMenu_Key(int down, int keynum);
void FpMenu_CreateMove(usercmd_t *cmd);
void FpCrash_Init(void);
void FpFists_Init(void);
void FpFists_NewMap(void);
void FpClothes_NewMap(void);
void FpProps_NewMap(void);
void FpProps_CreateEntities(void);
void FpProps_CalcRefdef(ref_params_t *pp);
void FpFists_Prepare(void);
void FpFists_Frame(void);
void FpFists_CreateMove(usercmd_t *cmd);
void FpLight_Init(void);
void FpLight_NewMap(void);
void FpLight_Update(ref_params_t *pp);
void FpLight_DrawWorld(void);
void FpLight_Draw3D(void);
void FpLight_ProcessPlayerState(struct entity_state_s *dst, const struct entity_state_s *src);

// ---------------------------------------------------------------------------
// Forwarding to the original client
// ---------------------------------------------------------------------------

static HMODULE g_self;
static HMODULE g_orig;

#define FWD_LIST(X) \
	X(CAM_Think) X(CL_CameraOffset) X(CL_CreateMove) X(CL_IsThirdPerson) \
	X(Demo_ReadBuffer) X(HUD_AddEntity) X(HUD_ConnectionlessPacket) \
	X(HUD_DirectorMessage) X(HUD_DrawNormalTriangles) X(HUD_DrawTransparentTriangles) \
	X(HUD_Frame) X(HUD_GetHullBounds) X(HUD_GetUserEntity) X(HUD_Key_Event) \
	X(HUD_PlayerMove) X(HUD_PlayerMoveInit) X(HUD_PlayerMoveTexture) X(HUD_PostRunCmd) \
	X(HUD_ProcessPlayerState) X(HUD_Redraw) X(HUD_Reset) X(HUD_Shutdown) \
	X(HUD_StudioEvent) X(HUD_TempEntUpdate) X(HUD_TxferLocalOverrides) \
	X(HUD_TxferPredictionData) X(HUD_UpdateClientData) X(HUD_VoiceStatus) \
	X(IN_Accumulate) X(IN_ActivateMouse) X(IN_ClearStates) X(IN_DeactivateMouse) \
	X(IN_MouseEvent) X(KB_Find)

#define DECL_PTR(n) extern "C" void *p_##n = NULL;
FWD_LIST(DECL_PTR)

typedef int   (*Initialize_t)(cl_enginefunc_t *, int);
typedef int   (*HUD_Init_t)(void);
typedef int   (*HUD_VidInit_t)(void);
typedef void  (*HUD_CreateEntities_t)(void);
typedef void  (*V_CalcRefdef_t)(ref_params_t *);
typedef int   (*HUD_GetStudioModelInterface_t)(int, struct r_studio_interface_s **, engine_studio_api_t *);
typedef void *(*CreateInterface_t)(const char *, int *);
typedef int   (*CL_IsThirdPerson_t)(void);

static Initialize_t                  o_Initialize;
static HUD_Init_t                    o_HUD_Init;
static HUD_VidInit_t                 o_HUD_VidInit;
static HUD_CreateEntities_t          o_HUD_CreateEntities;
static V_CalcRefdef_t                o_V_CalcRefdef;
static HUD_GetStudioModelInterface_t o_HUD_GetStudioModelInterface;
static CreateInterface_t             o_CreateInterface;

extern "C" void EnsureLoaded(void)
{
	if (g_orig)
		return;

	char path[MAX_PATH];
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	strcpy(slash ? slash + 1 : path, "client_cof.dll");

	g_orig = LoadLibraryA(path);
	if (!g_orig)
	{
		MessageBoxA(NULL, "fpbody: could not load cl_dlls\\client_cof.dll (the original Cry of Fear client).",
			"Cry of Fear fpbody", MB_ICONERROR);
		ExitProcess(1);
	}

#define LOAD_PTR(n) p_##n = (void *)GetProcAddress(g_orig, #n);
	FWD_LIST(LOAD_PTR)

	o_Initialize                  = (Initialize_t)GetProcAddress(g_orig, "Initialize");
	o_HUD_Init                    = (HUD_Init_t)GetProcAddress(g_orig, "HUD_Init");
	o_HUD_VidInit                 = (HUD_VidInit_t)GetProcAddress(g_orig, "HUD_VidInit");
	o_HUD_CreateEntities          = (HUD_CreateEntities_t)GetProcAddress(g_orig, "HUD_CreateEntities");
	o_V_CalcRefdef                = (V_CalcRefdef_t)GetProcAddress(g_orig, "V_CalcRefdef");
	o_HUD_GetStudioModelInterface = (HUD_GetStudioModelInterface_t)GetProcAddress(g_orig, "HUD_GetStudioModelInterface");
	o_CreateInterface             = (CreateInterface_t)GetProcAddress(g_orig, "CreateInterface");
}

// Plain jump thunks: signatures don't matter, the stack is untouched.
#define DEF_THUNK(n) \
	extern "C" __declspec(naked) void T_##n(void) { \
		__asm pushad \
		__asm call EnsureLoaded \
		__asm popad \
		__asm jmp dword ptr [p_##n] \
	}
FWD_LIST(DEF_THUNK)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

cl_enginefunc_t            *eng;            // engine function table (owned by engine)
engine_studio_api_t         g_studioEng;    // engine's studio API, untouched

static cvar_t *fp_enable, *fp_offset, *fp_crouchoffset, *fp_zoffset, *fp_arms, *fp_model, *fp_debug;

static cl_entity_t  g_body;          // must stay alive: the engine keeps a pointer in its visedict list
static model_t     *g_bodyModel;
static char         g_bodyModelName[64];   // source model the body was built from
static float        g_bodyArms;
static bool         g_loadTried;
static bool         g_bodyVisible;
static bool         g_hideBone[MAXSTUDIOBONES];
static int          g_numBones;

static int  seq_idle = -1, seq_walk = -1, seq_run = -1, seq_sprint = -1;
static int  seq_cidle = -1, seq_cwalk = -1, seq_jump = -1;

static int   g_curSeq = -1;
static float g_phase;
static float g_bodyYaw;
static float g_lastTime = -1.0f;
static bool  g_wasAirborne;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void GameDir(char *out, size_t size);

// Console + cryoffear\fpbody.log, so problems can be diagnosed without the console.
static void Log(const char *fmt, ...)
{
	char msg[4096];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
	va_end(ap);
	msg[sizeof(msg) - 1] = 0;
	if (eng)
		eng->Con_Printf("%s", msg);

	char path[MAX_PATH];
	GameDir(path, sizeof(path));
	strcat(path, "fpbody.log");
	static bool fresh = true;
	FILE *f = fopen(path, fresh ? "w" : "a");
	fresh = false;
	if (f)
	{
		fputs(msg, f);
		fclose(f);
	}
}

// Game folder ("...\cryoffear\") for the other modules.
void FpGameDir(char *out, size_t size) { GameDir(out, size); }

// Which game is this? (the folder above cl_dlls: "cryoffear", "AoMDC", ...)
bool FpIsCoF(void)
{
	char dir[MAX_PATH];
	GameDir(dir, sizeof(dir));
	size_t n = strlen(dir);
	if (n && dir[n - 1] == '\\') dir[--n] = 0;
	const char *name = strrchr(dir, '\\');
	return !_stricmp(name ? name + 1 : dir, "cryoffear");
}

static const char *DefaultBodyModel(void)
{
	// Simon in Cry of Fear; the player model (David in Afraid of Monsters) elsewhere.
	return FpIsCoF() ? "models/cutscene/player.mdl" : "models/player.mdl";
}

// For the other modules.
void FpLog(const char *fmt, ...)
{
	char msg[2048];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
	va_end(ap);
	msg[sizeof(msg) - 1] = 0;
	Log("%s", msg);
}

static float AngleNorm(float a)
{
	a = fmodf(a, 360.0f);
	if (a > 180.0f) a -= 360.0f;
	if (a < -180.0f) a += 360.0f;
	return a;
}

static int FindSeq(studiohdr_t *hdr, const char *name)
{
	mstudioseqdesc_t *s = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex);
	for (int i = 0; i < hdr->numseq; i++)
		if (!_stricmp(s[i].label, name))
			return i;
	return -1;
}

static int FindBone(studiohdr_t *hdr, const char *name)
{
	mstudiobone_t *b = (mstudiobone_t *)((byte *)hdr + hdr->boneindex);
	for (int i = 0; i < hdr->numbones; i++)
		if (!_stricmp(b[i].name, name))
			return i;
	return -1;
}

// Hide a bone and every bone below it.
static void HideSubtree(studiohdr_t *hdr, int root)
{
	if (root < 0)
		return;
	mstudiobone_t *b = (mstudiobone_t *)((byte *)hdr + hdr->boneindex);
	g_hideBone[root] = true;
	// Bones are stored parent-before-child, so one forward pass is enough.
	for (int i = root + 1; i < hdr->numbones; i++)
		if (b[i].parent >= 0 && g_hideBone[b[i].parent])
			g_hideBone[i] = true;
}

static void BuildHiddenBones(studiohdr_t *hdr)
{
	memset(g_hideBone, 0, sizeof(g_hideBone));
	g_numBones = hdr->numbones;
	HideSubtree(hdr, FindBone(hdr, "Bip01 Head"));
	if (!fp_arms || fp_arms->value == 0.0f)
	{
		// "L Arm" is the clavicle; hide from the upper arm down so the shoulders stay.
		HideSubtree(hdr, FindBone(hdr, "Bip01 L Arm1"));
		HideSubtree(hdr, FindBone(hdr, "Bip01 R Arm1"));
	}
}

static int FindSeqAny(studiohdr_t *hdr, const char *a, const char *b, const char *c = NULL)
{
	int s = FindSeq(hdr, a);
	if (s < 0 && b) s = FindSeq(hdr, b);
	if (s < 0 && c) s = FindSeq(hdr, c);
	return s;
}

static int HiddenRoot(mstudiobone_t *b, int i)
{
	while (b[i].parent >= 0 && g_hideBone[b[i].parent])
		i = b[i].parent;
	return i;
}

// Folder of the game ("...\cryoffear\"), from our own path (...\cryoffear\cl_dlls\client.dll).
static void GameDir(char *out, size_t size)
{
	GetModuleFileNameA(g_self, out, (DWORD)size);
	for (int up = 0; up < 2; up++)
	{
		char *slash = strrchr(out, '\\');
		if (slash) *slash = 0;
	}
	strcat(out, "\\");
}

// Writes a copy of the source model where every vertex on a hidden bone is
// collapsed onto the joint where the hidden chain starts. This works no matter
// how Cry of Fear's renderer draws studio models. Returns the new model's
// game-relative name, or NULL on failure.
static const char *BuildBodyModel(const char *src, char *outName, size_t outSize)
{
	char dir[MAX_PATH], path[MAX_PATH];
	GameDir(dir, sizeof(dir));
	_snprintf(path, sizeof(path), "%s%s", dir, src);

	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	byte *data = new byte[len];
	fread(data, 1, len, f);
	fclose(f);

	studiohdr_t *hdr = (studiohdr_t *)data;
	if (len < (long)sizeof(studiohdr_t) || memcmp(&hdr->id, "IDST", 4) || hdr->version != 10)
	{
		delete[] data;
		return NULL;
	}

	BuildHiddenBones(hdr);
	mstudiobone_t *bones = (mstudiobone_t *)(data + hdr->boneindex);
	mstudiobodyparts_t *parts = (mstudiobodyparts_t *)(data + hdr->bodypartindex);
	int collapsed = 0;
	for (int p = 0; p < hdr->numbodyparts; p++)
	{
		mstudiomodel_t *models = (mstudiomodel_t *)(data + parts[p].modelindex);
		for (int m = 0; m < parts[p].nummodels; m++)
		{
			byte *vbone = data + models[m].vertinfoindex;
			vec3_t *verts = (vec3_t *)(data + models[m].vertindex);
			for (int v = 0; v < models[m].numverts; v++)
			{
				if (vbone[v] >= hdr->numbones || !g_hideBone[vbone[v]])
					continue;
				vbone[v] = (byte)HiddenRoot(bones, vbone[v]);
				verts[v][0] = verts[v][1] = verts[v][2] = 0.0f;
				collapsed++;
			}
		}
	}

	// models/fpbody/<source path with separators flattened>_a<arms>.mdl
	char flat[128];
	_snprintf(flat, sizeof(flat), "%s", src);
	for (char *c = flat; *c; c++)
		if (*c == '/' || *c == '\\') *c = '_';
	char *ext = strrchr(flat, '.');
	if (ext) *ext = 0;
	_snprintf(outName, outSize, "models/fpbody/%s_a%d.mdl", flat, (fp_arms && fp_arms->value != 0.0f) ? 1 : 0);

	_snprintf(path, sizeof(path), "%smodels\\fpbody", dir);
	CreateDirectoryA(path, NULL);
	_snprintf(path, sizeof(path), "%s%s", dir, outName);
	f = fopen(path, "wb");
	bool ok = f && fwrite(data, 1, len, f) == (size_t)len;
	if (f) fclose(f);
	delete[] data;
	if (!ok)
		return NULL;

	Log("fpbody: built %s (%d vertices hidden)\n", outName, collapsed);
	return outName;
}

static void LoadBodyModel(void)
{
	g_bodyModel = NULL;
	const char *src = (fp_model && fp_model->string && fp_model->string[0]) ? fp_model->string : DefaultBodyModel();
	// Remember what was attempted, so a failure is not retried every frame.
	g_loadTried = true;
	strncpy(g_bodyModelName, src, sizeof(g_bodyModelName) - 1);
	g_bodyArms = fp_arms ? fp_arms->value : 0.0f;

	char built[96];
	const char *name = BuildBodyModel(src, built, sizeof(built));
	if (!name)
	{
		Log("fpbody: couldn't build a body from %s, using it unmodified\n", src);
		name = src;
	}

	// CL_LoadModel only finds models the server precached, so go through the
	// studio API's Mod_ForName, which loads any model file.
	int index = 0;
	model_t *mod = g_studioEng.Mod_ForName(name, 0);
	if ((!mod || mod->type != mod_studio) && name != src)
	{
		Log("fpbody: engine can't load %s, falling back to %s\n", name, src);
		name = src;
		mod = g_studioEng.Mod_ForName(name, 0);
	}
	if (!mod || mod->type != mod_studio)
	{
		Log("fpbody: can't load studio model %s\n", name);
		return;
	}
	studiohdr_t *hdr = (studiohdr_t *)g_studioEng.Mod_Extradata(mod);
	if (!hdr)
		return;

	seq_idle   = FindSeqAny(hdr, "idle", "look_idle");
	seq_walk   = FindSeq(hdr, "walk");
	seq_run    = FindSeqAny(hdr, "run", "run2");
	seq_sprint = FindSeq(hdr, "sprint");
	seq_cidle  = FindSeq(hdr, "crouch_idle");
	seq_cwalk  = FindSeqAny(hdr, "crawl", "crouch_crouch", "crouch_walk");
	seq_jump   = FindSeq(hdr, "jump");
	if (seq_run < 0)    seq_run = seq_walk;
	if (seq_sprint < 0) seq_sprint = seq_run;
	if (seq_cidle < 0)  seq_cidle = seq_idle;
	if (seq_cwalk < 0)  seq_cwalk = seq_walk;
	if (seq_idle < 0)   seq_idle = 0;

	BuildHiddenBones(hdr);

	memset(&g_body, 0, sizeof(g_body));
	g_body.model = mod;
	g_body.index = 0;
	g_body.curstate.modelindex = index;
	g_body.curstate.rendermode = kRenderNormal;
	g_body.curstate.renderamt = 255;
	g_body.curstate.body = 0;
	g_body.curstate.skin = 0;
	for (int i = 0; i < 4; i++) g_body.curstate.controller[i] = 127;
	for (int i = 0; i < 2; i++) g_body.curstate.blending[i] = 127;

	g_bodyModel = mod;
	g_curSeq = -1;
	Log("fpbody: using %s (%d bones, idle=%d walk=%d run=%d sprint=%d)\n",
		name, hdr->numbones, seq_idle, seq_walk, seq_run, seq_sprint);
}

static studiohdr_t *BodyHeader(void)
{
	return g_bodyModel ? (studiohdr_t *)g_studioEng.Mod_Extradata(g_bodyModel) : NULL;
}

// ---------------------------------------------------------------------------
// Body placement and animation
// ---------------------------------------------------------------------------

// Why the body is hidden right now (logged whenever it changes).
static const char *HiddenReason(ref_params_t *pp, cl_entity_t *local)
{
	CL_IsThirdPerson_t thirdperson = (CL_IsThirdPerson_t)p_CL_IsThirdPerson;
	if (!fp_enable || fp_enable->value == 0.0f) return "cl_fpbody 0";
	if (!g_bodyModel)                          return "no model";
	if (!local)                                return "no local player";
	if (pp->intermission)                      return "intermission";
	if (pp->spectator)                         return "spectator";
	if (pp->health <= 0)                       return "dead";
	if (pp->viewentity != pp->playernum + 1)   return "camera view";   // cutscenes
	if (thirdperson && thirdperson())          return "third person";
	if (local->curstate.movetype == MOVETYPE_FLY) return "ladder";
	if (pp->waterlevel >= 3)                   return "underwater";
	return NULL;
}

static void UpdateBody(ref_params_t *pp)
{
	g_bodyVisible = false;
	cl_entity_t *local = eng->GetLocalPlayer();
	const char *why = HiddenReason(pp, local);
	static const char *lastWhy = "start";
	if (why != lastWhy)
	{
		Log("fpbody: %s (viewentity=%d playernum=%d health=%d)\n",
			why ? why : "visible", pp->viewentity, pp->playernum, pp->health);
		lastWhy = why;
	}
	if (why)
		return;

	float now = pp->time;
	float dt = (g_lastTime < 0.0f) ? 0.0f : now - g_lastTime;
	if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
	g_lastTime = now;

	float vx = pp->simvel[0], vy = pp->simvel[1];
	float speed = sqrtf(vx * vx + vy * vy);
	float viewYaw = pp->cl_viewangles[1];
	bool crouched = pp->viewheight[2] < 20.0f || local->curstate.usehull == 1;
	bool airborne = !pp->onground;

	// Pick the gait. CoF speeds: 45 (two-handed) / 75 (one-handed) walking, 125 sprinting.
	int seq;
	if (airborne && seq_jump >= 0)     seq = seq_jump;
	else if (crouched)                 seq = speed > 5.0f ? seq_cwalk : seq_cidle;
	else if (speed < 5.0f)             seq = seq_idle;
	else if (speed < 100.0f)           seq = seq_walk;
	else                               seq = seq_sprint;

	// Legs follow the movement direction, body never twists more than 70 degrees.
	float targetYaw = viewYaw;
	float dir = 1.0f;
	if (speed > 5.0f && !airborne)
	{
		float diff = AngleNorm(atan2f(vy, vx) * (180.0f / 3.14159265f) - viewYaw);
		if (fabsf(diff) > 100.0f) { diff = AngleNorm(diff + 180.0f); dir = -1.0f; } // backpedal
		if (diff > 70.0f) diff = 70.0f;
		if (diff < -70.0f) diff = -70.0f;
		targetYaw = viewYaw + diff;
	}
	float k = dt * 10.0f; if (k > 1.0f || dt == 0.0f) k = 1.0f;
	g_bodyYaw = AngleNorm(g_bodyYaw + AngleNorm(targetYaw - g_bodyYaw) * k);

	// Advance the animation ourselves (sequences are in-place, so scale by real speed).
	studiohdr_t *hdr = BodyHeader();
	if (!hdr || seq < 0 || seq >= hdr->numseq)
		return;
	mstudioseqdesc_t *sd = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + seq;
	float natural = (sd->numframes > 1) ? sd->fps / (float)(sd->numframes - 1) : 0.0f; // cycles/sec
	float cycleRate = natural;
	float stride = sqrtf(DotProduct(sd->linearmovement, sd->linearmovement)); // units per cycle
	if (stride > 1.0f && speed > 5.0f && !airborne)
	{
		// Match the feet to the ground: one cycle per stride length travelled.
		cycleRate = speed / stride;
		if (cycleRate < natural * 0.4f) cycleRate = natural * 0.4f;
		if (cycleRate > natural * 2.5f) cycleRate = natural * 2.5f;
	}

	if (seq != g_curSeq)
	{
		g_curSeq = seq;
		g_phase = 0.0f;
	}
	if (airborne && !g_wasAirborne)
		g_phase = 0.0f;
	g_wasAirborne = airborne;

	g_phase += dt * cycleRate * dir;
	if (sd->flags & STUDIO_LOOPING)
	{
		g_phase -= floorf(g_phase);
	}
	else if (g_phase > 0.999f)
		g_phase = 0.999f;

	// Place it: same spot as the player, pushed back so the camera sits in front of the neck.
	float yr = viewYaw * (3.14159265f / 180.0f);
	float back = crouched ? fp_crouchoffset->value : fp_offset->value;

	g_body.origin[0] = pp->simorg[0] - cosf(yr) * back;
	g_body.origin[1] = pp->simorg[1] - sinf(yr) * back;
	g_body.origin[2] = pp->simorg[2] + fp_zoffset->value;
	g_body.angles[0] = 0.0f;
	g_body.angles[1] = g_bodyYaw;
	g_body.angles[2] = 0.0f;
	VectorCopy(g_body.origin, g_body.curstate.origin);
	VectorCopy(g_body.angles, g_body.curstate.angles);

	g_body.curstate.sequence = seq;
	g_body.curstate.frame = g_phase * 256.0f;
	g_body.curstate.framerate = 0.0f;       // frame is driven above
	g_body.curstate.animtime = now;
	g_body.latched.prevsequence = seq;
	g_body.latched.prevframe = g_body.curstate.frame;
	g_body.latched.prevanimtime = now;
	g_body.latched.sequencetime = 0.0f;
	VectorCopy(g_body.origin, g_body.latched.prevorigin);
	VectorCopy(g_body.angles, g_body.latched.prevangles);
	g_body.curstate.skin = local->curstate.skin;

	g_bodyVisible = true;
}

// ---------------------------------------------------------------------------
// Intercepted exports
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Flashlight state. Cry of Fear doesn't use EF_DIMLIGHT; the server tells the
// client through user messages instead. We sit between the client and the
// engine's HookUserMsg, so we see those messages before the client does.
// ---------------------------------------------------------------------------

static unsigned char g_clientTable[sizeof(cl_enginefunc_t) + 256];   // what the client gets
static bool g_flashlightOn, g_dualFlashOn;
static int  g_flashFlags;
static int  g_msgLogged;

struct WatchedMsg { const char *name; pfnUserMsgHook orig; };
static WatchedMsg g_watched[] = { { "Flashlight", NULL }, { "FlashFlags", NULL }, { "DualFlash", NULL }, { "FlashBat", NULL } };

static void OnLightMsg(int which, int size, const unsigned char *buf)
{
	if (g_msgLogged < 40)
	{
		char hex[64] = "";
		for (int i = 0; i < size && i < 12; i++)
			_snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02x ", buf[i]);
		Log("fpbody: msg %s [%d]: %s\n", g_watched[which].name, size, hex);
		g_msgLogged++;
	}
	if (size < 1)
		return;
	if (which == 0) g_flashlightOn = buf[0] != 0;     // Flashlight: first byte = on/off (as in Half-Life)
	if (which == 1) g_flashFlags = buf[0];            // FlashFlags: bit 0 = light on (seen in game: 01 on, 00 off)
	if (which == 2) g_dualFlashOn = buf[0] != 0;      // DualFlash: light held next to a weapon
}

#define LIGHT_TRAMPOLINE(i) \
	static int LightMsg##i(const char *name, int size, void *buf) \
	{ \
		OnLightMsg(i, size, (const unsigned char *)buf); \
		return g_watched[i].orig ? g_watched[i].orig(name, size, buf) : 1; \
	}
LIGHT_TRAMPOLINE(0) LIGHT_TRAMPOLINE(1) LIGHT_TRAMPOLINE(2) LIGHT_TRAMPOLINE(3)
static pfnUserMsgHook g_trampolines[] = { LightMsg0, LightMsg1, LightMsg2, LightMsg3 };

static int Hook_HookUserMsg(char *name, pfnUserMsgHook fn)
{
	for (int i = 0; i < 4; i++)
		if (!_stricmp(name, g_watched[i].name))
		{
			g_watched[i].orig = fn;
			return eng->pfnHookUserMsg(name, g_trampolines[i]);
		}
	return eng->pfnHookUserMsg(name, fn);
}

bool FpFlashlightOn(void)
{
	return (g_flashFlags & 1) || g_flashlightOn || g_dualFlashOn;
}

static void ResetFlashlight(void)
{
	g_flashlightOn = g_dualFlashOn = false;
	g_flashFlags = 0;
}

extern "C" int W_Initialize(cl_enginefunc_t *pEnginefuncs, int iVersion)
{
	EnsureLoaded();
	eng = pEnginefuncs;
	FpCrash_Init();
	// Hand the client a copy of the engine table with HookUserMsg routed through us.
	// (A little extra is copied in case this engine build's table is longer.)
	memcpy(g_clientTable, pEnginefuncs, sizeof(g_clientTable));
	((cl_enginefunc_t *)g_clientTable)->pfnHookUserMsg = Hook_HookUserMsg;
	return o_Initialize((cl_enginefunc_t *)g_clientTable, iVersion);
}

// ---------------------------------------------------------------------------
// Settings file: the mod's cvars live in cryoffear\fpbody.cfg as well as the
// engine config, so playing the unmodded game (which drops unknown cvars from
// config.cfg) doesn't reset them.
// ---------------------------------------------------------------------------

static cvar_t *g_saved[128];
static int     g_numSaved;

cvar_t *FpRegister(const char *name, const char *value, int flags)
{
	cvar_t *c = eng->pfnRegisterVariable((char *)name, (char *)value, flags);
	if (c && (flags & FCVAR_ARCHIVE) && g_numSaved < 128)
		g_saved[g_numSaved++] = c;
	return c;
}

// Numeric settings are applied straight away (so an early save can't overwrite a
// freshly installed fpbody.cfg with defaults); text ones go through "exec".
static void LoadSettings(void)
{
	char path[MAX_PATH];
	GameDir(path, sizeof(path));
	strcat(path, "fpbody.cfg");
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[256];
	while (fgets(line, sizeof(line), f))
	{
		char name[64], value[128];
		if (sscanf(line, "%63s \"%127[^\"]\"", name, value) != 2 || strncmp(name, "cl_", 3))
			continue;
		char *end;
		float v = (float)strtod(value, &end);
		if (end != value && *end == 0)
			eng->Cvar_SetValue(name, v);
	}
	fclose(f);
}

static void SaveSettings(void)
{
	if (!g_numSaved)
		return;
	char path[MAX_PATH];
	GameDir(path, sizeof(path));
	strcat(path, "fpbody.cfg");
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "// Cry of Fear first-person mod settings (written by the mod)\n");
	for (int i = 0; i < g_numSaved; i++)
		fprintf(f, "%s \"%s\"\n", g_saved[i]->name, g_saved[i]->string);
	fclose(f);
}

static void Cmd_FpbodyInfo(void)
{
	eng->Con_Printf("fpbody: model=%s visible=%d seq=%d phase=%.2f yaw=%.0f\n",
		g_bodyModel ? g_bodyModelName : "(none)", g_bodyVisible, g_curSeq, g_phase, g_bodyYaw);
}

static void Cmd_FpbodyReload(void)
{
	LoadBodyModel();
}

extern "C" int W_HUD_Init(void)
{
	int r = o_HUD_Init();
	fp_enable       = FpRegister("cl_fpbody", "1", FCVAR_ARCHIVE);
	fp_offset       = FpRegister("cl_fpbody_offset", "15", FCVAR_ARCHIVE);
	fp_crouchoffset = FpRegister("cl_fpbody_crouchoffset", "20", FCVAR_ARCHIVE);
	fp_zoffset      = FpRegister("cl_fpbody_zoffset", "0", FCVAR_ARCHIVE);
	fp_arms         = FpRegister("cl_fpbody_arms", "0", FCVAR_ARCHIVE);
	fp_model        = FpRegister("cl_fpbody_simon", DefaultBodyModel(), FCVAR_ARCHIVE);
	fp_debug        = FpRegister("cl_fpbody_debug", "0", 0);
	FpCam_Init();
	FpPost_Init();
	FpLight_Init();
	eng->pfnAddCommand("fpbody_info", Cmd_FpbodyInfo);
	eng->pfnAddCommand("fpbody_reload", Cmd_FpbodyReload);
	eng->pfnAddCommand("fp_bench", Cmd_Bench);
	FpMenu_Init();
	FpFists_Init();
	LoadSettings();
	eng->pfnClientCmd("exec fpbody.cfg\n");
	return r;
}

typedef void (*HUD_Shutdown_t)(void);

extern "C" void W_HUD_Shutdown(void)
{
	EnsureLoaded();
	SaveSettings();
	((HUD_Shutdown_t)p_HUD_Shutdown)();
}

extern "C" int W_HUD_VidInit(void)
{
	int r = o_HUD_VidInit();
	SaveSettings();           // also on every map load, in case the game crashes later
	g_bodyModel = NULL;     // models are reloaded on every map change
	ResetFlashlight();      // the server re-sends the flashlight state on spawn
	FpLight_NewMap();
	FpFists_NewMap();
	FpClothes_NewMap();
	FpProps_NewMap();
	g_loadTried = false;
	g_bodyVisible = false;
	g_lastTime = -1.0f;
	return r;
}

extern "C" void W_HUD_CreateEntities(void)
{
	o_HUD_CreateEntities();
	FpFists_Prepare();          // loads models before the frame is drawn
	FpProps_CreateEntities();   // F8 > Models (loads models here too)
	if (!fp_enable || fp_enable->value == 0.0f)
		return;
	if (!g_loadTried || _stricmp(g_bodyModelName, fp_model->string) || g_bodyArms != fp_arms->value)
		LoadBodyModel();
	// Visibility comes from the previous V_CalcRefdef (one frame behind is fine).
	if (g_bodyModel && g_bodyVisible)
		eng->CL_CreateVisibleEntity(ET_NORMAL, &g_body);
}

extern "C" void W_V_CalcRefdef(ref_params_t *pparams)
{
	o_V_CalcRefdef(pparams);
	UpdateBody(pparams);         // body placement uses the real aim, before camera effects
	FpCam_CalcRefdef(pparams);
	FpLight_Update(pparams);     // after the camera effects: the torch is in the hand
	FpFists_Frame();             // fists mode: swap the nightstick viewmodel
	FpProps_CalcRefdef(pparams); // where you're looking, for placing models
	if (fp_debug && fp_debug->value != 0.0f)
	{
		static float next;
		if (pparams->time > next)
		{
			next = pparams->time + 1.0f;
			Cmd_FpbodyInfo();
		}
	}
}

extern "C" int W_HUD_GetStudioModelInterface(int version, struct r_studio_interface_s **ppinterface, engine_studio_api_t *pstudio)
{
	EnsureLoaded();
	memcpy(&g_studioEng, pstudio, sizeof(g_studioEng));    // for Mod_Extradata
	return o_HUD_GetStudioModelInterface(version, ppinterface, pstudio);
}

typedef void (*HUD_DrawNormalTriangles_t)(void);
typedef void (*HUD_ProcessPlayerState_t)(struct entity_state_s *, const struct entity_state_s *);

extern "C" void W_HUD_DrawNormalTriangles(void)
{
	EnsureLoaded();
	((HUD_DrawNormalTriangles_t)p_HUD_DrawNormalTriangles)();
	FpLight_DrawWorld();     // after solid geometry, before transparent things
}

extern "C" void W_HUD_ProcessPlayerState(struct entity_state_s *dst, const struct entity_state_s *src)
{
	EnsureLoaded();
	((HUD_ProcessPlayerState_t)p_HUD_ProcessPlayerState)(dst, src);
	FpLight_ProcessPlayerState(dst, src);
}

typedef void (*HUD_DrawTransparentTriangles_t)(void);
typedef int  (*HUD_Redraw_t)(float, int);

extern "C" void W_HUD_DrawTransparentTriangles(void)
{
	EnsureLoaded();
	((HUD_DrawTransparentTriangles_t)p_HUD_DrawTransparentTriangles)();
	FpLight_Draw3D();        // dust in the beam (3D, depth-tested) + texture filtering
	FpPost_Capture3D();
}

// ---------------------------------------------------------------------------
// fp_bench: measures frame times with parts of the mod switched off, to find
// what causes slowdowns. The player keeps moving/turning while it runs.
// ---------------------------------------------------------------------------

struct BenchPhase { const char *name; float pp, body, cam; };
static const BenchPhase s_phases[] = {
	{ "everything on",          1, 1, 1 },
	{ "graphics effects off",   0, 1, 1 },
	{ "body off",               1, 0, 1 },
	{ "camera + hands off",     1, 1, 0 },
	{ "whole mod off",          0, 0, 0 },
};
static const int   BENCH_PHASES = sizeof(s_phases) / sizeof(s_phases[0]);
static const float BENCH_SETTLE = 1.0f, BENCH_MEASURE = 6.0f;
static int    s_benchPhase = -1;
static double s_benchStart, s_benchLast;
static float  s_savedPP, s_savedBody, s_savedCam, s_savedVM, s_savedFov;
static float  s_frames[4096];
static int    s_numFrames;
static char   s_benchReport[BENCH_PHASES][160];

static double NowSeconds(void)
{
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	return (double)c.QuadPart / (double)f.QuadPart;
}

static float CvarValue(const char *name)
{
	cvar_t *c = g_studioEng.GetCvar ? g_studioEng.GetCvar(name) : NULL;
	return c ? c->value : 1.0f;
}

static void ApplyPhase(int i)
{
	const BenchPhase &p = s_phases[i];
	eng->Cvar_SetValue((char *)"cl_pp", p.pp ? s_savedPP : 0.0f);
	eng->Cvar_SetValue((char *)"cl_fpbody", p.body ? s_savedBody : 0.0f);
	eng->Cvar_SetValue((char *)"cl_fpcam", p.cam ? s_savedCam : 0.0f);
	eng->Cvar_SetValue((char *)"cl_fpvm", p.cam ? s_savedVM : 0.0f);
	eng->Cvar_SetValue((char *)"cl_fpfov", p.cam ? s_savedFov : 0.0f);
	s_benchStart = NowSeconds();
	s_numFrames = 0;
	eng->Con_Printf("fp_bench: %d/%d %s - keep turning the camera fast\n", i + 1, BENCH_PHASES, p.name);
}

static int CompareFloat(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;
	return x < y ? -1 : (x > y ? 1 : 0);
}

static void FinishPhase(int i)
{
	if (s_numFrames < 10)
	{
		_snprintf(s_benchReport[i], sizeof(s_benchReport[i]), "%-22s  (not enough frames)", s_phases[i].name);
		return;
	}
	double sum = 0.0;
	int slow = 0;
	for (int k = 0; k < s_numFrames; k++)
	{
		sum += s_frames[k];
		if (s_frames[k] > 25.0f) slow++;
	}
	qsort(s_frames, s_numFrames, sizeof(float), CompareFloat);
	float p99 = s_frames[(int)(s_numFrames * 0.99f)];
	float worst = s_frames[s_numFrames - 1];
	_snprintf(s_benchReport[i], sizeof(s_benchReport[i]),
		"%-22s  avg %5.1f fps | 1%% low %5.1f fps | worst %6.1f ms | frames over 25 ms: %d of %d",
		s_phases[i].name, 1000.0 * s_numFrames / sum, 1000.0f / p99, worst, slow, s_numFrames);
}

static void Cmd_Bench(void)
{
	if (s_benchPhase >= 0)
	{
		eng->Con_Printf("fp_bench: already running\n");
		return;
	}
	s_savedPP = CvarValue("cl_pp");
	s_savedBody = CvarValue("cl_fpbody");
	s_savedCam = CvarValue("cl_fpcam");
	s_savedVM = CvarValue("cl_fpvm");
	s_savedFov = CvarValue("cl_fpfov");
	s_benchPhase = 0;
	s_benchLast = 0.0;
	ApplyPhase(0);
}

static void BenchFrame(void)
{
	if (s_benchPhase < 0)
		return;
	double now = NowSeconds();
	if (s_benchLast > 0.0 && now - s_benchStart > BENCH_SETTLE && s_numFrames < 4096)
		s_frames[s_numFrames++] = (float)((now - s_benchLast) * 1000.0);
	s_benchLast = now;
	if (now - s_benchStart < BENCH_SETTLE + BENCH_MEASURE)
		return;

	FinishPhase(s_benchPhase);
	if (++s_benchPhase < BENCH_PHASES)
	{
		ApplyPhase(s_benchPhase);
		return;
	}
	// Done: restore the player's settings and report.
	eng->Cvar_SetValue((char *)"cl_pp", s_savedPP);
	eng->Cvar_SetValue((char *)"cl_fpbody", s_savedBody);
	eng->Cvar_SetValue((char *)"cl_fpcam", s_savedCam);
	eng->Cvar_SetValue((char *)"cl_fpvm", s_savedVM);
	eng->Cvar_SetValue((char *)"cl_fpfov", s_savedFov);
	s_benchPhase = -1;
	Log("fp_bench results:\n");
	for (int i = 0; i < BENCH_PHASES; i++)
		Log("  %s\n", s_benchReport[i]);
	eng->Con_Printf("fp_bench: done, results are in cryoffear\\fpbody.log\n");
}

extern "C" int W_HUD_Redraw(float time, int intermission)
{
	EnsureLoaded();
	BenchFrame();
	FpPost_Render(time);    // before the HUD, so the HUD stays crisp
	int r = ((HUD_Redraw_t)p_HUD_Redraw)(time, intermission);
	FpMenu_Draw();          // on top of the HUD
	return r;
}

typedef int (*HUD_Key_Event_t)(int, int, const char *);

extern "C" int W_HUD_Key_Event(int down, int keynum, const char *binding)
{
	EnsureLoaded();
	if (!FpMenu_Key(down, keynum))
		return 0;           // the menu used this key
	return ((HUD_Key_Event_t)p_HUD_Key_Event)(down, keynum, binding);
}

typedef void (*CL_CreateMove_t)(float, usercmd_t *, int);
typedef int  (*HUD_UpdateClientData_t)(client_data_t *, float);

extern "C" void W_CL_CreateMove(float frametime, usercmd_t *cmd, int active)
{
	EnsureLoaded();
	((CL_CreateMove_t)p_CL_CreateMove)(frametime, cmd, active);
	FpCam_CreateMove(cmd);
	FpMenu_CreateMove(cmd);     // big menu: mouse moves the cursor, not the view
	FpFists_CreateMove(cmd);    // fists mode: mouse 2 punches too
}

extern "C" int W_HUD_UpdateClientData(client_data_t *cdata, float time)
{
	EnsureLoaded();
	int changed = ((HUD_UpdateClientData_t)p_HUD_UpdateClientData)(cdata, time);
	return FpCam_UpdateClientData(cdata, time, changed);
}

extern "C" void *W_CreateInterface(const char *name, int *ret)
{
	EnsureLoaded();
	return o_CreateInterface ? o_CreateInterface(name, ret) : NULL;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = hinst;
		DisableThreadLibraryCalls(hinst);
	}
	return TRUE;
}
