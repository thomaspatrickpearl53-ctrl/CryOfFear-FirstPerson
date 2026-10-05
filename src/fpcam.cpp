// Procedural first-person camera and viewmodel feel for Cry of Fear.
//
// Everything here runs on top of what the original client already computed:
//   FpCam_CalcRefdef      (after V_CalcRefdef)  camera bob, footstep/landing impacts,
//                                               rotation lag, strafe/turn lean, horizon
//                                               lock, viewmodel sway/breathing/actions
//   FpCam_CreateMove      (after CL_CreateMove) pitch clamp, button state for actions
//   FpCam_UpdateClientData(after HUD_UpdateClientData) sprint/aim FOV shifts
//
// All offsets go through damped springs, so nothing ever snaps, and every
// effect is clamped so the view and the hands stay inside sane limits.

#include <windows.h>
#include <math.h>
#include <string.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "ref_params.h"
#include "r_studioint.h"
#include "studio.h"
#include "usercmd.h"
#include "in_buttons.h"

extern cl_enginefunc_t     *eng;
extern engine_studio_api_t  g_studioEng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
typedef int (*CL_IsThirdPerson_t)(void);
extern "C" void *p_CL_IsThirdPerson;

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

static cvar_t *cam_enable, *cam_bob, *cam_impact, *cam_lag, *cam_tilt, *cam_pitchmax, *cam_maxroll;
static cvar_t *vm_enable, *vm_sway, *vm_breath, *vm_action;
static cvar_t *fov_enable, *fov_sprint, *fov_ads;

