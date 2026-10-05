// Shows where a frozen (hung) 32-bit game is stuck: samples every thread's
// instruction pointer a few times and prints module+offset, plus a short
// frame-pointer stack walk. Read-only: threads are suspended for a moment only.
// Build (32-bit): cl /EHsc test\hangwhere.cpp psapi.lib
// Usage: hangwhere.exe <pid>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <vector>
#include <string>

struct Mod { DWORD base, size; std::string name; };
static std::vector<Mod> g_mods;

static void LoadModules(HANDLE proc)
{
	HMODULE mods[1024];
	DWORD need = 0;
	if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_32BIT))
		return;
	for (DWORD i = 0; i < need / sizeof(HMODULE); i++)
	{
		MODULEINFO mi;
		char name[MAX_PATH];
		if (GetModuleInformation(proc, mods[i], &mi, sizeof(mi)) && GetModuleBaseNameA(proc, mods[i], name, sizeof(name)))
			g_mods.push_back({ (DWORD)mi.lpBaseOfDll, mi.SizeOfImage, name });
	}
}

static std::string Where(DWORD addr)
{
	char buf[300];
	for (auto &m : g_mods)
		if (addr >= m.base && addr < m.base + m.size)
		{
			sprintf(buf, "%s+0x%X", m.name.c_str(), addr - m.base);
			return buf;
		}
	sprintf(buf, "0x%08X", addr);
	return buf;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: hangwhere <pid>\n");
		return 2;
	}
	DWORD pid = atoi(argv[1]);
	HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!proc)
	{
		printf("can't open process %lu (error %lu)\n", pid, GetLastError());
		return 1;
	}
	LoadModules(proc);

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 te = { sizeof(te) };
	std::vector<DWORD> tids;
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
		if (te.th32OwnerProcessID == pid)
			tids.push_back(te.th32ThreadID);
	CloseHandle(snap);

	// "hangwhere <pid> <tid>": search that thread's stack for exception records
	// (a crash that a handler turned into a wait leaves one there).
	if (argc >= 3)
	{
		DWORD tid = atoi(argv[2]);
		HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
		CONTEXT ctx = {};
		ctx.ContextFlags = CONTEXT_CONTROL;
		if (!th || SuspendThread(th) == (DWORD)-1 || !GetThreadContext(th, &ctx))
		{
			printf("can't read thread %lu\n", tid);
			return 1;
		}
		ResumeThread(th);
		static DWORD stack[0x40000 / 4];
		SIZE_T got = 0;
		// Optional 3rd arg: also read this many KB below esp (stale frames of an
		// unwound crash).
		DWORD below = argc >= 4 ? atoi(argv[3]) * 1024 : 0;
		DWORD base = (ctx.Esp - below) & ~0xFFFu;
		if (below)
			printf("esp=%08X, reading from %08X\n", ctx.Esp, base);
		ReadProcessMemory(proc, (void *)base, stack, sizeof(stack), &got);
		if (!got)
		{
			// read page by page up to the end of the stack
			for (SIZE_T off = 0; off < sizeof(stack); off += 0x1000)
			{
				SIZE_T g = 0;
				if (!ReadProcessMemory(proc, (void *)(base + off), (byte *)stack + off, 0x1000 - ((base + off) & 0xFFF), &g) || !g)
					break;
				got = off + g;
			}
		}
		printf("read %lu bytes of stack from esp=%08X\n", (DWORD)got, base);
		DWORD n = (DWORD)(got / 4);
		for (DWORD i = 0; i + 6 < n; i++)
		{
			DWORD code = stack[i];
			if ((code & 0xF0000000) != 0xC0000000 || stack[i + 1] > 1 || stack[i + 4] > 15)
				continue;
			DWORD addr = stack[i + 3];
			std::string w = Where(addr);
			if (w[0] == '0')
				continue;   // exception address must be inside a module
			printf("exception record at esp+%X: code %08X flags %u at %s", i * 4, code, stack[i + 1], w.c_str());
			if (code == 0xC0000005 && stack[i + 4] >= 2)
				printf(" (%s %08X)", stack[i + 5] ? "write" : "read", stack[i + 6]);
			printf("\n");
			// return addresses just above the record's context: the faulting call chain
		}
		// every return address on the stack, top first
		printf("stack values pointing into modules:\n");
		int shown = 0;
		for (DWORD i = 0; i < n && shown < 4000; i++)
		{
			std::string w = Where(stack[i]);
			if (w[0] != '0' && stack[i] > 0x10000)
			{
				printf("  esp+%05X %s\n", i * 4, w.c_str());
				shown++;
			}
		}
		return 0;
	}

	for (DWORD tid : tids)
	{
		HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
		if (!th)
			continue;
		printf("thread %lu:\n", tid);
		for (int sample = 0; sample < 5; sample++)
		{
			if (SuspendThread(th) == (DWORD)-1)
				break;
			CONTEXT ctx = {};
			ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
			if (GetThreadContext(th, &ctx))
			{
				printf("  [%d] %s", sample, Where(ctx.Eip).c_str());
				if (sample == 0)
				{
					DWORD ebp = ctx.Ebp;
					for (int f = 0; f < 12 && ebp; f++)
					{
						DWORD frame[2];
						SIZE_T got = 0;
						if (!ReadProcessMemory(proc, (void *)ebp, frame, sizeof(frame), &got) || got != sizeof(frame) || frame[0] <= ebp)
							break;
						printf("\n        <- %s", Where(frame[1]).c_str());
						ebp = frame[0];
					}
				}
				printf("\n");
			}
			ResumeThread(th);
			Sleep(150);
		}
		CloseHandle(th);
	}
	CloseHandle(proc);
	return 0;
}
