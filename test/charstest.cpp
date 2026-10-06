// Offline test of F8 > Player (src/fpchars.cpp): lists characters, picks a
// few (copying from Half-Life like the menu does) and builds the versions with
// Simon's animations.
// Usage: charstest.exe "<...\cryoffear>" "<...\Half-Life\valve>" rel [rel ...]
//        rel = cryoffear-relative model path, or "valve:<rel>" for a Half-Life model
#include "../src/fpchars.cpp"
#include <stdarg.h>

static char g_dir[MAX_PATH], g_hl[MAX_PATH];
void FpGameDir(char *out, size_t size) { _snprintf(out, size, "%s\\", g_dir); out[size - 1] = 0; }
void FpLog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
cvar_t *FpRegister(const char *, const char *, int) { return NULL; }
int FpMaps_NumTabs(void) { return 2; }
const char *FpMaps_TabDir(int tab) { return tab == 1 ? g_hl : NULL; }
const char *FpMaps_TabFolder(int tab) { return tab == 1 ? "valve" : NULL; }
static int ClientCmd(char *s) { printf("  [cmd] %s", s); return 0; }
static cl_enginefunc_t g_eng;
cl_enginefunc_t *eng = &g_eng;

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) return 2;
	strncpy(g_dir, argv[1], sizeof(g_dir));
	strncpy(g_hl, argv[2], sizeof(g_hl));
	g_eng.pfnClientCmd = ClientCmd;
	printf("Cry of Fear characters: %d, Half-Life: %d\n", FpChars_Count(0), FpChars_Count(1));
	for (int a = 3; a < argc; a++)
	{
		std::string rel = argv[a];
		if (rel.compare(0, 6, "valve:") == 0)
		{
			std::string want = rel.substr(6);
			rel = "";
			for (int i = 0; i < FpChars_Count(1); i++)
				if (!_stricmp(s_tabs[1][i].rel.c_str(), want.c_str())) rel = FpChars_Pick(1, i);
			if (rel.empty()) { printf("%s not listed\n", want.c_str()); continue; }
		}
		std::string out = FpChars_Prepare(rel.c_str());
		printf("%s -> %s\n", rel.c_str(), out.c_str());
	}
	return 0;
}