void FpCam_Init(void)
{
	cam_enable   = FpRegister("cl_fpcam", "1", FCVAR_ARCHIVE);
	cam_bob      = FpRegister("cl_fpcam_bob", "1", FCVAR_ARCHIVE);       // head bob scale
	cam_impact   = FpRegister("cl_fpcam_impact", "1", FCVAR_ARCHIVE);    // footstep/landing kick scale
	cam_lag      = FpRegister("cl_fpcam_lag", "1", FCVAR_ARCHIVE);       // rotation lag scale
	cam_tilt     = FpRegister("cl_fpcam_tilt", "1", FCVAR_ARCHIVE);      // strafe/turn lean scale
	cam_pitchmax = FpRegister("cl_fpcam_pitchmax", "80", FCVAR_ARCHIVE); // look up/down limit (degrees)
	cam_maxroll  = FpRegister("cl_fpcam_maxroll", "5", FCVAR_ARCHIVE);   // horizon lock (degrees)
	vm_enable    = FpRegister("cl_fpvm", "1", FCVAR_ARCHIVE);
	vm_sway      = FpRegister("cl_fpvm_sway", "1", FCVAR_ARCHIVE);       // inertia sway scale
	vm_breath    = FpRegister("cl_fpvm_breath", "1", FCVAR_ARCHIVE);     // idle breathing scale
	vm_action    = FpRegister("cl_fpvm_action", "1", FCVAR_ARCHIVE);     // sprint/jump/fire/reload offsets
	fov_enable   = FpRegister("cl_fpfov", "1", FCVAR_ARCHIVE);
	fov_sprint   = FpRegister("cl_fpfov_sprint", "6", FCVAR_ARCHIVE);    // % wider while sprinting
	fov_ads      = FpRegister("cl_fpfov_ads", "8", FCVAR_ARCHIVE);       // % narrower while aiming
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct Spring
{
	float pos, vel;
	// Semi-implicit spring toward target; sub-stepped so low framerates stay stable.
	void Step(float target, float stiffness, float damping, float dt)
	{
		while (dt > 0.0f)
		{
			float h = dt > (1.0f / 120.0f) ? (1.0f / 120.0f) : dt;
			vel += ((target - pos) * stiffness - vel * damping) * h;
			pos += vel * h;
			dt -= h;
		}
	}
	void Reset() { pos = vel = 0.0f; }
};

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float AngNorm(float a)
{
	a = fmodf(a, 360.0f);
	if (a > 180.0f) a -= 360.0f;
	if (a < -180.0f) a += 360.0f;
	return a;
}

static float Approach(float cur, float target, float rate, float dt)
{
	return cur + (target - cur) * (1.0f - expf(-rate * dt));
}

static void AngVectors(const float *ang, float *fwd, float *right, float *up)
{
	float p = ang[0] * (M_PI / 180.0f), y = ang[1] * (M_PI / 180.0f), r = ang[2] * (M_PI / 180.0f);
	float sp = sinf(p), cp = cosf(p), sy = sinf(y), cy = cosf(y), sr = sinf(r), cr = cosf(r);
	fwd[0] = cp * cy; fwd[1] = cp * sy; fwd[2] = -sp;
	right[0] = -sr * sp * cy + cr * sy; right[1] = -sr * sp * sy - cr * cy; right[2] = -sr * cp;
	up[0] = cr * sp * cy + sr * sy; up[1] = cr * sp * sy - sr * cy; up[2] = cr * cp;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool  s_active;                 // player-controlled first-person view last frame
static int   s_buttons, s_prevButtons; // from the latest usercmd
static float s_speed, s_sprint, s_ads; // smoothed 0..1 weights shared with the FOV code
static int   s_health = -1;             // for the post-processing damage pulse

// camera
static float  s_stepPhase;
static float  s_bobAmp;
static Spring s_impactZ, s_impactPitch, s_roll;
static float  s_lagYaw, s_lagPitch;
static float  s_prevYaw, s_prevPitch;
static bool   s_wasOnGround = true;
static float  s_airVelZ;

// viewmodel
static Spring s_vmYaw, s_vmPitch, s_vmRoll, s_vmX, s_vmY, s_vmZ, s_vmKick, s_vmKickPitch;
static float  s_reload;

// fov
static float s_fovScale = 1.0f;

static void ResetAll(const ref_params_t *pp)
{
	s_impactZ.Reset(); s_impactPitch.Reset(); s_roll.Reset();
	s_vmYaw.Reset(); s_vmPitch.Reset(); s_vmRoll.Reset();
	s_vmX.Reset(); s_vmY.Reset(); s_vmZ.Reset(); s_vmKick.Reset(); s_vmKickPitch.Reset();
	s_bobAmp = 0.0f;
	s_sprint = s_ads = s_reload = 0.0f;
	s_lagYaw = s_prevYaw = pp->cl_viewangles[1];
	s_lagPitch = s_prevPitch = pp->cl_viewangles[0];
	s_wasOnGround = true;
	s_airVelZ = 0.0f;
}

// The player's own first-person view last frame (not a cutscene, third person,
// intermission or death).
bool FpCam_PlayerView(void)
{
	return s_active;
}

static bool PlayerView(const ref_params_t *pp)
{
	CL_IsThirdPerson_t thirdperson = (CL_IsThirdPerson_t)p_CL_IsThirdPerson;
	if (pp->intermission || pp->spectator || pp->health <= 0)
		return false;
	if (pp->viewentity != pp->playernum + 1)   // cutscene camera
		return false;
	if (thirdperson && thirdperson())
		return false;
	return true;
}

// Is the viewmodel playing a reload animation right now?
static bool ViewmodelReloading(cl_entity_t *vm)
{
	if (!vm || !vm->model || vm->model->type != mod_studio || !g_studioEng.Mod_Extradata)
		return false;
	studiohdr_t *hdr = (studiohdr_t *)g_studioEng.Mod_Extradata(vm->model);
	if (!hdr || vm->curstate.sequence < 0 || vm->curstate.sequence >= hdr->numseq)
		return false;
	mstudioseqdesc_t *sd = (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex) + vm->curstate.sequence;
	char label[33];
	strncpy(label, sd->label, 32);
	label[32] = 0;
	_strlwr(label);
	return strstr(label, "reload") != NULL || strstr(label, "insert") != NULL;
}

// ---------------------------------------------------------------------------
// Input: pitch clamp + buttons
// ---------------------------------------------------------------------------

void FpCam_CreateMove(usercmd_t *cmd)
{
	s_buttons = cmd->buttons;

	if (!cam_enable || cam_enable->value == 0.0f || !s_active)
		return;
	float limit = Clamp(cam_pitchmax->value, 10.0f, 89.0f);
	float ang[3];
	eng->GetViewAngles(ang);
	if (ang[0] > limit || ang[0] < -limit)
	{
		ang[0] = Clamp(ang[0], -limit, limit);
		eng->SetViewAngles(ang);
		cmd->viewangles[0] = ang[0];
	}
}

// ---------------------------------------------------------------------------
// Camera + viewmodel
// ---------------------------------------------------------------------------

void FpCam_CalcRefdef(ref_params_t *pp)
{
	if (pp->paused)
		return;

	s_health = pp->health;
	bool active = PlayerView(pp);
	if (!active)
	{
		s_active = false;
		return;
	}
	if (!s_active)
		ResetAll(pp);
	s_active = true;

	float dt = Clamp(pp->frametime, 0.0f, 0.1f);
	if (dt <= 0.0f)
		return;

	bool  camOn = cam_enable && cam_enable->value != 0.0f;
	bool  vmOn  = vm_enable && vm_enable->value != 0.0f;
	float speed = sqrtf(pp->simvel[0] * pp->simvel[0] + pp->simvel[1] * pp->simvel[1]);
	bool  onGround = pp->onground != 0;
	s_speed = speed;

	// Shared state weights.
	bool sprinting = onGround && speed > 100.0f;
	s_sprint = Approach(s_sprint, sprinting ? 1.0f : 0.0f, 6.0f, dt);
	s_ads    = Approach(s_ads, (s_buttons & IN_ATTACK2) ? 1.0f : 0.0f, 10.0f, dt);
	float steady = 1.0f - 0.7f * s_ads;    // aiming steadies bob and sway

	// Yaw/pitch rates (deg/s) of the real aim.
	float yaw = pp->cl_viewangles[1], pitch = pp->cl_viewangles[0];
	float yawRate   = AngNorm(yaw - s_prevYaw) / dt;
	float pitchRate = (pitch - s_prevPitch) / dt;
	s_prevYaw = yaw;
	s_prevPitch = pitch;
	yawRate   = Clamp(yawRate, -1500.0f, 1500.0f);
	pitchRate = Clamp(pitchRate, -1500.0f, 1500.0f);

	// --- Footsteps and landings -------------------------------------------------
	float speedK = Clamp(speed / 125.0f, 0.0f, 1.3f);
	bool footDown = false;
	if (onGround && speed > 5.0f)
	{
		const float stepLength = 48.0f;    // units per footstep
		float before = floorf(s_stepPhase);
		s_stepPhase += speed * dt / stepLength;
		footDown = floorf(s_stepPhase) != before;
	}
	float landImpulse = 0.0f;
	if (!onGround)
	{
		if (pp->simvel[2] < s_airVelZ) s_airVelZ = pp->simvel[2];
	}
	else if (!s_wasOnGround)
	{
		landImpulse = Clamp((-s_airVelZ - 100.0f) / 40.0f, 0.0f, 10.0f);
		s_airVelZ = 0.0f;
	}
	s_wasOnGround = onGround;

	float camDelta[3] = { 0, 0, 0 };   // world-space offset added to the camera (viewmodel follows)
	float camAng[3]   = { 0, 0, 0 };   // extra pitch/yaw/roll added to the camera (viewmodel follows)

	if (camOn)
	{
		float impactK = cam_impact->value;
		if (footDown)
		{
			s_impactZ.vel     -= 9.0f * Clamp(speedK, 0.3f, 1.2f) * impactK * steady;
			s_impactPitch.vel += 6.0f * Clamp(speedK, 0.3f, 1.2f) * impactK * steady;
		}
		if (landImpulse > 0.0f)
		{
			s_impactZ.vel     -= landImpulse * 14.0f * impactK;
			s_impactPitch.vel += landImpulse * 10.0f * impactK;
		}
		s_impactZ.Step(0.0f, 140.0f, 13.0f, dt);       // slightly underdamped: a small rebound
		s_impactPitch.Step(0.0f, 160.0f, 16.0f, dt);
		s_impactZ.pos     = Clamp(s_impactZ.pos, -5.0f, 1.5f);
		s_impactPitch.pos = Clamp(s_impactPitch.pos, -1.5f, 3.0f);

		// --- Head bob: lowest at foot contact, sways toward the planted foot.
		float bobTarget = onGround ? speedK : 0.0f;
		s_bobAmp = Approach(s_bobAmp, bobTarget, 8.0f, dt);
		float b = cam_bob->value * s_bobAmp * steady;
		float ph = s_stepPhase * (float)M_PI;
		float bobUp   = -0.9f * b * cosf(2.0f * ph);
		float bobSide =  0.7f * b * sinf(ph);
		float bobRoll =  0.6f * b * sinf(ph);
		float bobPitch = 0.35f * b * cosf(2.0f * ph);

		float up = bobUp + s_impactZ.pos;
		for (int i = 0; i < 3; i++)
			camDelta[i] = pp->right[i] * bobSide + pp->up[i] * up;

		// --- Rotation lag: the view trails the aim a little and catches up.
		float lagK = cam_lag->value;
		if (lagK > 0.0f)
		{
			float rate = 35.0f / lagK;     // settles in ~0.1 s
			s_lagYaw   = s_lagYaw + AngNorm(yaw - s_lagYaw) * (1.0f - expf(-rate * dt));
			s_lagPitch = Approach(s_lagPitch, pitch, rate, dt);
		}
		else
		{
			s_lagYaw = yaw;
			s_lagPitch = pitch;
		}
		float maxLag = 1.2f * lagK;    // small, so fast flicks never feel like input lag
		float lagYaw   = Clamp(AngNorm(s_lagYaw - yaw), -maxLag, maxLag);
		float lagPitch = Clamp(s_lagPitch - pitch, -maxLag, maxLag);

		// --- Lean into strafes and turns.
		float rightFlat[2] = { sinf(yaw * (M_PI / 180.0f)), -cosf(yaw * (M_PI / 180.0f)) };
		float strafe = (pp->simvel[0] * rightFlat[0] + pp->simvel[1] * rightFlat[1]) / 125.0f;
		float rollTarget = cam_tilt->value * (Clamp(strafe, -1.2f, 1.2f) * 1.6f
			+ Clamp(-yawRate / 300.0f, -1.0f, 1.0f) * 1.4f) * (onGround ? 1.0f : 0.5f);
		s_roll.Step(rollTarget, 60.0f, 14.0f, dt);

		float before[3];
		VectorCopy(pp->viewangles, before);
		VectorAdd(pp->vieworg, camDelta, pp->vieworg);
		pp->viewangles[0] += lagPitch + s_impactPitch.pos + bobPitch;
		pp->viewangles[1] += lagYaw;
		pp->viewangles[2] += s_roll.pos + bobRoll;

		// --- Horizon lock and anatomical pitch limit on what is displayed.
		float maxRoll = Clamp(cam_maxroll->value, 0.0f, 30.0f);
		pp->viewangles[2] = Clamp(AngNorm(pp->viewangles[2]), -maxRoll, maxRoll);
		float limit = Clamp(cam_pitchmax->value, 10.0f, 89.0f) + 4.0f;
		pp->viewangles[0] = Clamp(pp->viewangles[0], -limit, limit);
		for (int i = 0; i < 3; i++)
			camAng[i] = AngNorm(pp->viewangles[i] - before[i]);

		AngVectors(pp->viewangles, pp->forward, pp->right, pp->up);
	}

	// --- Viewmodel ---------------------------------------------------------------
	cl_entity_t *vm = eng->GetViewModel();
	if (!vm || !vm->model)
		return;

	// The hands are attached to the head: they take the camera's bob, impacts,
	// lag and lean, so they don't swim against the view.
	VectorAdd(vm->origin, camDelta, vm->origin);
	vm->angles[0] -= camAng[0];     // studio pitch is stored negated
	vm->angles[1] += camAng[1];
	vm->angles[2] += camAng[2];
	if (!vmOn)
		return;

	float sway = vm_sway->value * steady;
	float act  = vm_action->value;
	float t    = pp->time;

	// Inertia: the hands trail the rotation, then spring back with a little overshoot.
	float swayYawT   = Clamp(-yawRate * 0.006f, -5.0f, 5.0f) * sway;
	float swayPitchT = Clamp(-pitchRate * 0.006f, -4.0f, 4.0f) * sway;
	s_vmYaw.Step(swayYawT, 90.0f, 11.0f, dt);
	s_vmPitch.Step(swayPitchT, 90.0f, 11.0f, dt);

	// Breathing: slow wave while standing still.
	float idle = 1.0f - Clamp(speed / 40.0f, 0.0f, 1.0f);
	float br = vm_breath->value * idle * (1.0f - 0.6f * s_ads);
	float breathe = sinf(t * (2.0f * (float)M_PI / 4.2f));
	float drift   = sinf(t * (2.0f * (float)M_PI / 7.3f) + 1.3f);

	// Movement bob of the hands (opposite phase to the head, smaller).
	float hb = s_bobAmp * steady * (cam_bob ? cam_bob->value : 1.0f);
	float ph = s_stepPhase * (float)M_PI;
	float handSide = -0.35f * hb * sinf(ph);
	float handUp   =  0.25f * hb * cosf(2.0f * ph);

	// Actions: sprint lowers and turns the weapon in, airborne floats it, fire kicks it back.
	s_reload = Approach(s_reload, ViewmodelReloading(vm) ? 1.0f : 0.0f, 6.0f, dt);
	float airLift = onGround ? 0.0f : Clamp(-pp->simvel[2] * 0.004f, -1.2f, 1.2f);
	if (landImpulse > 0.0f)
		s_vmZ.vel -= landImpulse * 6.0f * act;
	bool firePressed = (s_buttons & IN_ATTACK) && !(s_prevButtons & IN_ATTACK);
	s_prevButtons = s_buttons;
	if (firePressed)
	{
		s_vmKick.vel      -= 25.0f * act * (1.0f - 0.5f * s_ads);
		s_vmKickPitch.vel += 30.0f * act * (1.0f - 0.5f * s_ads);
	}
	s_vmKick.Step(0.0f, 180.0f, 18.0f, dt);
	s_vmKickPitch.Step(0.0f, 180.0f, 18.0f, dt);

	float targetX = act * (-0.8f * s_sprint) + s_vmKick.pos * 0.04f;
	float targetY = handSide + drift * 0.08f * br - s_vmYaw.pos * 0.12f;
	float targetZ = handUp + breathe * 0.22f * br + act * (-1.2f * s_sprint - 0.5f * s_reload + airLift)
	              + s_vmPitch.pos * 0.08f;
	s_vmX.Step(targetX, 70.0f, 15.0f, dt);
	s_vmY.Step(targetY, 70.0f, 15.0f, dt);
	s_vmZ.Step(targetZ, 70.0f, 13.0f, dt);
	s_vmRoll.Step(act * (-5.0f * s_sprint + 4.0f * s_reload) , 60.0f, 14.0f, dt);

	// Keep everything small enough that the hands never leave or cut through the screen.
	float ox = Clamp(s_vmX.pos, -2.5f, 1.0f);
	float oy = Clamp(s_vmY.pos, -1.5f, 1.5f);
	float oz = Clamp(s_vmZ.pos, -2.5f, 1.5f);

	float vf[3], vr[3], vu[3];
	AngVectors(pp->viewangles, vf, vr, vu);
	for (int i = 0; i < 3; i++)
		vm->origin[i] += vf[i] * ox + vr[i] * oy + vu[i] * oz;

	// Studio viewmodels store pitch negated: subtracting tips the weapon down.
	float pitchDown = s_vmPitch.pos + act * 8.0f * s_sprint + breathe * 0.5f * br + s_vmKickPitch.pos * -0.06f;
	vm->angles[0] -= Clamp(pitchDown, -8.0f, 12.0f);
	vm->angles[1] += Clamp(s_vmYaw.pos + act * 6.0f * s_sprint, -8.0f, 10.0f);
	vm->angles[2] += Clamp(s_vmRoll.pos, -8.0f, 8.0f);
}

float FpCam_AimWeight(void)
{
	return s_active ? s_ads : 0.0f;
}

int FpCam_Health(void)
{
	return s_active ? s_health : -1;
}

// ---------------------------------------------------------------------------
// FOV
// ---------------------------------------------------------------------------

int FpCam_UpdateClientData(client_data_t *cdata, float time, int changed)
{
	static float last = -1.0f;
	static float lastBase, lastOut;     // what the game asked for / what we handed the engine
	float dt = (last < 0.0f) ? 0.0f : Clamp(time - last, 0.0f, 0.1f);
	last = time;

	// If the client didn't set a fresh FOV this frame, the engine handed us back
	// our own scaled value: undo it so the scale never compounds.
	float base = cdata->fov;
	if (!changed && lastOut > 0.0f && fabsf(cdata->fov - lastOut) < 0.01f)
		base = lastBase;

	cvar_t *def = g_studioEng.GetCvar ? g_studioEng.GetCvar("default_fov") : NULL;
	float defFov = def ? def->value : 90.0f;
	bool on = fov_enable && fov_enable->value != 0.0f && s_active;
	bool gameZoom = base < defFov - 1.0f;      // leave the game's own zooms alone

	float target = 1.0f;
	if (on && !gameZoom)
		target = Clamp(1.0f + s_sprint * fov_sprint->value * 0.01f - s_ads * fov_ads->value * 0.01f, 0.75f, 1.2f);
	s_fovScale = gameZoom ? 1.0f : Approach(s_fovScale, target, 7.0f, dt);

	if (fabsf(s_fovScale - 1.0f) < 0.001f && lastOut == 0.0f)
		return changed;                       // nothing of ours to apply or undo

	cdata->fov = Clamp(base * s_fovScale, 50.0f, 120.0f);
	lastBase = base;
	lastOut = (fabsf(s_fovScale - 1.0f) < 0.001f) ? 0.0f : cdata->fov;
	return 1;
}
