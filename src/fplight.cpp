// Realistic flashlight: a projected spotlight that replaces Half-Life's round
// flashlight blob.
//
//   FpLight_ProcessPlayerState  (HUD_ProcessPlayerState) notices EF_DIMLIGHT on the
//                               local player and hides it from the engine, so the
//                               engine's blob isn't drawn.
//   FpLight_Update              (V_CalcRefdef) places the light in the player's hand,
//                               lets it lag the aim a little, and adds an entity
//                               light at the beam's hit point so monsters are lit.
//   FpLight_DrawWorld           (HUD_DrawNormalTriangles) re-draws the visible world
//                               and brush-entity surfaces additively with a GLSL
//                               spotlight: reflector pattern, distance falloff and
//                               N.L shading, depth-tested against the frame.
//
// The engine's OpenGL surface structures aren't in the SDK (com_model.h has the
// software renderer's), so the GL layout is declared here and checked against
// the loaded map before use. Anything unexpected disables the world lighting.

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cl_entity.h"
#include "com_model.h"
#include "cvardef.h"
#include "ref_params.h"
#include "r_efx.h"
#include "event_api.h"
#include "pm_defs.h"
#include "entity_state.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
void    FpLog(const char *fmt, ...);
bool    FpIsCoF(void);
bool    FpFlashlightOn(void);

// GL helpers shared with fppost.cpp
bool   FpGL_Init(void);
GLuint FpGL_Program(const char *vs, const char *fs, const char *name);
void   FpGL_Use(GLuint prog);
GLint  FpGL_Loc(GLuint prog, const char *name);
void   FpGL_Uniform1f(GLint loc, float v);
void   FpGL_Uniform1i(GLint loc, int v);
void   FpGL_Uniform3f(GLint loc, float a, float b, float c);
void   FpGL_UniformMatrix4(GLint loc, const float *m);
void   FpGL_ActiveTexture(int unit);

// ---------------------------------------------------------------------------
// Engine (GL renderer) world structures
// ---------------------------------------------------------------------------

#define GL_VERTEXSIZE 7

struct gl_poly_t
{
	gl_poly_t *next;
	gl_poly_t *chain;
	int        numverts;
	int        flags;
	float      verts[4][GL_VERTEXSIZE];   // variable sized: xyz, s, t, lightmap s, t
};

struct gl_texture_t
{
	char      name[16];
	unsigned  width, height;
	int       gl_texturenum;
	void     *texturechain;
	int       anim_total, anim_min, anim_max;
	gl_texture_t *anim_next;
	gl_texture_t *alternate_anims;
	unsigned  offsets[4];
	unsigned  paloffset;
};

struct gl_texinfo_t
{
	float         vecs[2][4];
	float         mipadjust;
	gl_texture_t *texture;
	int           flags;
};

struct gl_surface_t
{
	int           visframe;
	mplane_t     *plane;
	int           flags;
	int           firstedge;
	int           numedges;
	short         texturemins[2];
	short         extents[2];
	int           light_s, light_t;
	gl_poly_t    *polys;
	gl_surface_t *texturechain;
	gl_texinfo_t *texinfo;
	int           dlightframe;
	int           dlightbits;
	int           lightmaptexturenum;
	byte          styles[4];
	int           cached_light[4];
	int           cached_dlight;
	color24      *samples;
	void         *pdecals;
};

#define GLSURF_PLANEBACK  0x02
#define GLSURF_SKIP       (0x04 | 0x08 | 0x10 | 0x40 | 0x80)   // sky, sprite, turb, background, underwater

// ---------------------------------------------------------------------------
// Settings and state
// ---------------------------------------------------------------------------

static cvar_t *fl_enable, *fl_bright, *fl_range, *fl_fov, *fl_sway, *fl_color, *fl_models;
static cvar_t *fl_dust, *fl_aniso;
static float  s_eye[3], s_vright[3], s_vup[3];   // camera, for the dust billboards
static int    s_mapFrames;      // frames since the map loaded (texture filtering runs at set points)

