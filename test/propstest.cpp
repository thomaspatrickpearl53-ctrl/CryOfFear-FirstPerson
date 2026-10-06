// Offline test of the F8 > Models listing (src/fpprops.cpp).
// Usage: propstest.exe "<...\cryoffear>"
#include "../src/fpprops.cpp"
#include <stdarg.h>

cl_enginefunc_t *eng;
engine_studio_api_t g_studioEng;
static char g_dir[MAX_PATH];
void FpGameDir(char *out, size_t size) { _snprintf(out, size, "%s\\", g_dir); out[size - 1] = 0; }
void FpLog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	strncpy(g_dir, argv[1], sizeof(g_dir));
	int total = 0;
	for (int d = 0; d < FpProps_NumDirs(); d++)
	{
		total += FpProps_NumModels(d);
		printf("%-28s %3d   e.g. %s\n", FpProps_DirName(d), FpProps_NumModels(d), FpProps_ModelName(d, 0));
	}
	printf("%d folders, %d models\n", FpProps_NumDirs(), total);
	return 0;
}
