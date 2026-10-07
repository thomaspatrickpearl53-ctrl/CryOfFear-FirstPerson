// Controller (fppad.cpp): the F8 menu's events and, on the original engine,
// playing the game with a controller.
#pragma once

struct usercmd_s;

struct PadEvents
{
	bool up, down, left, right;            // D-pad / left stick (repeat while held)
	bool a, b, x, y, lb, rb, lt, rt, start;
	bool toggle;                           // both sticks pressed in together (L3 + R3)
	bool any;
};

void FpPad_Init(void);
bool FpPad_Poll(float now, PadEvents *ev);              // menu events since the last call; once per frame
void FpPad_Game(bool menuOpen, float now);              // buttons -> the game's commands
void FpPad_CreateMove(float frametime, struct usercmd_s *cmd, bool menuOpen);   // sticks -> moving and looking