static bool   s_on;             // flashlight switched on
static bool   s_engineFlash;    // EF_DIMLIGHT seen on the local player
static float  s_amount;         // 0..1 fade
static float  s_pos[3], s_dir[3], s_right[3], s_up[3];
static bool   s_haveDir;
static float  s_lastTime = -1.0f;

static GLuint s_prog, s_cookie;
static int    s_glState;        // 0 untried, 1 ready, -1 failed
static model_t *s_checkedWorld; // world model the surface layout was validated for
static bool   s_layoutOk;

void FpLight_Init(void)
{
	fl_enable = FpRegister("cl_fplight", FpIsCoF() ? "0" : "1", FCVAR_ARCHIVE);     // CoF has its own projected light
	fl_bright = FpRegister("cl_fplight_brightness", "1", FCVAR_ARCHIVE);
	fl_range  = FpRegister("cl_fplight_range", "900", FCVAR_ARCHIVE);
	fl_fov    = FpRegister("cl_fplight_fov", "50", FCVAR_ARCHIVE);
	fl_sway   = FpRegister("cl_fplight_sway", "1", FCVAR_ARCHIVE);
	fl_color  = FpRegister("cl_fplight_color", "1 0.94 0.82", FCVAR_ARCHIVE);
	fl_models = FpRegister("cl_fplight_models", "1", FCVAR_ARCHIVE);
	fl_dust   = FpRegister("cl_pp_dust", "1", FCVAR_ARCHIVE);          // dust specks in the flashlight beam
	fl_aniso  = FpRegister("cl_pp_aniso", "16", FCVAR_ARCHIVE);        // anisotropic filtering (0 = off)
}

static bool Replacing(void) { return fl_enable && fl_enable->value != 0.0f; }

bool FpLight_EngineFlash(void) { return s_engineFlash; }

void FpLight_NewMap(void)
{
	s_checkedWorld = NULL;
	s_layoutOk = false;
	s_engineFlash = false;
	s_haveDir = false;
	s_mapFrames = 0;    // texture filtering is applied shortly after the map loads, and again later
}

// Light position/direction in world space for the volumetric beam. Returns the fade amount.
float FpLight_Get(float *pos, float *dir, float *halfAngleCos)
{
	VectorCopy(s_pos, pos);
	VectorCopy(s_dir, dir);
	float fov = fl_fov ? fl_fov->value : 50.0f;
	if (fov < 10.0f) fov = 10.0f;
	if (fov > 120.0f) fov = 120.0f;
	*halfAngleCos = cosf(fov * 0.5f * (float)M_PI / 180.0f);
	return s_haveDir ? s_amount : 0.0f;
}

// ---------------------------------------------------------------------------
// Hiding the engine's blob
// ---------------------------------------------------------------------------

void FpLight_ProcessPlayerState(entity_state_t *dst, const entity_state_t *src)
{
	cl_entity_t *local = eng ? eng->GetLocalPlayer() : NULL;
	if (!local || dst->number != local->index)
		return;
	s_engineFlash = (src->effects & EF_DIMLIGHT) != 0;
	if (Replacing())
		dst->effects &= ~EF_DIMLIGHT;
}

// ---------------------------------------------------------------------------
// Per frame: place the light, light the models
// ---------------------------------------------------------------------------

