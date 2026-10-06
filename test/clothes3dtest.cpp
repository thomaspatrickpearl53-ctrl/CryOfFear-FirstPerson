// Offline test of the Clothes 3D preview (src/fpclothes.cpp): renders Simon in a
// few costumes into a hidden OpenGL window and saves the pixels as .bmp files.
// Usage: clothes3dtest.exe "<...\cryoffear>" <out prefix> costume [costume ...]
#include "../src/fpclothes.cpp"
#include <stdarg.h>

static char g_dir[MAX_PATH];
void FpGameDir(char *out, size_t size) { _snprintf(out, size, "%s\\", g_dir); out[size - 1] = 0; }
void FpLog(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
bool FpGL_Init(void) { return false; }
void FpGL_Use(GLuint) {}
void FpGL_ActiveTexture(int) {}

static void SaveBmp(const char *path, int w, int h, const std::vector<byte> &rgb)
{
	FILE *f = fopen(path, "wb");
	int row = (w * 3 + 3) & ~3, size = 54 + row * h;
	byte hdr[54] = { 'B', 'M' };
	*(int *)&hdr[2] = size; *(int *)&hdr[10] = 54; *(int *)&hdr[14] = 40;
	*(int *)&hdr[18] = w; *(int *)&hdr[22] = h; *(short *)&hdr[26] = 1; *(short *)&hdr[28] = 24;
	fwrite(hdr, 1, 54, f);
	std::vector<byte> line(row);
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			line[x * 3 + 0] = rgb[(y * w + x) * 3 + 2];
			line[x * 3 + 1] = rgb[(y * w + x) * 3 + 1];
			line[x * 3 + 2] = rgb[(y * w + x) * 3 + 0];
		}
		fwrite(line.data(), 1, row, f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 4) return 2;
	strncpy(g_dir, argv[1], sizeof(g_dir));
	const int W = 320, H = 400;
	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "c3d";
	RegisterClassA(&wc);
	HWND wnd = CreateWindowA("c3d", "c3d", WS_OVERLAPPEDWINDOW, 0, 0, W + 50, H + 50, NULL, NULL, wc.hInstance, NULL);
	HDC dc = GetDC(wnd);
	PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	pfd.cDepthBits = 24;
	SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
	HGLRC rc = wglCreateContext(dc);
	wglMakeCurrent(dc, rc);
	for (int a = 3; a < argc; a++)
	{
		int costume = atoi(argv[a]);
		glViewport(0, 0, W, H);
		glClearColor(0.12f, 0.12f, 0.13f, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		FpClothes_Draw3D(costume, 0, 0, W, H, W, H, 1.3f + a * 0.9f);
		glFinish();
		std::vector<byte> rgb(W * H * 3);
		glReadBuffer(GL_BACK);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
		char path[MAX_PATH];
		_snprintf(path, sizeof(path), "%s_%d.bmp", argv[2], costume);
		SaveBmp(path, W, H, rgb);
		printf("costume %d -> %s (glError %d)\n", costume, path, glGetError());
	}
	return 0;
}
