// Offline test of src/fpmaps.cpp: scans Steam for GoldSrc games and installs one
// map into a scratch "cryoffear" folder (never the real game).
// Usage: mapstest.exe <scratch cryoffear folder> <game name substring> <map>
#include "../src/fpmaps.cpp"
#include <stdarg.h>

static char g_dir[MAX_PATH];
void FpGameDir(char *out, size_t size) { _snprintf(out, size, "%s\\", g_dir); out[size - 1] = 0; }
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
	if (argc < 4) return 2;
	strncpy(g_dir, argv[1], sizeof(g_dir));
	CreateDirectoryA(g_dir, NULL);
	CreateDirectoryA((std::string(g_dir) + "\\maps").c_str(), NULL);
	FpMaps_Scan();
	int pick = -1;
	for (int i = 0; i < FpMaps_NumGames(); i++)
	{
		printf("  [%c] %s (%d maps)\n", FpMaps_GameOn(i) ? 'x' : ' ', FpMaps_GameName(i), FpMaps_GameMapCount(i));
		if (!strcmp(FpMaps_GameName(i), argv[2])) pick = i;
	}
	if (pick < 0) { printf("no game matching %s\n", argv[2]); return 1; }
	if (!FpMaps_GameOn(pick)) FpMaps_ToggleGame(pick);
	printf("tabs: %d (", FpMaps_NumTabs());
	for (int t = 0; t < FpMaps_NumTabs(); t++) printf("%s%s", t ? ", " : "", FpMaps_TabName(t));
	printf(")\n");
	int tab = -1;
	for (int t = 1; t < FpMaps_NumTabs(); t++)
		if (!strcmp(FpMaps_TabName(t), FpMaps_GameName(pick))) tab = t;
	for (int i = 0; i < FpMaps_Count(tab); i++)
		if (!_stricmp(FpMaps_Name(tab, i), argv[3]))
		{
			std::string cmd = FpMaps_Load(tab, i);
			printf("command: %s\n", cmd.c_str());
			return 0;
		}
	printf("map %s not found in %s\n", argv[3], FpMaps_GameName(pick));
	return 1;
}
