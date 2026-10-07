// Controller input for the F8 menu (Steam Deck, Xbox and other XInput pads).
//
// The menu reads the pad itself through XInput: Steam Input presents the Steam
// Deck's controls to Windows games (and to Proton) as an Xbox controller, so this
// works whatever the game's own joystick settings are. Nothing here reaches the
// game; fpmenu.cpp keeps the player still while the menu is open.
//
// FpPad_Poll reports button presses since the last call; directions repeat while
// held (D-pad or left stick).

#include <windows.h>
#include <Xinput.h>
#include <math.h>
#include <string.h>

struct PadEvents
{
	bool up, down, left, right;            // D-pad / left stick (repeat while held)
	bool a, b, x, y, lb, rb, lt, rt, start;
	bool toggle;                           // both sticks pressed in together (L3 + R3)
	bool any;
};

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE *);
static XInputGetState_t s_getState;
static bool  s_tried;
static int   s_pad = -1;                  // connected pad in use
static float s_nextProbe;                 // probing empty slots is slow: not every frame
static WORD  s_prev;
static bool  s_prevLT, s_prevRT;
static float s_dirStart[4], s_dirNext[4]; // repeat timing: up, down, left, right
static bool  s_dirHeld[4];

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
	if (!s_dirHeld[i]) { s_dirHeld[i] = true; s_dirStart[i] = now; s_dirNext[i] = now + 0.35f; return true; }
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
		if (now < s_nextProbe && now >= s_nextProbe - 5.0f) return false;
		s_nextProbe = now + 2.0f;
		for (DWORD i = 0; i < 4 && s_pad < 0; i++)
			if (s_getState(i, &st) == ERROR_SUCCESS) s_pad = (int)i;
		if (s_pad < 0) return false;
		s_prev = st.Gamepad.wButtons;              // don't act on what was already held
		s_prevLT = st.Gamepad.bLeftTrigger > 128;
		s_prevRT = st.Gamepad.bRightTrigger > 128;
		return true;
	}
	const XINPUT_GAMEPAD &g = st.Gamepad;
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
