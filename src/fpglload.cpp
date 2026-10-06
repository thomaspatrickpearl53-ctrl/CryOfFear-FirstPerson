// Which OpenGL the mod draws with.
//
// The mod's OpenGL calls are delay-loaded (build.bat: /DELAYLOAD:opengl32.dll),
// so they bind on first use, after the engine has set up its renderer. If the
// engine runs through the Wine/Proton graphics fix (tools/cof_glfix.py), its
// renderer is opengp32.dll, and the mod must use that same module rather than
// load a second, unfixed copy of Cry of Fear's opengl32.dll wrapper. Without
// the fix this changes nothing: opengl32.dll is loaded the usual way.

#include <windows.h>
#include <delayimp.h>

static FARPROC WINAPI DelayHook(unsigned notify, PDelayLoadInfo info)
{
	if (notify == dliNotePreLoadLibrary && info && info->szDll && !_stricmp(info->szDll, "opengl32.dll"))
		return (FARPROC)GetModuleHandleA("opengp32.dll");   // NULL: load opengl32.dll normally
	return NULL;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DelayHook;
