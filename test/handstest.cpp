// Offline test of F8 > Player > Hands (src/fphands.cpp): builds the arm-swapped
// copies of some weapon models.
// Usage: handstest.exe "<...\cryoffear>" set weapon [weapon ...]   (set: 1 doctor, 2 sick)
#include "../src/fphands.cpp"
#include <stdarg.h>

cl_enginefunc_t *eng;
engine_studio_api_t g_studioEng;
static char g_dir[MAX_PATH];
void FpGameDir(char *out, size_t size) { _snprintf(out, size, "%s\\", g_dir); out[size - 1] = 0; }
void FpLog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
cvar_t *FpRegister(const char *, const char *, int) { return NULL; }

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 4) return 2;
	strncpy(g_dir, argv[1], sizeof(g_dir));
	int set = atoi(argv[2]);
	for (int a = 3; a < argc; a++)
		printf("%s -> %s\n", argv[a], Build(argv[a], set).c_str());
	return 0;
}
