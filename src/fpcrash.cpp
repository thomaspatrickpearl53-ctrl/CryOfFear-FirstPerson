// Crash logging: records the first few serious exceptions (access violations
// and the like) to fpbody.log as module+offset with a stack walk, before any
// other handler sees them. Some of the game's libraries catch a crash and then
// wait forever, which looks like a freeze; this shows where it really started.
// Only observes: every exception continues to the game's own handlers.

#include <windows.h>
#include <stdio.h>

void FpLog(const char *fmt, ...);

static volatile LONG s_logged;
static const LONG    kMaxLogged = 20;

static void Where(DWORD addr, char *out, size_t size)
{
	HMODULE mod = NULL;
	char path[MAX_PATH];
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(UINT_PTR)addr, &mod) && mod && GetModuleFileNameA(mod, path, sizeof(path)))
	{
		const char *name = strrchr(path, '\\');
		_snprintf(out, size, "%s+0x%X", name ? name + 1 : path, addr - (DWORD)(UINT_PTR)mod);
	}
	else
		_snprintf(out, size, "0x%08X", addr);
	out[size - 1] = 0;
}

static bool InModule(DWORD addr)
{
	HMODULE mod = NULL;
	return addr > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)(UINT_PTR)addr, &mod) && mod;
}

static LONG CALLBACK Handler(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
		code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
		code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
		return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedIncrement(&s_logged) > kMaxLogged)
		return EXCEPTION_CONTINUE_SEARCH;

	char at[300], buf[300];
	CONTEXT *c = ep->ContextRecord;
	Where(c->Eip, at, sizeof(at));
	if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
		FpLog("crash: access violation at %s (%s address 0x%08X) thread %lu\n", at,
			ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
			(DWORD)ep->ExceptionRecord->ExceptionInformation[1], GetCurrentThreadId());
	else
		FpLog("crash: exception 0x%08X at %s thread %lu\n", code, at, GetCurrentThreadId());
	FpLog("crash:   eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X esp=%08X ebp=%08X\n",
		c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Esp, c->Ebp);

	// Return addresses found on the stack (works without frame pointers too).
	if (code != EXCEPTION_STACK_OVERFLOW)
	{
		DWORD *sp = (DWORD *)(UINT_PTR)c->Esp;
		int shown = 0;
		for (int i = 0; i < 512 && shown < 16; i++)
		{
			if (IsBadReadPtr(sp + i, 4))
				break;
			DWORD v = sp[i];
			if (!InModule(v))
				continue;
			Where(v, buf, sizeof(buf));
			if (strstr(buf, ".exe") || strstr(buf, ".dll") || strstr(buf, ".DLL"))
			{
				FpLog("crash:   stack+%03X %s\n", i * 4, buf);
				shown++;
			}
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

void FpCrash_Init(void)
{
	static bool done;
	if (done)
		return;
	done = true;
	AddVectoredExceptionHandler(1, Handler);
}
