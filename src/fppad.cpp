// Controller support (Steam Deck, Xbox and other XInput pads).
//
// The pad is read through XInput: Steam Input presents the Steam Deck's controls
// to Windows games (and to Proton) as an Xbox controller when the game uses a
// gamepad layout in Steam.
//
// F8 menu: FpPad_Poll reports button presses since the last call, directions
// repeating while held (D-pad or left stick); fpmenu.cpp acts on them.
//
// Playing (original engine only; Cry of Fear: Enhanced has its own controller
// support): the same layout as Enhanced's, so both feel the same.
//   left stick   move                 right stick  look
//   RT           attack               LT           aim (+attack3)
//   LB           secondary attack     RB           dodge
//   A            jump                 B            crouch (toggle)
//   X            reload               Y            use / pick up
//   L3           sprint (toggle)      R3           quick 180 turn
//   D-pad        quick slots 1 / 2 / 3 (up / left / right), weapon toggle (down)
//   View         inventory            Menu         pause (Esc)
//   L3 + R3      F8 menu
// Buttons run the commands their keys are bound to (+attack, quicksel 1...), the
// same way pressing the key does; the sticks are added to the movement and view.

#include <windows.h>
#include <Xinput.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cvardef.h"
#include "usercmd.h"
#include "fppad.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE *);
static XInputGetState_t s_getState;
static bool  s_tried;
static int   s_pad = -1;                  // connected pad in use
static float s_nextProbe;                 // probing empty slots is slow: not every frame
static XINPUT_GAMEPAD s_now;              // the pad as last read
static WORD  s_prev;
static bool  s_prevLT, s_prevRT;
static float s_dirNext[4];                // repeat timing: up, down, left, right
static bool  s_dirHeld[4];

static cvar_t *s_enable, *s_look, *s_invert;

void FpPad_Init(void)
{
	s_enable = FpRegister("cl_fppad", "1", FCVAR_ARCHIVE);          // play with a controller (original engine)
	s_look   = FpRegister("cl_fppad_look", "1", FCVAR_ARCHIVE);     // right stick look speed
	s_invert = FpRegister("cl_fppad_invert", "0", FCVAR_ARCHIVE);   // 1 = up / down inverted
}

static bool Load(void)
{
	if (s_tried) return s_getState != NULL;
	s_tried = true;
	static const char *const kDlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
	for (const char *dll : kDlls)
		if (HMODULE m = LoadLibraryA(dll))
			if ((s_getState = (XInputGetState_t)GetProcAddress(m, "XInputGetState")) != NULL)
				return true;
	return false;
}

// Pressed now and repeating: true on the first frame, then every 0.09 s after 0.35 s.
static bool Repeat(int i, bool held, float now)
{
	if (!held) { s_dirHeld[i] = false; return false; }
	if (!s_dirHeld[i]) { s_dirHeld[i] = true; s_dirNext[i] = now + 0.35f; return true; }
	if (now >= s_dirNext[i]) { s_dirNext[i] = now + 0.09f; return true; }
	return false;
}

