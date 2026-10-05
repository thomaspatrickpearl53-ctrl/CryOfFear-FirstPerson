// Offline test: runs the fists model builder from src/fpfists.cpp without the game.
// Build: cl /EHsc /I<sdk dirs> test\fiststest.cpp   Run: fiststest.exe "<...>\cryoffear\"
#include "../src/fpfists.cpp"

#include <stdarg.h>

cl_enginefunc_t     *eng;
engine_studio_api_t  g_studioEng;
static char          g_dir[MAX_PATH];

cvar_t *FpRegister(const char *, const char *, int) { return NULL; }
void FpGameDir(char *out, size_t size) { strncpy(out, g_dir, size); }
void FpLog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2)
		return 2;
	_snprintf(g_dir, sizeof(g_dir), "%s\\", argv[1]);
	return BuildFists() ? 0 : 1;
}
