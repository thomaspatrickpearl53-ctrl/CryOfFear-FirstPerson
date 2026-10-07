// Offline test of the controller (src/fppad.cpp).
//   padtest.exe        prints the real pad's menu events for 10 s
//   padtest.exe fake   runs a scripted fake pad: menu events, then playing
#include "../src/fppad.cpp"
#include <stdio.h>
#include <stdlib.h>

static WORD  g_buttons;
static SHORT g_lx, g_ly, g_rx;
static BYTE  g_rt;
static DWORD WINAPI FakeGetState(DWORD i, XINPUT_STATE *st)
{
	if (i != 0) return ERROR_DEVICE_NOT_CONNECTED;
	memset(st, 0, sizeof(*st));
	st->Gamepad.wButtons = g_buttons; st->Gamepad.sThumbLX = g_lx; st->Gamepad.sThumbLY = g_ly;
	st->Gamepad.sThumbRX = g_rx; st->Gamepad.bRightTrigger = g_rt;
	return ERROR_SUCCESS;
}

// the engine, faked
static float g_time, g_ang[3];
static int  FakeCmd(char *s) { printf("%5.2f  cmd %s", g_time, s); return 0; }
static float FakeCvar(char *n) { return !strcmp(n, "cl_forwardspeed") || !strcmp(n, "cl_sidespeed") ? 400.0f : 0.0f; }
static void FakeGetAngles(float *a) { memcpy(a, g_ang, sizeof(g_ang)); }
static void FakeSetAngles(float *a) { memcpy(g_ang, a, sizeof(g_ang)); }
static cl_enginefunc_t g_eng;
cl_enginefunc_t *eng = &g_eng;
cvar_t *FpRegister(const char *, const char *value, int)
{
	cvar_t *c = (cvar_t *)calloc(1, sizeof(cvar_t));
	c->value = (float)atof(value);
	return c;
}

static void Print(float now, const PadEvents &ev)
{
	printf("%5.2f  menu %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", now, ev.up ? "up " : "", ev.down ? "down " : "", ev.left ? "left " : "", ev.right ? "right " : "",
		ev.a ? "A " : "", ev.b ? "B " : "", ev.x ? "X " : "", ev.y ? "Y " : "", ev.lb ? "LB " : "", ev.rb ? "RB " : "",
		ev.lt ? "LT " : "", ev.rt ? "RT " : "", ev.start ? "START " : "", ev.toggle ? "L3+R3 " : "");
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	PadEvents ev;
	g_eng.pfnClientCmd = FakeCmd; g_eng.pfnGetCvarFloat = FakeCvar;
	g_eng.GetViewAngles = FakeGetAngles; g_eng.SetViewAngles = FakeSetAngles;
	FpPad_Init();
	if (argc > 1 && !strcmp(argv[1], "fake"))
	{
		s_tried = true; s_getState = FakeGetState;
		bool menu = false;
		float lastYaw = 0, lastMove = -1;
		for (int f = 0; f <= 9 * 60; f++)
		{
			float t = g_time = f / 60.0f;
			g_buttons = 0; g_lx = g_ly = g_rx = 0; g_rt = 0;
			// menu part: L3 + R3 opens it, A, D-pad down held, L3 + R3 closes it
			if (t >= 0.2f && t < 0.4f) g_buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
			if (t >= 0.25f && t < 0.4f) g_buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			if (t >= 0.6f && t < 0.7f) g_buttons |= XINPUT_GAMEPAD_A;
			if (t >= 0.8f && t < 1.3f) g_buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
			if (t >= 1.5f && t < 1.7f) g_buttons |= XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
			// playing: right trigger, B twice, D-pad up, walk with L3 (sprint) then stop, R3 quick turn, look right
			if (t >= 2.0f && t < 2.3f) g_rt = 255;
			if ((t >= 2.5f && t < 2.6f) || (t >= 3.0f && t < 3.1f)) g_buttons |= XINPUT_GAMEPAD_B;
			if (t >= 3.3f && t < 3.4f) g_buttons |= XINPUT_GAMEPAD_DPAD_UP;
			if (t >= 3.6f && t < 5.0f) g_ly = 32767;
			if (t >= 3.8f && t < 3.9f) g_buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
			if (t >= 5.6f && t < 5.7f) g_buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			if (t >= 6.5f && t < 7.5f) g_rx = 32767;
			if (t >= 8.0f && t < 8.1f) g_buttons |= XINPUT_GAMEPAD_START;

			if (FpPad_Poll(t, &ev) && ev.any)
			{
				Print(t, ev);
				if (ev.toggle) { menu = !menu; printf("%5.2f  -> menu %s\n", t, menu ? "open" : "closed"); }
			}
			FpPad_Game(menu, t);
			usercmd_t cmd = {};
			FpPad_CreateMove(1.0f / 60.0f, &cmd, menu);
			if (cmd.forwardmove != lastMove) { printf("%5.2f  move forward %.0f\n", t, cmd.forwardmove); lastMove = cmd.forwardmove; }
			if (fabsf(g_ang[1] - lastYaw) > 0.01f && (f % 15 == 0)) { printf("%5.2f  yaw %.1f\n", t, g_ang[1]); }
			lastYaw = g_ang[1];
		}
		printf("final yaw %.1f\n", g_ang[1]);
		return 0;
	}
	DWORD t0 = GetTickCount();
	bool said = false;
	while (GetTickCount() - t0 < 10000)
	{
		float now = (GetTickCount() - t0) / 1000.0f;
		bool ok = FpPad_Poll(now, &ev);
		if (!said) { printf("xinput %s, pad %s\n", s_getState ? "loaded" : "missing", ok ? "connected" : "none"); said = true; }
		if (ev.any) Print(now, ev);
		Sleep(16);
	}
	return 0;
}