// Fills *ev with what happened since the last call. False when no pad is connected.
bool FpPad_Poll(float now, PadEvents *ev)
{
	memset(ev, 0, sizeof(*ev));
	if (!Load())
		return false;
	XINPUT_STATE st;
	if (s_pad >= 0 && s_getState(s_pad, &st) != ERROR_SUCCESS)
		s_pad = -1;
	if (s_pad < 0)
	{
		memset(&s_now, 0, sizeof(s_now));
		if (now < s_nextProbe && s_nextProbe - now < 5.0f) return false;   // (the clock restarts on map changes)
		s_nextProbe = now + 2.0f;
		for (DWORD i = 0; i < 4 && s_pad < 0; i++)
			if (s_getState(i, &st) == ERROR_SUCCESS) s_pad = (int)i;
		if (s_pad < 0) return false;
		s_now = st.Gamepad;
		s_prev = st.Gamepad.wButtons;              // don't act on what was already held
		s_prevLT = st.Gamepad.bLeftTrigger > 128;
		s_prevRT = st.Gamepad.bRightTrigger > 128;
		return true;
	}
	const XINPUT_GAMEPAD &g = st.Gamepad;
	s_now = g;
	WORD b = g.wButtons, pressed = b & ~s_prev;
	s_prev = b;
	float lx = g.sThumbLX / 32768.0f, ly = g.sThumbLY / 32768.0f;
	ev->up    = Repeat(0, (b & XINPUT_GAMEPAD_DPAD_UP) || ly > 0.6f, now);
	ev->down  = Repeat(1, (b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -0.6f, now);
	ev->left  = Repeat(2, (b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -0.6f, now);
	ev->right = Repeat(3, (b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.6f, now);
	ev->a     = (pressed & XINPUT_GAMEPAD_A) != 0;
	ev->b     = (pressed & XINPUT_GAMEPAD_B) != 0;
	ev->x     = (pressed & XINPUT_GAMEPAD_X) != 0;
	ev->y     = (pressed & XINPUT_GAMEPAD_Y) != 0;
	ev->lb    = (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
	ev->rb    = (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
	ev->start = (pressed & XINPUT_GAMEPAD_START) != 0;
	bool lt = g.bLeftTrigger > 128, rt = g.bRightTrigger > 128;
	ev->lt = lt && !s_prevLT;
	ev->rt = rt && !s_prevRT;
	s_prevLT = lt; s_prevRT = rt;
	const WORD sticks = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
	ev->toggle = (b & sticks) == sticks && (pressed & sticks) != 0;
	ev->any = ev->up || ev->down || ev->left || ev->right || ev->a || ev->b || ev->x || ev->y ||
		ev->lb || ev->rb || ev->lt || ev->rt || ev->start || ev->toggle;
	return true;
}

// ---------------------------------------------------------------------------
// Playing with the controller (original engine)
// ---------------------------------------------------------------------------

static bool Playing(void)
{
	return s_pad >= 0 && s_enable && s_enable->value != 0.0f && !GetModuleHandleA("xash.dll");
}

static void Cmd(const char *c)
{
	char buf[64];
	_snprintf(buf, sizeof(buf), "%s\n", c);
	buf[sizeof(buf) - 1] = 0;
	eng->pfnClientCmd(buf);
}

// Held buttons: "+x" while down, "-x" when let go.
enum { H_ATTACK, H_AIM, H_ATTACK2, H_DODGE, H_JUMP, H_RELOAD, H_USE, H_INVENTORY, H_SPRINT, H_DUCK, NUM_HOLDS };
static const char *const kHold[NUM_HOLDS] = { "attack", "attack3", "attack2", "dodge", "jump", "reload", "use", "inventory", "sprint", "duck" };
static bool s_sent[NUM_HOLDS];

static void Set(int h, bool on)
{
	if (s_sent[h] == on) return;
	s_sent[h] = on;
	char c[32];
	_snprintf(c, sizeof(c), "%c%s", on ? '+' : '-', kHold[h]);
	Cmd(c);
}

static void ReleaseAll(void)
{
	for (int h = 0; h < NUM_HOLDS; h++) Set(h, false);
}

static bool  s_suppress;          // after the menu: ignore the pad until everything is let go
static bool  s_sprint, s_duck;    // toggles
static WORD  s_gamePrev;
static bool  s_l3Other, s_r3Other; // the other stick was pressed while this one was held (L3 + R3)
static float s_sprintIdle;
static float s_turnLeft;          // quick turn: degrees still to turn

// The game window, for the pause key.
static BOOL CALLBACK FindOwn(HWND w, LPARAM out)
{
	DWORD pid;
	GetWindowThreadProcessId(w, &pid);
	if (pid == GetCurrentProcessId() && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) { *(HWND *)out = w; return FALSE; }
	return TRUE;
}

static void PressEscape(void)
{
	HWND w = NULL;
	EnumWindows(FindOwn, (LPARAM)&w);
	if (!w) return;
	UINT scan = MapVirtualKeyA(VK_ESCAPE, 0);
	PostMessageA(w, WM_KEYDOWN, VK_ESCAPE, 1 | (scan << 16));
	PostMessageA(w, WM_KEYUP, VK_ESCAPE, 1 | (scan << 16) | (1u << 30) | (1u << 31));
}

static float StickLen(SHORT x, SHORT y) { float fx = x / 32768.0f, fy = y / 32768.0f; return sqrtf(fx * fx + fy * fy); }

// Every frame: buttons to the game's commands.
void FpPad_Game(bool menuOpen, float now)
{
	static float last;
	float dt = now - last;
	if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
	last = now;
	if (!Playing())
	{
		ReleaseAll();
		s_sprint = s_duck = false;
		return;
	}
	const XINPUT_GAMEPAD &g = s_now;
	WORD b = g.wButtons;
	bool lt = g.bLeftTrigger > 77, rt = g.bRightTrigger > 77;   // 30%
	if (menuOpen)
	{
		ReleaseAll();
		s_suppress = true;
		s_gamePrev = b;
		return;
	}
	if (s_suppress)                       // the buttons that closed the menu aren't for the game,
	{                                     // nor letting go of them
		s_gamePrev = b;
		if (!b && !lt && !rt) { s_suppress = false; s_l3Other = s_r3Other = false; }
		return;
	}
	WORD pressed = b & ~s_gamePrev, released = s_gamePrev & ~b;
	s_gamePrev = b;

	Set(H_ATTACK, rt);
	Set(H_AIM, lt);
	Set(H_ATTACK2, (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
	Set(H_DODGE, (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
	Set(H_JUMP, (b & XINPUT_GAMEPAD_A) != 0);
	Set(H_RELOAD, (b & XINPUT_GAMEPAD_X) != 0);
	Set(H_USE, (b & XINPUT_GAMEPAD_Y) != 0);
	Set(H_INVENTORY, (b & XINPUT_GAMEPAD_BACK) != 0);

	if (pressed & XINPUT_GAMEPAD_DPAD_UP)    Cmd("quicksel 1");
	if (pressed & XINPUT_GAMEPAD_DPAD_LEFT)  Cmd("quicksel 2");
	if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) Cmd("quicksel 3");
	if (pressed & XINPUT_GAMEPAD_DPAD_DOWN)  Cmd("weapontoggle");
	if (pressed & XINPUT_GAMEPAD_START)      PressEscape();
	if (pressed & XINPUT_GAMEPAD_B)          s_duck = !s_duck;
	Set(H_DUCK, s_duck);

	// Stick clicks act when let go, unless the other one was pressed too (that's the menu).
	if (pressed & XINPUT_GAMEPAD_LEFT_THUMB)  s_l3Other = false;
	if (pressed & XINPUT_GAMEPAD_RIGHT_THUMB) s_r3Other = false;
	if (b & XINPUT_GAMEPAD_LEFT_THUMB && b & XINPUT_GAMEPAD_RIGHT_THUMB) s_l3Other = s_r3Other = true;
	if ((released & XINPUT_GAMEPAD_LEFT_THUMB) && !s_l3Other)  s_sprint = !s_sprint;
	if ((released & XINPUT_GAMEPAD_RIGHT_THUMB) && !s_r3Other) s_turnLeft = 180.0f;
	// Sprinting stops once you stop moving.
	if (s_sprint && StickLen(g.sThumbLX, g.sThumbLY) < 0.25f)
	{
		s_sprintIdle += dt;
		if (s_sprintIdle > 0.3f) s_sprint = false;
	}
	else
		s_sprintIdle = 0.0f;
	Set(H_SPRINT, s_sprint);
}

// A stick with a round dead zone, rescaled so it starts from 0 at the edge of it.
static void Stick(SHORT sx, SHORT sy, float dead, float *x, float *y)
{
	float fx = sx / 32767.0f, fy = sy / 32767.0f, len = sqrtf(fx * fx + fy * fy);
	if (len < dead) { *x = *y = 0.0f; return; }
	float k = (fminf(len, 1.0f) - dead) / (1.0f - dead) / len;
	*x = fx * k; *y = fy * k;
}

// After the game built the move: add the sticks.
void FpPad_CreateMove(float frametime, usercmd_s *cmd, bool menuOpen)
{
	if (!Playing() || menuOpen || s_suppress)
	{
		s_turnLeft = 0.0f;
		return;
	}
	if (frametime <= 0.0f || frametime > 0.1f) frametime = 0.016f;
	const XINPUT_GAMEPAD &g = s_now;
	float mx, my, lx, ly;
	Stick(g.sThumbLX, g.sThumbLY, 0.24f, &mx, &my);
	Stick(g.sThumbRX, g.sThumbRY, 0.20f, &lx, &ly);

	// Moving: as far as the stick is pushed, up to the full speed.
	float fwd = eng->pfnGetCvarFloat((char *)"cl_forwardspeed"), side = eng->pfnGetCvarFloat((char *)"cl_sidespeed");
	if (fwd <= 0.0f) fwd = 400.0f;
	if (side <= 0.0f) side = 400.0f;
	cmd->forwardmove += my * fwd;
	cmd->sidemove += mx * side;
	if (cmd->forwardmove > fwd) cmd->forwardmove = fwd;
	if (cmd->forwardmove < -fwd) cmd->forwardmove = -fwd;
	if (cmd->sidemove > side) cmd->sidemove = side;
	if (cmd->sidemove < -side) cmd->sidemove = -side;

	// Looking: squared for fine aim near the centre; 220 / 150 degrees a second at full tilt.
	float speed = s_look ? s_look->value : 1.0f;
	float ax = lx * fabsf(lx), ay = ly * fabsf(ly);
	if (s_invert && s_invert->value != 0.0f) ay = -ay;
	float ang[3];
	eng->GetViewAngles(ang);
	ang[1] -= ax * 220.0f * speed * frametime;
	ang[0] -= ay * 150.0f * speed * frametime;
	if (s_turnLeft > 0.0f)                         // quick turn: 180 degrees in a quarter of a second
	{
		float step = fminf(s_turnLeft, 720.0f * frametime);
		ang[1] += step;
		s_turnLeft -= step;
	}
	if (ang[0] > 89.0f) ang[0] = 89.0f;
	if (ang[0] < -89.0f) ang[0] = -89.0f;
	eng->SetViewAngles(ang);
	VectorCopy(ang, cmd->viewangles);
}