static void Cross(const float *a, const float *b, float *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static void Normalize(float *v)
{
	float l = sqrtf(DotProduct(v, v));
	if (l > 0.0001f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

void FpLight_Update(ref_params_t *pp)
{
	float dt = (s_lastTime < 0.0f) ? 0.0f : pp->time - s_lastTime;
	if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
	s_lastTime = pp->time;

	s_on = FpFlashlightOn() || s_engineFlash;
	bool playerView = pp->viewentity == pp->playernum + 1 && pp->health > 0 && !pp->intermission;
	float target = (s_on && playerView) ? 1.0f : 0.0f;
	s_amount += (target - s_amount) * (dt > 0.0f ? 1.0f - expf(-14.0f * dt) : 1.0f);

	// Aim with a little lag, as if held in the hand.
	float sway = fl_sway ? fl_sway->value : 1.0f;
	if (!s_haveDir || sway <= 0.0f || dt == 0.0f)
	{
		VectorCopy(pp->forward, s_dir);
	}
	else
	{
		float k = 1.0f - expf(-dt * 14.0f / sway);
		for (int i = 0; i < 3; i++)
			s_dir[i] += (pp->forward[i] - s_dir[i]) * k;
	}
	Normalize(s_dir);

	// Basis around the beam (roll follows the view's right vector).
	Cross(s_dir, pp->up, s_right);
	Normalize(s_right);
	Cross(s_right, s_dir, s_up);
	Normalize(s_up);

	// Held low and to the right of the eyes.
	for (int i = 0; i < 3; i++)
		s_pos[i] = pp->vieworg[i] + pp->right[i] * 6.0f - pp->up[i] * 7.0f + pp->forward[i] * 2.0f;
	s_haveDir = true;
	VectorCopy(pp->vieworg, s_eye);
	VectorCopy(pp->right, s_vright);
	VectorCopy(pp->up, s_vup);

	if (!Replacing() || s_amount < 0.01f || !fl_models || fl_models->value == 0.0f)
		return;

	// Light the models where the beam lands.
	float range = fl_range->value;
	float end[3];
	for (int i = 0; i < 3; i++)
		end[i] = pp->vieworg[i] + s_dir[i] * range;
	pmtrace_t tr;
	memset(&tr, 0, sizeof(tr));
	tr.fraction = 1.0f;
	eng->pEventAPI->EV_SetUpPlayerPrediction(false, true);
	eng->pEventAPI->EV_PushPMStates();
	eng->pEventAPI->EV_SetSolidPlayers(-1);
	eng->pEventAPI->EV_SetTraceHull(2);
	eng->pEventAPI->EV_PlayerTrace(pp->vieworg, end, PM_STUDIO_BOX, -1, &tr);
	eng->pEventAPI->EV_PopPMStates();

	float dist = tr.fraction * range;
	dlight_t *dl = eng->pEfxAPI->CL_AllocElight(0x46504C);
	if (!dl)
		return;
	float back = dist * 0.15f < 24.0f ? dist * 0.15f : 24.0f;
	for (int i = 0; i < 3; i++)
		dl->origin[i] = tr.endpos[i] - s_dir[i] * back;
	float c[3] = { 1.0f, 0.94f, 0.82f };
	if (fl_color && fl_color->string)
		sscanf(fl_color->string, "%f %f %f", &c[0], &c[1], &c[2]);
	float fall = 1.0f / (1.0f + (dist / (range * 0.35f)) * (dist / (range * 0.35f)));
	float k = s_amount * fl_bright->value * fall * 200.0f;
	dl->color.r = (byte)fminf(255.0f, c[0] * k);
	dl->color.g = (byte)fminf(255.0f, c[1] * k);
	dl->color.b = (byte)fminf(255.0f, c[2] * k);
	dl->radius = 90.0f + dist * tanf(fl_fov->value * 0.5f * (float)M_PI / 180.0f);
	dl->die = eng->GetClientTime() + 0.05f;
	dl->decay = 0.0f;
}

// ---------------------------------------------------------------------------
// GL resources
// ---------------------------------------------------------------------------

static const char *VS_LIGHT =
	"#version 120\n"
	"uniform mat4 modelMat;\n"
	"varying vec3 wpos;\n"
	"varying vec3 wnorm;\n"
	"varying vec2 tc;\n"
	"void main() {\n"
	"  vec4 w = modelMat * gl_Vertex;\n"
	"  wpos = w.xyz;\n"
	"  wnorm = normalize(mat3(modelMat) * gl_Normal);\n"
	"  tc = gl_MultiTexCoord0.xy;\n"
	"  gl_Position = gl_ModelViewProjectionMatrix * w;\n"
	"}\n";

static const char *FS_LIGHT =
	"#version 120\n"
	"uniform sampler2D diffuse;\n"
	"uniform sampler2D cookie;\n"
	"uniform vec3 lpos;\n"
	"uniform vec3 ldir;\n"
	"uniform vec3 lright;\n"
	"uniform vec3 lup;\n"
	"uniform float tanHalf;\n"
	"uniform float range;\n"
	"uniform float intensity;\n"
	"uniform vec3 color;\n"
	"varying vec3 wpos;\n"
	"varying vec3 wnorm;\n"
	"varying vec2 tc;\n"
	"void main() {\n"
	"  vec3 L = wpos - lpos;\n"
	"  float z = dot(L, ldir);\n"
	"  if (z <= 1.0) discard;\n"
	"  vec2 p = vec2(dot(L, lright), dot(L, lup)) / (z * tanHalf);\n"
	"  if (abs(p.x) > 1.0 || abs(p.y) > 1.0) discard;\n"
	"  float dist = length(L);\n"
	"  if (dist > range) discard;\n"
	"  vec3 ck = texture2D(cookie, p * 0.5 + 0.5).rgb;\n"
	"  float ndl = max(dot(wnorm, -L / dist), 0.0);\n"
	"  float r = dist / (range * 0.35);\n"
	"  float att = (1.0 / (1.0 + r * r)) * (1.0 - smoothstep(range * 0.75, range, dist));\n"
	"  vec4 tex = texture2D(diffuse, tc);\n"
	"  if (tex.a < 0.5) discard;\n"
	"  gl_FragColor = vec4(tex.rgb * ck * color * (ndl * att * intensity), 1.0);\n"
	"}\n";

// A torch reflector pattern: hot core, a slightly darker ring, a wide soft spill
// and faint concentric rings from the reflector, with a little unevenness.
static void MakeCookie(void)
{
	const int N = 256;
	static unsigned char px[N * N * 4];
	unsigned int rng = 0xC0FFEEu;
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
		{
			float dx = (x + 0.5f) / N * 2.0f - 1.0f, dy = (y + 0.5f) / N * 2.0f - 1.0f;
			float r = sqrtf(dx * dx + dy * dy);
			float edge = r >= 1.0f ? 0.0f : 1.0f - powf(r, 6.0f);                 // soft outer edge
			float spill = 0.35f * edge;
			float body = 0.55f * (r < 0.55f ? 1.0f : expf(-(r - 0.55f) * (r - 0.55f) / 0.02f));
			float core = 0.65f * expf(-r * r / 0.025f);
			float dip = -0.08f * expf(-(r - 0.33f) * (r - 0.33f) / 0.002f);       // dark ring around the core
			float rings = 0.04f * sinf(r * 70.0f) * (r > 0.2f && r < 0.9f ? 1.0f : 0.0f);
			rng = rng * 1664525u + 1013904223u;
			float noise = ((rng >> 8) / 16777216.0f - 0.5f) * 0.03f;
			float v = (spill + body + core + dip + rings + noise) * edge;
			v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			unsigned char b = (unsigned char)(v * 255.0f);
			unsigned char *p = px + (y * N + x) * 4;
			p[0] = b; p[1] = b; p[2] = b; p[3] = 255;
		}
	glGenTextures(1, &s_cookie);
	glBindTexture(GL_TEXTURE_2D, s_cookie);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static bool EnsureGL(void)
{
	if (s_glState == 1) return true;
	if (s_glState == -1) return false;
	if (!FpGL_Init() || !(s_prog = FpGL_Program(VS_LIGHT, FS_LIGHT, "flashlight")))
	{
		s_glState = -1;
		FpLog("fplight: shader unavailable, projected flashlight disabled\n");
		return false;
	}
	MakeCookie();
	s_glState = 1;
	return true;
}

// ---------------------------------------------------------------------------
// Surface layout check (once per map). SEH guards against a wrong layout.
// ---------------------------------------------------------------------------

static bool PlausibleFloat(float f) { return f == f && f > -65536.0f && f < 65536.0f; }

static int CheckLayout(model_t *world)
{
	__try
	{
		gl_surface_t *surfs = (gl_surface_t *)world->surfaces;
		int n = world->nummodelsurfaces, good = 0, tested = 0;
		for (int i = 0; i < n && tested < 64; i++)
		{
			gl_surface_t *s = &surfs[world->firstmodelsurface + i];
			if (s->flags & GLSURF_SKIP)
				continue;
			tested++;
			if (!s->plane || !s->texinfo || !s->texinfo->texture || !s->polys)
				continue;
			gl_poly_t *p = s->polys;
			if (p->numverts < 3 || p->numverts > 64)
				continue;
			bool ok = true;
			for (int v = 0; v < p->numverts && ok; v++)
				for (int c = 0; c < 5; c++)
					ok = ok && PlausibleFloat(p->verts[v][c]);
			const char *name = s->texinfo->texture->name;
			ok = ok && name[0] >= 32 && name[0] < 127 && s->texinfo->texture->gl_texturenum > 0;
			// Vertex should lie on the surface plane.
			float d = DotProduct(p->verts[0], s->plane->normal) - s->plane->dist;
			ok = ok && fabsf(d) < 1.0f;
			if (ok) good++;
		}
		return tested > 0 && good * 10 >= tested * 9;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static GLint s_uModel, s_uLpos, s_uLdir, s_uLright, s_uLup, s_uTan, s_uRange, s_uIntensity, s_uColor, s_uDiffuse, s_uCookie;

static void DrawSurface(gl_surface_t *s, bool cull, int *lastTex)
{
	if (s->flags & GLSURF_SKIP)
		return;
	float n[3];
	VectorCopy(s->plane->normal, n);
	float dist = s->plane->dist;
	if (s->flags & GLSURF_PLANEBACK)
	{
		n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
		dist = -dist;
	}
	if (cull)
	{
		float d = DotProduct(s_pos, n) - dist;        // light in front of the surface?
		if (d <= 0.0f || d > fl_range->value)
			return;
	}
	int tex = s->texinfo->texture->gl_texturenum;
	if (tex != *lastTex)
	{
		glBindTexture(GL_TEXTURE_2D, tex);
		*lastTex = tex;
	}
	glNormal3fv(n);
	for (gl_poly_t *p = s->polys; p; p = p->next)
	{
		glBegin(GL_POLYGON);
		for (int v = 0; v < p->numverts; v++)
		{
			glTexCoord2f(p->verts[v][3], p->verts[v][4]);
			glVertex3fv(p->verts[v]);
		}
		glEnd();
	}
}

static void BuildEntityMatrix(cl_entity_t *e, float *m)
{
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(e->origin[0], e->origin[1], e->origin[2]);
	glRotatef(e->angles[1], 0, 0, 1);
	glRotatef(-e->angles[0], 0, 1, 0);
	glRotatef(e->angles[2], 1, 0, 0);
	glGetFloatv(GL_MODELVIEW_MATRIX, m);
	glPopMatrix();
}

static int DrawPass(model_t *world)
{
	__try
	{
		gl_surface_t *surfs = (gl_surface_t *)world->surfaces;
		int lastTex = -1;
		static const float ident[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
		FpGL_UniformMatrix4(s_uModel, ident);

		// World: only the surfaces the engine drew this frame (highest visframe).
		int first = world->firstmodelsurface, n = world->nummodelsurfaces, maxVis = 0x80000000;
		for (int i = 0; i < n; i++)
			if (surfs[first + i].visframe > maxVis)
				maxVis = surfs[first + i].visframe;
		for (int i = 0; i < n; i++)
			if (surfs[first + i].visframe == maxVis)
				DrawSurface(&surfs[first + i], true, &lastTex);

		// Doors, platforms and other brush entities in this frame's packet.
		cl_entity_t *local = eng->GetLocalPlayer();
		for (int idx = 1; idx < 2048; idx++)
		{
			cl_entity_t *e = eng->GetEntityByIndex(idx);
			if (!e)
				break;
			if (!e->model || e->model->type != mod_brush || e->model == world)
				continue;
			if (local && e->curstate.messagenum != local->curstate.messagenum)
				continue;
			if (e->curstate.rendermode != kRenderNormal || (e->curstate.effects & EF_NODRAW))
				continue;
			float d[3];
			VectorSubtract(e->origin, s_pos, d);
			float reach = fl_range->value + e->model->radius;
			if (DotProduct(d, d) > reach * reach)
				continue;
			float m[16];
			BuildEntityMatrix(e, m);
			FpGL_UniformMatrix4(s_uModel, m);
			for (int i = 0; i < e->model->nummodelsurfaces; i++)
				DrawSurface(&surfs[e->model->firstmodelsurface + i], false, &lastTex);
		}
		return 1;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

void FpLight_DrawWorld(void)
{
	if (!Replacing() || s_amount < 0.01f || !s_haveDir || !eng)
		return;
	cl_entity_t *worldEnt = eng->GetEntityByIndex(0);
	model_t *world = worldEnt ? worldEnt->model : NULL;
	if (!world || world->type != mod_brush)
		return;
	if (world != s_checkedWorld)
	{
		s_checkedWorld = world;
		s_layoutOk = CheckLayout(world) != 0;
		FpLog("fplight: world surface layout %s (%d surfaces)\n", s_layoutOk ? "ok" : "NOT recognised - projected light off", world->nummodelsurfaces);
	}
	if (!s_layoutOk || !EnsureGL())
		return;

	float c[3] = { 1.0f, 0.94f, 0.82f };
	if (fl_color && fl_color->string)
		sscanf(fl_color->string, "%f %f %f", &c[0], &c[1], &c[2]);
	float fov = fl_fov->value < 10.0f ? 10.0f : (fl_fov->value > 120.0f ? 120.0f : fl_fov->value);

	GLint prevProg = 0;
	glGetIntegerv(0x8B8D, &prevProg);   // GL_CURRENT_PROGRAM
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glDisable(0x8804);                   // GL_FRAGMENT_PROGRAM_ARB
	glDisable(0x8620);                   // GL_VERTEX_PROGRAM_ARB
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -2.0f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glColor4f(1, 1, 1, 1);

	FpGL_Use(s_prog);
	if (!s_uModel)
	{
		s_uModel = FpGL_Loc(s_prog, "modelMat");   s_uLpos = FpGL_Loc(s_prog, "lpos");
		s_uLdir = FpGL_Loc(s_prog, "ldir");        s_uLright = FpGL_Loc(s_prog, "lright");
		s_uLup = FpGL_Loc(s_prog, "lup");          s_uTan = FpGL_Loc(s_prog, "tanHalf");
		s_uRange = FpGL_Loc(s_prog, "range");      s_uIntensity = FpGL_Loc(s_prog, "intensity");
		s_uColor = FpGL_Loc(s_prog, "color");      s_uDiffuse = FpGL_Loc(s_prog, "diffuse");
		s_uCookie = FpGL_Loc(s_prog, "cookie");
	}
	FpGL_Uniform3f(s_uLpos, s_pos[0], s_pos[1], s_pos[2]);
	FpGL_Uniform3f(s_uLdir, s_dir[0], s_dir[1], s_dir[2]);
	FpGL_Uniform3f(s_uLright, s_right[0], s_right[1], s_right[2]);
	FpGL_Uniform3f(s_uLup, s_up[0], s_up[1], s_up[2]);
	FpGL_Uniform1f(s_uTan, tanf(fov * 0.5f * (float)M_PI / 180.0f));
	FpGL_Uniform1f(s_uRange, fl_range->value);
	FpGL_Uniform1f(s_uIntensity, s_amount * fl_bright->value * 1.6f);
	FpGL_Uniform3f(s_uColor, c[0], c[1], c[2]);
	FpGL_Uniform1i(s_uDiffuse, 0);
	FpGL_Uniform1i(s_uCookie, 1);
	FpGL_ActiveTexture(1);
	glBindTexture(GL_TEXTURE_2D, s_cookie);
	FpGL_ActiveTexture(0);

	if (!DrawPass(world))
	{
		s_layoutOk = false;
		FpLog("fplight: error while drawing - projected light off for this map\n");
	}

	FpGL_Use((GLuint)prevProg);
	glPopAttrib();
}

// ---------------------------------------------------------------------------
// Dust in the beam: specks floating in a box around the camera, drawn as small
// additive billboards in the 3D pass (depth-tested), lit only inside the beam.
// ---------------------------------------------------------------------------

#define DUST_COUNT 600
#define DUST_BOX   360.0f      // side of the cube around the camera that holds the dust

struct Mote { float pos[3], vel[3], phase, size; };
static Mote   s_motes[DUST_COUNT];
static bool   s_motesInit;
static GLuint s_dotTex;
static float  s_dustLast = -1.0f;

static float Rand01(unsigned int &r) { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; }

static void InitMotes(void)
{
	unsigned int r = 0x5EEDu;
	for (int i = 0; i < DUST_COUNT; i++)
	{
		Mote &m = s_motes[i];
		for (int k = 0; k < 3; k++)
		{
			m.pos[k] = s_eye[k] + (Rand01(r) - 0.5f) * DUST_BOX;
			m.vel[k] = (Rand01(r) - 0.5f) * 3.0f;
		}
		m.vel[2] -= 0.6f;                         // dust settles slowly
		m.phase = Rand01(r) * 6.2831853f;
		m.size = 0.18f + Rand01(r) * Rand01(r) * 0.5f;
	}
	s_motesInit = true;
}

static void MakeDotTexture(void)
{
	const int N = 32;
	static unsigned char px[N * N * 4];
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++)
		{
			float dx = (x + 0.5f) / N * 2.0f - 1.0f, dy = (y + 0.5f) / N * 2.0f - 1.0f;
			float v = expf(-(dx * dx + dy * dy) * 5.0f);
			unsigned char b = (unsigned char)(v * 255.0f);
			unsigned char *p = px + (y * N + x) * 4;
			p[0] = p[1] = p[2] = b;
			p[3] = 255;
		}
	glGenTextures(1, &s_dotTex);
	glBindTexture(GL_TEXTURE_2D, s_dotTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

// ---------------------------------------------------------------------------
// Sharper textures: anisotropic + trilinear filtering on every mipmapped 2D
// texture. Uses GL directly (no engine structures), so it works in any GoldSrc
// game. Runs shortly after a map loads and again once its models have loaded.
// ---------------------------------------------------------------------------

#define GL_TEXTURE_MAX_ANISOTROPY      0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY  0x84FF
#define GL_TEXTURE_BINDING_2D_         0x8069

static void ApplyAnisotropy(void)
{
	float want = fl_aniso ? fl_aniso->value : 0.0f;
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	if (want < 1.0f || !ext || !strstr(ext, "texture_filter_anisotropic"))
		return;
	float maxAniso = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
	if (want > maxAniso) want = maxAniso;

	GLint prevTex = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D_, &prevTex);
	while (glGetError() != GL_NO_ERROR) {}
	int changed = 0;
	for (GLuint id = 1; id < 16384; id++)
	{
		if (!glIsTexture(id))
			continue;
		glBindTexture(GL_TEXTURE_2D, id);
		if (glGetError() != GL_NO_ERROR)
			continue;                                     // not a 2D texture
		GLint w1 = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &w1);
		if (w1 <= 0)
			continue;                                     // no mipmaps: render targets, HUD, lightmaps
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, want);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		changed++;
	}
	glBindTexture(GL_TEXTURE_2D, prevTex);
	while (glGetError() != GL_NO_ERROR) {}
	static int logged;
	if (logged++ < 6)
		FpLog("fplight: %gx anisotropic filtering on %d textures\n", want, changed);
}

// Called from HUD_DrawTransparentTriangles, while the 3D view is set up.
void FpLight_Draw3D(void)
{
	if (!eng)
		return;
	s_mapFrames++;
	if (s_mapFrames == 30 || s_mapFrames == 600)
		ApplyAnisotropy();

	float now = eng->GetClientTime();
	float dt = (s_dustLast < 0.0f) ? 0.0f : now - s_dustLast;
	if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
	s_dustLast = now;

	float amount = fl_dust ? fl_dust->value : 0.0f;
	if (amount <= 0.0f || s_amount < 0.01f || !s_haveDir)
		return;
	if (!FpGL_Init())
		return;
	if (!s_motesInit)
		InitMotes();
	if (!s_dotTex)
		MakeDotTexture();

	float fov = fl_fov->value < 10.0f ? 10.0f : (fl_fov->value > 120.0f ? 120.0f : fl_fov->value);
	float outer = cosf(fov * 0.5f * (float)M_PI / 180.0f), inner = outer + (1.0f - outer) * 0.6f;
	float range = 700.0f;
	float c[3] = { 1.0f, 0.94f, 0.82f };
	if (fl_color && fl_color->string)
		sscanf(fl_color->string, "%f %f %f", &c[0], &c[1], &c[2]);

	GLint prevProg = 0;
	glGetIntegerv(0x8B8D, &prevProg);   // GL_CURRENT_PROGRAM
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	FpGL_Use(0);
	glDisable(0x8804);                   // GL_FRAGMENT_PROGRAM_ARB
	glDisable(0x8620);                   // GL_VERTEX_PROGRAM_ARB
	for (int u = 3; u >= 0; u--)
	{
		FpGL_ActiveTexture(u);
		glDisable(GL_TEXTURE_2D);
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_dotTex);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_FOG);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	glBegin(GL_QUADS);
	for (int i = 0; i < DUST_COUNT; i++)
	{
		Mote &m = s_motes[i];
		// Drift, and wrap around the camera so there is always dust nearby.
		for (int k = 0; k < 3; k++)
		{
			m.pos[k] += (m.vel[k] + sinf(now * 0.3f + m.phase + k) * 0.8f) * dt;
			float d = m.pos[k] - s_eye[k];
			if (d > DUST_BOX * 0.5f) m.pos[k] -= DUST_BOX;
			else if (d < -DUST_BOX * 0.5f) m.pos[k] += DUST_BOX;
		}
		// Light it only inside the beam.
		float L[3] = { m.pos[0] - s_pos[0], m.pos[1] - s_pos[1], m.pos[2] - s_pos[2] };
		float dist = sqrtf(DotProduct(L, L));
		if (dist < 8.0f || dist > range)
			continue;
		float cs = DotProduct(L, s_dir) / dist;
		if (cs <= outer)
			continue;
		float t = (cs - outer) / (inner - outer);
		float cone = t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
		float r = dist / (range * 0.35f);
		float att = 1.0f / (1.0f + r * r);
		float twinkle = 0.55f + 0.45f * sinf(now * 2.3f + m.phase * 3.0f);
		float b = cone * att * twinkle * s_amount * amount * 0.9f;
		if (b < 0.01f)
			continue;
		glColor3f(c[0] * b, c[1] * b, c[2] * b);
		float s = m.size;
		float rx = s_vright[0] * s, ry = s_vright[1] * s, rz = s_vright[2] * s;
		float ux = s_vup[0] * s, uy = s_vup[1] * s, uz = s_vup[2] * s;
		glTexCoord2f(0, 0); glVertex3f(m.pos[0] - rx - ux, m.pos[1] - ry - uy, m.pos[2] - rz - uz);
		glTexCoord2f(1, 0); glVertex3f(m.pos[0] + rx - ux, m.pos[1] + ry - uy, m.pos[2] + rz - uz);
		glTexCoord2f(1, 1); glVertex3f(m.pos[0] + rx + ux, m.pos[1] + ry + uy, m.pos[2] + rz + uz);
		glTexCoord2f(0, 1); glVertex3f(m.pos[0] - rx + ux, m.pos[1] - ry + uy, m.pos[2] - rz + uz);
	}
	glEnd();

	FpGL_Use((GLuint)prevProg);
	glPopAttrib();
}

// Used by test\pptest.cpp to compile the shader outside the game.
bool FpLight_ShaderTest(void) { return EnsureGL(); }
