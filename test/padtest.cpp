// Offline test of the controller input (src/fppad.cpp).
//   padtest.exe        prints the real pad's menu events for 10 s
//   padtest.exe fake   runs a scripted fake pad and checks the events
#include "../src/fppad.cpp"
#include <stdio.h>

static WORD  g_buttons;
static SHORT g_ly;
static BYTE  g_rt;
static DWORD WINAPI FakeGetState(DWORD i, XINPUT_STATE *st)
{
	if (i != 0) return ERROR_DEVICE_NOT_CONNECTED;
	memset(st, 0, sizeof(*st));
	st->Gamepad.wButtons = g_buttons; st->Gamepad.sThumbLY = g_ly; st->Gamepad.bRightTrigger = g_rt;
	return ERROR_SUCCESS;
}

static void Print(float now, const PadEvents &ev)
{
	printf("%.2f %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", now, ev.up ? "up " : "", ev.down ? "down " : "", ev.left ? "left " : "", ev.right ? "right " : "",
		ev.a ? "A " : "", ev.b ? "B " : "", ev.x ? "X " : "", ev.y ? "Y " : "", ev.lb ? "LB " : "", ev.rb ? "RB " : "",
		ev.lt ? "LT " : "", ev.rt ? "RT " : "", ev.start ? "START " : "", ev.toggle ? "L3+R3 " : "");
}

int main(int argc, char **argv)
{
	PadEvents ev;
	if (argc > 1 && !strcmp(argv[1], "fake"))
	{
		s_tried = true; s_getState = FakeGetState;
		// 0.0-0.2 nothing; 0.2 hold A until 0.3; 0.5-1.2 hold D-pad down; 1.4-1.9 left stick up;
		// 2.0 L3 then R3 (combo); 2.4 right trigger
		for (int f = 0; f <= 160; f++)
		{
			float t = f / 60.0f;
			g_buttons = 0; g_ly = 0; g_rt = 0;
			if (t >= 0.2f && t < 0.3f) g_buttons |= XINPUT_GAMEPAD_A;
			if (t >= 0.5f && t < 1.2f) g_buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
			if (t >= 1.4f && t < 1.9f) g_ly = 30000;
			if (t >= 2.0f && t < 2.2f) g_buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
			if (t >= 2.05f && t < 2.2f) g_buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			if (t >= 2.4f && t < 2.5f) g_rt = 255;
			FpPad_Poll(t, &ev);
			if (ev.any) Print(t, ev);
		}
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
