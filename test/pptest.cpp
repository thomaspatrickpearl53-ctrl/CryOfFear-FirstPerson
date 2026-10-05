// Offline check for fppost: creates a hidden GL window, compiles every shader and
// runs one full post-processing frame on a fake scene, reporting GL errors.
#include "../src/fppost.cpp"
bool FpIsCoF(void) { return false; }
bool FpLight_ShaderTest(void);
#include <stdarg.h>

cl_enginefunc_t *eng = NULL;
float FpCam_AimWeight(void) { return 1.0f; }
int FpCam_Health(void) { return 100; }
bool FpFlashlightOn(void) { return true; }
cvar_t *FpRegister(const char *, const char *, int) { return NULL; }
void FpLog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

int main()
{
	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = "pptest";
	RegisterClassA(&wc);
	HWND wnd = CreateWindowA("pptest", "pptest", WS_OVERLAPPEDWINDOW, 0, 0, 1920, 1080, NULL, NULL, wc.hInstance, NULL);
	HDC dc = GetDC(wnd);
	PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
	HGLRC rc = wglCreateContext(dc);
	wglMakeCurrent(dc, rc);

	// Fake 3D frame: a GoldSrc-style projection (fov 90, near 4, far 4096).
	glViewport(0, 0, 1920, 1080);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-4, 4, -2.25, 2.25, 4, 4096);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(-90, 1, 0, 0);
	glRotatef(90, 0, 0, 1);
	glClearColor(0.3f, 0.3f, 0.3f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glBegin(GL_TRIANGLES);
	glColor3f(1, 1, 1); glVertex3f(200, -100, -50); glVertex3f(200, 100, -50); glVertex3f(400, 0, 100);
	glEnd();

	cvar_t on = { (char *)"cl_pp", (char *)"1", 0, 1.0f, NULL };
	pp_enable = &on;
	pp_vol_always = &on;   // exercise the flashlight beam too

	LARGE_INTEGER f0, f1, fq; QueryPerformanceFrequency(&fq);
	for (int frame = 0; frame < 303; frame++)
	{
		if (frame == 3) { glFinish(); QueryPerformanceCounter(&f0); }
		FpPost_Capture3D();
		glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1920, 1080, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
		FpPost_Render(frame * 0.016f);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-4, 4, -2.25, 2.25, 4, 4096);
		glMatrixMode(GL_MODELVIEW);
	}
	glFinish(); QueryPerformanceCounter(&f1);
	printf("avg effects cost at 1920x1080: %.2f ms/frame\n",(f1.QuadPart - f0.QuadPart) * 1000.0 / fq.QuadPart / 300.0);
	printf("flashlight shader: %s\n", FpLight_ShaderTest() ? "ok" : "FAILED");
	printf("state=%s\n", s_state == S_READY ? "READY" : s_state == S_FAILED ? "FAILED" : "UNTRIED");

	unsigned char px[4];
	glReadPixels(640, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("center pixel after post: %d %d %d\n", px[0], px[1], px[2]);
	GLenum err = glGetError();
	printf("glGetError=0x%x\n", err);
	return s_state == S_READY ? 0 : 1;
}
