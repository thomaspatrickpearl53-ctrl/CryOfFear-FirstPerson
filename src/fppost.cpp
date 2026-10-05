// Screen-space post-processing for Cry of Fear: SSAO, depth fog, motion blur,
// depth of field, light shafts, bloom and sharpening.
//
// FpPost_Capture3D  (after HUD_DrawTransparentTriangles) grabs the camera
//                    matrices and depth range while the 3D view is still set up.
// FpPost_Render     (before HUD_Redraw) copies the finished 3D frame (colour +
//                    depth), runs the GLSL passes and draws the result back, so
//                    the HUD is drawn on top untouched.
//
// The weapon is told apart by its compressed depth range (GoldSrc draws the
// viewmodel into the first 30% of the depth range) and is kept out of AO, fog,
// blur and DoF. The sky (depth at the far plane) is kept out of AO and fog.

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mathlib.h"
#include "hud_iface.h"
#include "cvardef.h"
#include "cl_entity.h"

extern cl_enginefunc_t *eng;
cvar_t *FpRegister(const char *name, const char *value, int flags);
float FpCam_AimWeight(void);
int   FpCam_Health(void);
bool  FpFlashlightOn(void);
bool  FpLight_EngineFlash(void);
float FpLight_Get(float *pos, float *dir, float *halfAngleCos);
void  FpLog(const char *fmt, ...);

// ---------------------------------------------------------------------------
// GL 2.0 / FBO entry points (fetched at runtime; the game only links GL 1.1)
// ---------------------------------------------------------------------------

#define GL_FRAGMENT_SHADER          0x8B30
#define GL_VERTEX_SHADER            0x8B31
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_CURRENT_PROGRAM          0x8B8D
#define GL_TEXTURE0                 0x84C0
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_DEPTH_COMPONENT24        0x81A6
#define GL_TEXTURE_COMPARE_MODE     0x884C
#define GL_DEPTH_TEXTURE_MODE       0x884B
#define GL_FRAMEBUFFER              0x8D40
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5
#define GL_FRAMEBUFFER_BINDING      0x8CA6
#define GL_FRAGMENT_PROGRAM_ARB     0x8804
#define GL_VERTEX_PROGRAM_ARB       0x8620
#define GL_TEXTURE_RECTANGLE        0x84F5
#define GL_PIXEL_PACK_BUFFER        0x88EB
#define GL_STREAM_READ              0x88E1
#define GL_READ_ONLY                0x88B8
#define GL_DEPTH_STENCIL            0x84F9
#define GL_UNSIGNED_INT_24_8        0x84FA
#define GL_DEPTH24_STENCIL8         0x88F0
#define GL_TIME_ELAPSED             0x88BF
#define GL_QUERY_RESULT             0x8866
#define GL_QUERY_RESULT_AVAILABLE   0x8867

typedef char GLchar;
typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void   (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const GLchar **, const GLint *);
typedef void   (APIENTRY *PFN_CompileShader)(GLuint);
typedef void   (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (APIENTRY *PFN_CreateProgram)(void);
typedef void   (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void   (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void   (APIENTRY *PFN_UseProgram)(GLuint);
typedef GLint  (APIENTRY *PFN_GetUniformLocation)(GLuint, const GLchar *);
typedef void   (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void   (APIENTRY *PFN_Uniform1f)(GLint, GLfloat);
typedef void   (APIENTRY *PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PFN_UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
typedef void   (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef void   (APIENTRY *PFN_GenFramebuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_DeleteFramebuffers)(GLsizei, const GLuint *);
typedef void   (APIENTRY *PFN_BindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CheckFramebufferStatus)(GLenum);
typedef void   (APIENTRY *PFN_GenBuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_BufferData)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void * (APIENTRY *PFN_MapBuffer)(GLenum, GLenum);
typedef GLboolean (APIENTRY *PFN_UnmapBuffer)(GLenum);
typedef void   (APIENTRY *PFN_GenQueries)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_BeginQuery)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_EndQuery)(GLenum);
typedef void   (APIENTRY *PFN_GetQueryObjectiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PFN_GetQueryObjectui64v)(GLuint, GLenum, unsigned __int64 *);

static PFN_CreateShader          qglCreateShader;
static PFN_ShaderSource          qglShaderSource;
static PFN_CompileShader         qglCompileShader;
static PFN_GetShaderiv           qglGetShaderiv;
static PFN_GetShaderInfoLog      qglGetShaderInfoLog;
static PFN_CreateProgram         qglCreateProgram;
static PFN_AttachShader          qglAttachShader;
static PFN_LinkProgram           qglLinkProgram;
static PFN_GetProgramiv          qglGetProgramiv;
static PFN_GetProgramInfoLog     qglGetProgramInfoLog;
static PFN_UseProgram            qglUseProgram;
static PFN_GetUniformLocation    qglGetUniformLocation;
static PFN_Uniform1i             qglUniform1i;
static PFN_Uniform1f             qglUniform1f;
static PFN_Uniform2f             qglUniform2f;
static PFN_Uniform3f             qglUniform3f;
static PFN_Uniform4f             qglUniform4f;
static PFN_UniformMatrix4fv      qglUniformMatrix4fv;
static PFN_ActiveTexture         qglActiveTexture;
static PFN_GenFramebuffers       qglGenFramebuffers;
static PFN_DeleteFramebuffers    qglDeleteFramebuffers;
static PFN_BindFramebuffer       qglBindFramebuffer;
static PFN_FramebufferTexture2D  qglFramebufferTexture2D;
static PFN_CheckFramebufferStatus qglCheckFramebufferStatus;
static PFN_GenBuffers            qglGenBuffers;
static PFN_BindBuffer            qglBindBuffer;
static PFN_BufferData            qglBufferData;
static PFN_MapBuffer             qglMapBuffer;
static PFN_UnmapBuffer           qglUnmapBuffer;
static PFN_GenQueries            qglGenQueries;         // optional (timing only)
static PFN_BeginQuery            qglBeginQuery;
static PFN_EndQuery              qglEndQuery;
static PFN_GetQueryObjectiv      qglGetQueryObjectiv;
static PFN_GetQueryObjectui64v   qglGetQueryObjectui64v;

static void *GetGL(const char *name)
{
	void *p = (void *)wglGetProcAddress(name);
	if (!p)
	{
		char ext[64];
		_snprintf(ext, sizeof(ext), "%sEXT", name);
		p = (void *)wglGetProcAddress(ext);
	}
	if (!p)
	{
		char arb[64];
		_snprintf(arb, sizeof(arb), "%sARB", name);
		p = (void *)wglGetProcAddress(arb);
	}
	return p;
}

static bool LoadGL(void)
{
#define LOAD(var, name) var = (decltype(var))GetGL(name); if (!var) { FpLog("fppost: missing %s\n", name); return false; }
	LOAD(qglCreateShader, "glCreateShader");
	LOAD(qglShaderSource, "glShaderSource");
	LOAD(qglCompileShader, "glCompileShader");
	LOAD(qglGetShaderiv, "glGetShaderiv");
	LOAD(qglGetShaderInfoLog, "glGetShaderInfoLog");
	LOAD(qglCreateProgram, "glCreateProgram");
	LOAD(qglAttachShader, "glAttachShader");
	LOAD(qglLinkProgram, "glLinkProgram");
	LOAD(qglGetProgramiv, "glGetProgramiv");
	LOAD(qglGetProgramInfoLog, "glGetProgramInfoLog");
	LOAD(qglUseProgram, "glUseProgram");
	LOAD(qglGetUniformLocation, "glGetUniformLocation");
	LOAD(qglUniform1i, "glUniform1i");
	LOAD(qglUniform1f, "glUniform1f");
	LOAD(qglUniform2f, "glUniform2f");
	LOAD(qglUniform3f, "glUniform3f");
	LOAD(qglUniform4f, "glUniform4f");
	LOAD(qglUniformMatrix4fv, "glUniformMatrix4fv");
	LOAD(qglActiveTexture, "glActiveTexture");
	LOAD(qglGenFramebuffers, "glGenFramebuffers");
	LOAD(qglDeleteFramebuffers, "glDeleteFramebuffers");
	LOAD(qglBindFramebuffer, "glBindFramebuffer");
	LOAD(qglFramebufferTexture2D, "glFramebufferTexture2D");
	LOAD(qglCheckFramebufferStatus, "glCheckFramebufferStatus");
	LOAD(qglGenBuffers, "glGenBuffers");
	LOAD(qglBindBuffer, "glBindBuffer");
	LOAD(qglBufferData, "glBufferData");
	LOAD(qglMapBuffer, "glMapBuffer");
	LOAD(qglUnmapBuffer, "glUnmapBuffer");
#undef LOAD
	qglGenQueries          = (PFN_GenQueries)GetGL("glGenQueries");
	qglBeginQuery          = (PFN_BeginQuery)GetGL("glBeginQuery");
	qglEndQuery            = (PFN_EndQuery)GetGL("glEndQuery");
	qglGetQueryObjectiv    = (PFN_GetQueryObjectiv)GetGL("glGetQueryObjectiv");
	qglGetQueryObjectui64v = (PFN_GetQueryObjectui64v)GetGL("glGetQueryObjectui64v");
	return true;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

static const char *VS =
	"#version 120\n"
	"varying vec2 uv;\n"
	"void main() { uv = gl_MultiTexCoord0.xy; gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0); }\n";

// Shared depth helpers. proj = (P0, P5, P10, P14); range = depth range (near, far).
static const char *COMMON =
	"#version 120\n"
	"varying vec2 uv;\n"
	"uniform sampler2D depthTex;\n"
	"uniform vec4 proj;\n"
	"uniform vec2 range;\n"
	"float depthN(vec2 t) { return (texture2D(depthTex, t).r - range.x) / (range.y - range.x); }\n"
	"float linearize(float dn) { return proj.w / (dn * 2.0 - 1.0 + proj.z); }\n"
	"float linDepth(vec2 t) { return linearize(depthN(t)); }\n"
	"bool isWeapon(float dn) { return dn < 0.3; }\n"
	"bool isSky(float dn) { return dn > 0.99995; }\n"
	"vec3 viewPos(vec2 t) { float d = linDepth(t); return vec3((t.x * 2.0 - 1.0) / proj.x * d, (t.y * 2.0 - 1.0) / proj.y * d, -d); }\n"
	"float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n";

// Ambient occlusion (half resolution): 12 spiral samples around the pixel.
static const char *FS_SSAO =
	"uniform float radius;\n"
	"uniform float intensity;\n"
	"uniform vec3 upView;\n"
	"uniform float contactLen;\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  if (isWeapon(dn) || isSky(dn)) { gl_FragColor = vec4(1.0); return; }\n"
	"  vec3 P = viewPos(uv);\n"
	"  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
	"  if (dot(N, P) > 0.0) N = -N;\n"
	"  float d = -P.z;\n"
	"  float rs = min(radius * proj.x * 0.5 / d, 0.12);\n"
	"  float a0 = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;\n"
	"  float occ = 0.0;\n"
	"  for (int i = 0; i < 12; i++) {\n"
	"    float t = (float(i) + 0.5) / 12.0;\n"
	"    float a = a0 + float(i) * 2.3999632;\n"
	"    vec2 s = uv + vec2(cos(a), sin(a)) * rs * t;\n"
	"    vec3 v = viewPos(s) - P;\n"
	"    float l = length(v);\n"
	"    float fall = 1.0 - clamp(l / radius, 0.0, 1.0);\n"
	"    occ += max(dot(N, v / max(l, 0.001)) - 0.15, 0.0) * fall;\n"
	"  }\n"
	"  float ao = clamp(1.0 - occ / 12.0 * intensity * 2.2, 0.0, 1.0);\n"
	// Contact shadows: march a short ray toward the light above; anything in the
	// way (a table over the floor, a box against a wall) shadows the pixel.
	"  float contact = 1.0;\n"
	"  if (contactLen > 0.0 && dot(N, upView) > 0.5) {\n"
	"    for (int i = 1; i <= 10; i++) {\n"
	"      vec3 R = P + upView * (contactLen * float(i) / 10.0);\n"
	"      if (R.z > -1.0) break;\n"
	"      vec2 su = vec2(R.x * proj.x / -R.z, R.y * proj.y / -R.z) * 0.5 + 0.5;\n"
	"      if (su.x < 0.0 || su.x > 1.0 || su.y < 0.0 || su.y > 1.0) break;\n"
	"      float diff = -R.z - linDepth(su);\n"
	"      float bias = 1.0 + d * 0.004;\n"
	"      if (diff > bias && diff < 10.0 + bias) { contact = 0.35 + 0.65 * float(i - 1) / 10.0; break; }\n"
	"    }\n"
	"  }\n"
	"  gl_FragColor = vec4(ao, contact, 1.0, 1.0);\n"
	"}\n";

// Separable 9-tap gaussian; bilateral (depth-aware) when 'bilateral' is 1.
static const char *FS_BLUR =
	"uniform sampler2D src;\n"
	"uniform vec2 dir;\n"
	"uniform float bilateral;\n"
	"void main() {\n"
	"  float w[5]; w[0] = 0.2270270; w[1] = 0.1945946; w[2] = 0.1216216; w[3] = 0.0540541; w[4] = 0.0162162;\n"
	"  float d0 = bilateral > 0.5 ? linDepth(uv) : 0.0;\n"
	"  vec4 acc = texture2D(src, uv) * w[0];\n"
	"  float wsum = w[0];\n"
	"  for (int i = 1; i < 5; i++) {\n"
	"    for (int s = -1; s <= 1; s += 2) {\n"
	"      vec2 t = uv + dir * float(i * s);\n"
	"      float k = w[i];\n"
	"      if (bilateral > 0.5) k *= exp(-abs(linDepth(t) - d0) / (0.04 * d0 + 1.0));\n"
	"      acc += texture2D(src, t) * k;\n"
	"      wsum += k;\n"
	"    }\n"
	"  }\n"
	"  gl_FragColor = acc / wsum;\n"
	"}\n";

// Bright pass with a soft knee (quarter resolution, 4 taps).
static const char *FS_BRIGHT =
	"uniform sampler2D src;\n"
	"uniform vec2 texel;\n"
	"uniform float threshold;\n"
	"void main() {\n"
	"  vec3 c = (texture2D(src, uv + texel * vec2(-1.0, -1.0)).rgb + texture2D(src, uv + texel * vec2(1.0, -1.0)).rgb\n"
	"          + texture2D(src, uv + texel * vec2(-1.0, 1.0)).rgb + texture2D(src, uv + texel * vec2(1.0, 1.0)).rgb) * 0.25;\n"
	// Held items (phone screen, lantern) sit right in front of the camera: let them
	// glow a little, but not flood the screen with bloom and lens dirt.
	"  if (isWeapon(depthN(uv))) c *= 0.15;\n"
	"  float br = max(c.r, max(c.g, c.b));\n"
	"  float knee = threshold * 0.5;\n"
	"  float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);\n"
	"  soft = soft * soft / (4.0 * knee + 0.00001);\n"
	"  float k = max(soft, br - threshold) / max(br, 0.00001);\n"
	"  gl_FragColor = vec4(c * k, 1.0);\n"
	"}\n";

// 4-tap box downsample.
static const char *FS_DOWN =
	"uniform sampler2D src;\n"
	"uniform vec2 texel;\n"
	"void main() {\n"
	"  gl_FragColor = (texture2D(src, uv + texel * vec2(-0.5, -0.5)) + texture2D(src, uv + texel * vec2(0.5, -0.5))\n"
	"               + texture2D(src, uv + texel * vec2(-0.5, 0.5)) + texture2D(src, uv + texel * vec2(0.5, 0.5))) * 0.25;\n"
	"}\n";

// Light shafts: radial blur of the bright pass toward the brightest light on screen.
static const char *FS_SHAFTS =
	"uniform sampler2D src;\n"
	"uniform vec2 light;\n"
	"uniform float strength;\n"
	"void main() {\n"
	"  vec2 delta = (uv - light) * (0.85 / 32.0);\n"
	"  vec2 p = uv;\n"
	"  float decay = 1.0;\n"
	"  vec3 acc = vec3(0.0);\n"
	"  for (int i = 0; i < 32; i++) {\n"
	"    p -= delta;\n"
	"    acc += texture2D(src, p).rgb * decay;\n"
	"    decay *= 0.95;\n"
	"  }\n"
	"  gl_FragColor = vec4(acc / 32.0 * strength * 3.0, 1.0);\n"
	"}\n";

// Main pass: sharpen + ambient occlusion + depth fog.
static const char *FS_MAIN =
	"uniform sampler2D scene;\n"
	"uniform sampler2D aoTex;\n"
	"uniform sampler2D giTex;\n"
	"uniform sampler2D ssrTex;\n"
	"uniform sampler2D volTex;\n"
	"uniform sampler2D shTex;\n"
	"uniform float shStrength;\n"
	"uniform float contactStrength;\n"
	"uniform float giStrength;\n"
	"uniform float volStrength;\n"
	"uniform vec3 volColor;\n"
	"uniform vec2 texel;\n"
	"uniform float sharpen;\n"
	"uniform float aoStrength;\n"
	"uniform float fogDensity;\n"
	"uniform vec3 fogColor;\n"
	"uniform float debug;\n"
	"void main() {\n"
	"  vec3 c = texture2D(scene, uv).rgb;\n"
	"  vec3 n = texture2D(scene, uv + vec2(0.0, texel.y)).rgb;\n"
	"  vec3 s = texture2D(scene, uv - vec2(0.0, texel.y)).rgb;\n"
	"  vec3 e = texture2D(scene, uv + vec2(texel.x, 0.0)).rgb;\n"
	"  vec3 w = texture2D(scene, uv - vec2(texel.x, 0.0)).rgb;\n"
	"  vec3 mn = min(c, min(min(n, s), min(e, w)));\n"
	"  vec3 mx = max(c, max(max(n, s), max(e, w)));\n"
	"  c = clamp(c + (4.0 * c - n - s - e - w) * 0.25 * sharpen, mn, mx);\n"
	"  float dn = depthN(uv);\n"
	"  vec4 aoc = texture2D(aoTex, uv);\n"
	"  float ao = aoc.r;\n"
	"  vec3 gi = texture2D(giTex, uv).rgb;\n"
	"  vec4 refl = texture2D(ssrTex, uv);\n"
	"  vec3 vol = texture2D(volTex, uv).rgb * volStrength * volColor;\n"
	"  if (debug == 1.0) { gl_FragColor = vec4(vec3(ao), 1.0); return; }\n"
	"  if (debug == 6.0) { gl_FragColor = vec4(gi * giStrength, 1.0); return; }\n"
	"  if (debug == 7.0) { gl_FragColor = vec4(vol, 1.0); return; }\n"
	"  if (debug == 8.0) { gl_FragColor = vec4(refl.rgb * refl.a, 1.0); return; }\n"
	"  float sh = texture2D(shTex, uv).r;\n"
	"  if (debug == 10.0) { gl_FragColor = vec4(vec3(1.0 - sh * max(shStrength, 0.5)), 1.0); return; }\n"
	"  if (debug == 9.0) { gl_FragColor = vec4(vec3(mix(1.0, aoc.g, contactStrength)), 1.0); return; }\n"
	"  if (debug == 2.0) { gl_FragColor = vec4(vec3(fract(linearize(dn) / 512.0)), 1.0); return; }\n"
	"  if (!isWeapon(dn) && !isSky(dn)) {\n"
	"    c *= mix(1.0, ao, aoStrength) * mix(1.0, aoc.g, contactStrength) * (1.0 - clamp(sh * shStrength, 0.0, 0.85));\n"
	"    c += gi * giStrength * (0.3 + c);\n"
	"    c = mix(c, refl.rgb, refl.a);\n"
	"    float d = linearize(dn) * fogDensity;\n"
	"    c = mix(c, fogColor, 1.0 - exp(-d * d));\n"
	"  }\n"
	"  if (!isWeapon(dn)) c += vol;\n"
	"  gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

// Camera motion blur by reprojecting each pixel into the previous frame.
static const char *FS_MOTION =
	"uniform sampler2D src;\n"
	"uniform mat4 invVP;\n"
	"uniform mat4 prevVP;\n"
	"uniform float scale;\n"
	"uniform float maxLen;\n"
	"uniform float debug;\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  vec4 c0 = texture2D(src, uv);\n"
	"  if (isWeapon(dn)) { gl_FragColor = c0; return; }\n"
	"  vec4 world = invVP * vec4(uv * 2.0 - 1.0, dn * 2.0 - 1.0, 1.0);\n"
	"  world /= world.w;\n"
	"  vec4 prev = prevVP * world;\n"
	"  vec2 puv = prev.xy / prev.w * 0.5 + 0.5;\n"
	"  vec2 vel = (uv - puv) * scale;\n"
	"  float l = length(vel);\n"
	"  if (l > maxLen) vel *= maxLen / l;\n"
	"  if (debug == 3.0) { gl_FragColor = vec4(abs(vel) * 20.0, 0.0, 1.0); return; }\n"
	// 12 samples with a per-pixel offset: fixed sample spacing showed up as stepped
	// ghost copies on fast turns.
	"  float jit = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
	"  vec4 acc = c0; float n = 1.0;\n"
	"  for (int i = 0; i < 12; i++) {\n"
	"    vec2 t = uv + vel * ((float(i) + jit) / 12.0 - 0.5);\n"
	"    if (isWeapon(depthN(t))) continue;\n"
	"    acc += texture2D(src, t); n += 1.0;\n"
	"  }\n"
	"  gl_FragColor = acc / n;\n"
	"}\n";

// Depth of field: background blur while aiming, slight blur in the far distance.
static const char *FS_DOF =
	"uniform sampler2D src;\n"
	"uniform vec2 texel;\n"
	"uniform float focus;\n"
	"uniform float aim;\n"
	"uniform float farAmt;\n"
	"uniform float maxRadius;\n"
	"uniform float debug;\n"
	"float coc(float dn) {\n"
	"  if (isWeapon(dn)) return 0.0;\n"
	"  float d = linearize(dn);\n"
	"  float a = aim * clamp((d - focus) / max(d, 1.0) * 2.5, 0.0, 1.0);\n"
	"  float f = farAmt * smoothstep(700.0, 3000.0, d);\n"
	"  return max(a, f) * maxRadius;\n"
	"}\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  float r = coc(dn);\n"
	"  if (debug == 4.0) { gl_FragColor = vec4(vec3(r / max(maxRadius, 1.0)), 1.0); return; }\n"
	"  vec4 c0 = texture2D(src, uv);\n"
	"  if (r < 0.5) { gl_FragColor = c0; return; }\n"
	"  float d0 = linearize(dn);\n"
	"  vec4 acc = c0; float wsum = 1.0;\n"
	"  for (int i = 0; i < 16; i++) {\n"
	"    float t = sqrt((float(i) + 0.5) / 16.0);\n"
	"    float a = float(i) * 2.3999632;\n"
	"    vec2 s = uv + vec2(cos(a), sin(a)) * t * r * texel;\n"
	"    float sdn = depthN(s);\n"
	"    if (isWeapon(sdn)) continue;\n"
	"    float k = linearize(sdn) >= d0 * 0.85 ? 1.0 : 0.15;\n"
	"    acc += texture2D(src, s) * k; wsum += k;\n"
	"  }\n"
	"  gl_FragColor = acc / wsum;\n"
	"}\n";

// One-bounce indirect light (half res): colour from nearby surfaces that face this pixel.
static const char *FS_GI =
	"uniform sampler2D scene;\n"
	"uniform float radius;\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  if (isWeapon(dn) || isSky(dn)) { gl_FragColor = vec4(0.0); return; }\n"
	"  vec3 P = viewPos(uv);\n"
	"  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
	"  if (dot(N, P) > 0.0) N = -N;\n"
	"  float rs = min(radius * proj.x * 0.5 / -P.z, 0.2);\n"
	"  float a0 = fract(sin(dot(gl_FragCoord.xy, vec2(41.31, 17.97))) * 24634.6345) * 6.2831853;\n"
	"  vec3 acc = vec3(0.0);\n"
	"  for (int i = 0; i < 8; i++) {\n"
	"    float a = a0 + float(i) * 2.3999632;\n"
	"    vec2 s = uv + vec2(cos(a), sin(a)) * rs * sqrt((float(i) + 0.5) / 8.0);\n"
	"    if (isWeapon(depthN(s))) continue;\n"
	"    vec3 v = viewPos(s) - P;\n"
	"    float l = length(v);\n"
	"    if (l < 1.0) continue;\n"
	"    float fall = 1.0 - clamp(l / radius, 0.0, 1.0);\n"
	"    acc += texture2D(scene, s).rgb * max(dot(N, v / l), 0.0) * fall;\n"
	"  }\n"
	"  gl_FragColor = vec4(acc / 8.0 * 2.0, 1.0);\n"
	"}\n";

// Volumetric flashlight (half res): march each view ray up to the surface it hits
// and add the light scattered by dust inside the beam's cone.
static const char *FS_VOL =
	"uniform vec3 lightPos;\n"
	"uniform vec3 lightDir;\n"
	"uniform float coneCos;\n"
	"uniform float beamRange;\n"
	"uniform float time;\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  if (isWeapon(dn)) { gl_FragColor = vec4(0.0); return; }\n"
	"  float d = isSky(dn) ? beamRange : min(linearize(dn), beamRange);\n"
	"  vec3 ray = normalize(vec3((uv.x * 2.0 - 1.0) / proj.x, (uv.y * 2.0 - 1.0) / proj.y, -1.0));\n"
	"  float tmax = min(d / max(-ray.z, 0.05), beamRange * 1.5);\n"
	"  float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
	"  float stepLen = tmax / 18.0;\n"
	"  float inner = mix(coneCos, 1.0, 0.6);\n"
	"  float acc = 0.0;\n"
	"  for (int i = 0; i < 18; i++) {\n"
	"    vec3 p = ray * ((float(i) + jitter) * stepLen);\n"
	"    vec3 L = p - lightPos;\n"
	"    float ld = length(L);\n"
	"    float cone = smoothstep(coneCos, inner, dot(L / ld, lightDir));\n"
	"    float att = smoothstep(0.0, 24.0, ld) / (1.0 + ld * ld / (beamRange * beamRange * 0.15));\n"
	"    float dust = 0.75 + 0.25 * sin(p.x * 0.05 + time) * sin(p.y * 0.07 - time * 0.7) * sin(p.z * 0.06 + time * 0.3);\n"
	"    acc += cone * att * dust * stepLen;\n"
	"  }\n"
	"  gl_FragColor = vec4(vec3(acc * 0.006), 1.0);\n"
	"}\n";

// Wet floors (half res): screen-space reflections on upward-facing surfaces,
// in puddle patches fixed in the world.
static const char *FS_SSR =
	"uniform sampler2D scene;\n"
	"uniform vec3 upView;\n"
	"uniform mat4 invMV;\n"
	"uniform float wet;\n"
	"uniform float puddles;\n"
	"float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
	"float vnoise(vec2 p) {\n"
	"  vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
	"  return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);\n"
	"}\n"
	"vec2 toUV(vec3 q) { return vec2(q.x * proj.x / -q.z, q.y * proj.y / -q.z) * 0.5 + 0.5; }\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  if (isWeapon(dn) || isSky(dn)) { gl_FragColor = vec4(0.0); return; }\n"
	"  vec3 P = viewPos(uv);\n"
	"  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
	"  if (dot(N, P) > 0.0) N = -N;\n"
	"  float floorK = smoothstep(0.85, 0.95, dot(N, upView));\n"
	"  if (floorK <= 0.0) { gl_FragColor = vec4(0.0); return; }\n"
	"  vec3 W = (invMV * vec4(P, 1.0)).xyz;\n"
	"  float m = vnoise(W.xy / 96.0) * 0.65 + vnoise(W.xy / 31.0) * 0.35;\n"
	"  float mask = mix(1.0, smoothstep(0.48, 0.62, m), puddles);\n"
	"  if (mask <= 0.0) { gl_FragColor = vec4(0.0); return; }\n"
	"  vec3 V = normalize(P);\n"
	"  vec3 R = normalize(reflect(V, N));\n"
	"  vec3 q = P; vec3 prev = P;\n"
	"  float stepL = 6.0;\n"
	"  bool hit = false; float steps = 0.0;\n"
	"  for (int i = 0; i < 24; i++) {\n"
	"    prev = q; q += R * stepL; stepL *= 1.15; steps += 1.0;\n"
	"    if (q.z > -2.0) break;\n"
	"    vec2 su = toUV(q);\n"
	"    if (su.x < 0.0 || su.x > 1.0 || su.y < 0.0 || su.y > 1.0) break;\n"
	"    float diff = -q.z - linDepth(su);\n"
	"    if (diff > 0.0 && diff < 4.0 + stepL * 0.5) { hit = true; break; }\n"
	"  }\n"
	"  if (!hit) { gl_FragColor = vec4(0.0); return; }\n"
	"  for (int j = 0; j < 4; j++) {\n"
	"    vec3 mid = (prev + q) * 0.5;\n"
	"    if (-mid.z - linDepth(toUV(mid)) > 0.0) q = mid; else prev = mid;\n"
	"  }\n"
	"  vec2 hu = toUV(q);\n"
	"  if (isWeapon(depthN(hu))) { gl_FragColor = vec4(0.0); return; }\n"
	"  float edge = smoothstep(0.0, 0.12, min(min(hu.x, 1.0 - hu.x), min(hu.y, 1.0 - hu.y)));\n"
	"  float fres = 0.2 + 0.8 * pow(1.0 - max(dot(-V, N), 0.0), 4.0);\n"
	"  float a = wet * mask * floorK * edge * fres * (1.0 - steps / 25.0);\n"
	"  gl_FragColor = vec4(texture2D(scene, hu).rgb, clamp(a, 0.0, 1.0));\n"
	"}\n";

// Flashlight shadows (half res): from each pixel the torch reaches, march through
// the depth buffer toward the torch; anything in the way blocks the light.
// Output: how much torch light this pixel loses (0..1).
static const char *FS_FLSHADOW =
	"uniform vec3 lightPos;\n"
	"uniform vec3 lightDir;\n"
	"uniform float coneCos;\n"
	"uniform float beamRange;\n"
	"vec2 toUV(vec3 q) { return vec2(q.x * proj.x / -q.z, q.y * proj.y / -q.z) * 0.5 + 0.5; }\n"
	"void main() {\n"
	"  float dn = depthN(uv);\n"
	"  if (isWeapon(dn) || isSky(dn)) { gl_FragColor = vec4(0.0); return; }\n"
	"  vec3 P = viewPos(uv);\n"
	"  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
	"  if (dot(N, P) > 0.0) N = -N;\n"
	"  vec3 L = lightPos - P;\n"
	"  float dist = length(L);\n"
	"  vec3 Ld = L / dist;\n"
	"  float cone = smoothstep(coneCos, mix(coneCos, 1.0, 0.6), dot(-Ld, lightDir));\n"
	"  float r = dist / (beamRange * 0.35);\n"
	"  float lit = cone * max(dot(N, Ld), 0.0) / (1.0 + r * r);\n"
	"  if (lit < 0.02) { gl_FragColor = vec4(0.0); return; }\n"
	"  float jit = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
	"  vec3 start = P + N * (1.0 + dist * 0.004);\n"
	"  float maxT = dist - 10.0;\n"
	"  float occ = 0.0;\n"
	"  for (int i = 0; i < 20; i++) {\n"
	"    float t = maxT * pow((float(i) + jit) / 20.0, 1.5);\n"
	"    vec3 q = start + Ld * t;\n"
	"    if (q.z > -6.0) break;\n"
	"    vec2 su = toUV(q);\n"
	"    if (su.x < 0.0 || su.x > 1.0 || su.y < 0.0 || su.y > 1.0) break;\n"
	"    float sdn = depthN(su);\n"
	"    if (isWeapon(sdn)) continue;\n"
	"    float diff = -q.z - linearize(sdn);\n"
	"    float bias = 0.6 + -q.z * 0.006;\n"
	"    if (diff > bias && diff < 28.0) { occ = 1.0; break; }\n"
	"  }\n"
	"  gl_FragColor = vec4(vec3(occ * lit), 1.0);\n"
	"}\n";

// Grade: bloom + shafts + lens dirt, eye adaptation, filmic tonemap, colour
// grade, chromatic aberration, vignette and the damage pulse. Writes luma to alpha.
static const char *FS_FINAL =
	"uniform sampler2D src;\n"
	"uniform sampler2D bloom1;\n"
	"uniform sampler2D bloom2;\n"
	"uniform sampler2D shafts;\n"
	"uniform sampler2D dirt;\n"
	"uniform float bloomAmt;\n"
	"uniform float shaftAmt;\n"
	"uniform float dirtAmt;\n"
	"uniform float exposure;\n"
	"uniform float tonemap;\n"
	"uniform float saturation;\n"
	"uniform float contrast;\n"
	"uniform vec3 tint;\n"
	"uniform float vignette;\n"
	"uniform float ca;\n"
	"uniform float hurt;\n"
	"uniform float debug;\n"
	// Shoulder-only filmic curve: leaves darks and mids untouched (Cry of Fear is
	// already dark) and rolls highlights off smoothly instead of clipping.
	"vec3 shoulder(vec3 x) { vec3 k = vec3(0.6); return mix(x, k + 0.4 * (1.0 - exp(-(x - k) / 0.4)), step(k, x)); }\n"
	"void main() {\n"
	"  vec2 d = uv - 0.5;\n"
	"  float r2 = dot(d, d);\n"
	"  vec2 off = d * r2 * (ca * 0.03 + hurt * 0.06);\n"
	"  vec3 c = vec3(texture2D(src, uv + off).r, texture2D(src, uv).g, texture2D(src, uv - off).b);\n"
	"  vec3 b = texture2D(bloom1, uv).rgb * 0.6 + texture2D(bloom2, uv).rgb * 0.8;\n"
	"  vec3 sh = texture2D(shafts, uv).rgb;\n"
	"  vec3 glow = b * bloomAmt + sh * shaftAmt;\n"
	"  if (debug == 5.0) { gl_FragColor = vec4(glow, 1.0); return; }\n"
	"  c += glow + b * texture2D(dirt, uv).rgb * dirtAmt * 2.0;\n"
	"  vec3 lin = pow(max(c, vec3(0.0)), vec3(2.2)) * exposure;\n"
	"  if (tonemap > 0.5) lin = shoulder(lin);\n"
	"  vec3 g = pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));\n"
	"  float l = lum(g);\n"
	"  g = mix(vec3(l), g, saturation);\n"
	"  g = (g - 0.5) * contrast + 0.5;\n"
	"  g *= mix(tint, vec3(1.0), smoothstep(0.0, 0.6, l));\n"
	"  float edge = smoothstep(0.1, 1.0, r2 * 2.0);\n"
	"  g *= 1.0 - (vignette + hurt * 0.45) * edge;\n"
	"  g = mix(g, g * vec3(1.25, 0.55, 0.55), clamp(hurt, 0.0, 1.0) * 0.6 * edge);\n"
	"  g = clamp(g, 0.0, 1.0);\n"
	"  gl_FragColor = vec4(g, dot(g, vec3(0.299, 0.587, 0.114)));\n"
	"}\n";

// FXAA (edge-directed blur on luma) followed by film grain, onto the screen.
static const char *FS_FXAA =
	"uniform sampler2D src;\n"
	"uniform vec2 texel;\n"
	"uniform float aa;\n"
	"uniform float grain;\n"
	"uniform float seed;\n"
	"void main() {\n"
	"  vec4 m = texture2D(src, uv);\n"
	"  vec3 c = m.rgb;\n"
	"  if (aa > 0.5) {\n"
	"    float lNW = texture2D(src, uv + vec2(-1.0, -1.0) * texel).a;\n"
	"    float lNE = texture2D(src, uv + vec2( 1.0, -1.0) * texel).a;\n"
	"    float lSW = texture2D(src, uv + vec2(-1.0,  1.0) * texel).a;\n"
	"    float lSE = texture2D(src, uv + vec2( 1.0,  1.0) * texel).a;\n"
	"    float lM = m.a;\n"
	"    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
	"    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
	"    if (lMax - lMin > max(0.0312, lMax * 0.125)) {\n"
	"      vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));\n"
	"      float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);\n"
	"      float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);\n"
	"      dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * texel;\n"
	"      vec3 a = 0.5 * (texture2D(src, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture2D(src, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
	"      vec3 bb = a * 0.5 + 0.25 * (texture2D(src, uv - dir * 0.5).rgb + texture2D(src, uv + dir * 0.5).rgb);\n"
	"      float lB = dot(bb, vec3(0.299, 0.587, 0.114));\n"
	"      c = (lB < lMin || lB > lMax) ? a : bb;\n"
	"    }\n"
	"  }\n"
	"  float n = fract(sin(dot(gl_FragCoord.xy + seed * vec2(37.0, 17.0), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;\n"
	"  c += n * grain * (1.0 - lum(c) * 0.6);\n"
	"  gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

enum { P_SSAO, P_BLUR, P_BRIGHT, P_DOWN, P_SHAFTS, P_MAIN, P_MOTION, P_DOF, P_FINAL, P_FXAA, P_GI, P_VOL, P_SSR, P_FLSHADOW, P_COUNT };
static const char *s_fragSrc[P_COUNT] = { FS_SSAO, FS_BLUR, FS_BRIGHT, FS_DOWN, FS_SHAFTS, FS_MAIN, FS_MOTION, FS_DOF, FS_FINAL, FS_FXAA, FS_GI, FS_VOL, FS_SSR, FS_FLSHADOW };
static const char *s_progName[P_COUNT] = { "ssao", "blur", "bright", "down", "shafts", "main", "motion", "dof", "grade", "fxaa", "gi", "volumetric", "ssr", "flashshadow" };
static GLuint s_prog[P_COUNT];

static GLuint Compile(GLenum type, const char *a, const char *b, const char *name)
{
	GLuint sh = qglCreateShader(type);
	const GLchar *srcs[2] = { a, b };
	qglShaderSource(sh, b ? 2 : 1, srcs, NULL);
	qglCompileShader(sh);
	GLint ok = 0;
	qglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048] = "";
		qglGetShaderInfoLog(sh, sizeof(log), NULL, log);
		FpLog("fppost: shader '%s' failed to compile:\n%s\n", name, log);
		return 0;
	}
	return sh;
}

static bool BuildPrograms(void)
{
	GLuint vs = Compile(GL_VERTEX_SHADER, VS, NULL, "vertex");
	if (!vs)
		return false;
	for (int i = 0; i < P_COUNT; i++)
	{
		GLuint fs = Compile(GL_FRAGMENT_SHADER, COMMON, s_fragSrc[i], s_progName[i]);
		if (!fs)
			return false;
		GLuint p = qglCreateProgram();
		qglAttachShader(p, vs);
		qglAttachShader(p, fs);
		qglLinkProgram(p);
		GLint ok = 0;
		qglGetProgramiv(p, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			char log[2048] = "";
			qglGetProgramInfoLog(p, sizeof(log), NULL, log);
			FpLog("fppost: program '%s' failed to link:\n%s\n", s_progName[i], log);
			return false;
		}
		s_prog[i] = p;
	}
	return true;
}

// ---------------------------------------------------------------------------
// GL helpers for the other modules (fplight.cpp)
// ---------------------------------------------------------------------------

bool FpGL_Init(void)
{
	static int state;   // 0 untried, 1 ok, -1 failed
	if (!state)
		state = LoadGL() ? 1 : -1;
	return state == 1;
}

GLuint FpGL_Program(const char *vsSrc, const char *fsSrc, const char *name)
{
	GLuint vs = Compile(GL_VERTEX_SHADER, vsSrc, NULL, name);
	GLuint fs = vs ? Compile(GL_FRAGMENT_SHADER, fsSrc, NULL, name) : 0;
	if (!fs)
		return 0;
	GLuint p = qglCreateProgram();
	qglAttachShader(p, vs);
	qglAttachShader(p, fs);
	qglLinkProgram(p);
	GLint ok = 0;
	qglGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[2048] = "";
		qglGetProgramInfoLog(p, sizeof(log), NULL, log);
		FpLog("fppost: program '%s' failed to link:\n%s\n", name, log);
		return 0;
	}
	return p;
}

void  FpGL_Use(GLuint prog)                          { qglUseProgram(prog); }
GLint FpGL_Loc(GLuint prog, const char *name)        { return qglGetUniformLocation(prog, name); }
void  FpGL_Uniform1f(GLint loc, float v)             { qglUniform1f(loc, v); }
void  FpGL_Uniform1i(GLint loc, int v)               { qglUniform1i(loc, v); }
void  FpGL_Uniform3f(GLint loc, float a, float b, float c) { qglUniform3f(loc, a, b, c); }
void  FpGL_UniformMatrix4(GLint loc, const float *m) { qglUniformMatrix4fv(loc, 1, GL_FALSE, m); }
void  FpGL_ActiveTexture(int unit)                   { qglActiveTexture(GL_TEXTURE0 + unit); }

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------

struct Target { GLuint tex, fbo; int w, h; };

static GLuint s_sceneTex, s_depthTex;
static Target s_full[2], s_aoA, s_aoB, s_q1, s_q2, s_e1, s_e2, s_shaft, s_tiny, s_lum;
static Target s_giA, s_giB, s_volA, s_volB, s_ssr, s_shA, s_shB;
static GLuint s_dirtTex, s_blackTex;
static int    s_w, s_h;

static GLuint MakeTex(int w, int h, GLenum internal, GLenum format, GLenum type)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
	return t;
}

static bool MakeTarget(Target &t, int w, int h)
{
	t.w = w > 1 ? w : 1;
	t.h = h > 1 ? h : 1;
	t.tex = MakeTex(t.w, t.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	qglGenFramebuffers(1, &t.fbo);
	qglBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
	return qglCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void FreeTarget(Target &t)
{
	if (t.fbo) qglDeleteFramebuffers(1, &t.fbo);
	if (t.tex) glDeleteTextures(1, &t.tex);
	memset(&t, 0, sizeof(t));
}

static bool CreateTargets(int w, int h, GLint restoreFbo)
{
	Target *all[] = { &s_full[0], &s_full[1], &s_aoA, &s_aoB, &s_q1, &s_q2, &s_e1, &s_e2, &s_shaft, &s_tiny, &s_lum,
		&s_giA, &s_giB, &s_volA, &s_volB, &s_ssr, &s_shA, &s_shB };
	for (Target *t : all) FreeTarget(*t);
	if (s_sceneTex) glDeleteTextures(1, &s_sceneTex);
	if (s_depthTex) glDeleteTextures(1, &s_depthTex);

	s_sceneTex = MakeTex(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	// Match the window's depth buffer format, so the copy is a straight GPU copy.
	GLint stencilBits = 0;
	glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
	if (stencilBits > 0)
		s_depthTex = MakeTex(w, h, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
	else
		s_depthTex = MakeTex(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
	glBindTexture(GL_TEXTURE_2D, s_depthTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);

	bool ok = MakeTarget(s_full[0], w, h) && MakeTarget(s_full[1], w, h)
		&& MakeTarget(s_aoA, w / 2, h / 2) && MakeTarget(s_aoB, w / 2, h / 2)
		&& MakeTarget(s_q1, w / 4, h / 4) && MakeTarget(s_q2, w / 4, h / 4)
		&& MakeTarget(s_e1, w / 8, h / 8) && MakeTarget(s_e2, w / 8, h / 8)
		&& MakeTarget(s_shaft, w / 4, h / 4) && MakeTarget(s_tiny, 32, 18) && MakeTarget(s_lum, 64, 36)
		&& MakeTarget(s_giA, w / 2, h / 2) && MakeTarget(s_giB, w / 2, h / 2)
		&& MakeTarget(s_volA, w / 4, h / 4) && MakeTarget(s_volB, w / 4, h / 4)
		&& MakeTarget(s_ssr, w / 2, h / 2)
		&& MakeTarget(s_shA, w / 2, h / 2) && MakeTarget(s_shB, w / 2, h / 2);
	qglBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
	s_w = w;
	s_h = h;
	return ok;
}

// ---------------------------------------------------------------------------
// Settings and frame state
// ---------------------------------------------------------------------------

static cvar_t *pp_enable, *pp_ssao, *pp_ssao_radius, *pp_bloom, *pp_bloom_thr, *pp_sharpen;
static cvar_t *pp_dof, *pp_dof_far, *pp_motion, *pp_fog, *pp_fog_color, *pp_shafts, *pp_debug, *pp_stats;
static cvar_t *pp_aa, *pp_tonemap, *pp_exposure, *pp_adapt, *pp_saturation, *pp_contrast, *pp_tint;
static cvar_t *pp_vignette, *pp_grain, *pp_ca, *pp_lensdirt, *pp_hurt;
static cvar_t *pp_flshadow;
static cvar_t *pp_vol, *pp_vol_always, *pp_gi, *pp_gi_radius, *pp_contact, *pp_contact_len, *pp_ssr, *pp_ssr_puddles;

void FpPost_Init(void)
{
	pp_enable      = FpRegister("cl_pp", "1", FCVAR_ARCHIVE);
	pp_ssao        = FpRegister("cl_pp_ssao", "0.6", FCVAR_ARCHIVE);        // AO strength
	pp_ssao_radius = FpRegister("cl_pp_ssao_radius", "28", FCVAR_ARCHIVE);  // world units
	pp_bloom       = FpRegister("cl_pp_bloom", "1", FCVAR_ARCHIVE);
	pp_bloom_thr   = FpRegister("cl_pp_bloom_threshold", "0.2", FCVAR_ARCHIVE);
	pp_sharpen     = FpRegister("cl_pp_sharpen", "0.4", FCVAR_ARCHIVE);
	pp_dof         = FpRegister("cl_pp_dof", "1", FCVAR_ARCHIVE);           // aiming DoF strength
	pp_dof_far     = FpRegister("cl_pp_dof_far", "0", FCVAR_ARCHIVE);    // distant blur
	pp_motion      = FpRegister("cl_pp_motionblur", "0.25", FCVAR_ARCHIVE);
	pp_fog         = FpRegister("cl_pp_fog", "0.4", FCVAR_ARCHIVE);         // density scale
	pp_fog_color   = FpRegister("cl_pp_fog_color", "0.07 0.075 0.08", FCVAR_ARCHIVE);
	pp_shafts      = FpRegister("cl_pp_shafts", "1", FCVAR_ARCHIVE);
	pp_debug       = FpRegister("cl_pp_debug", "0", 0); // 1 AO, 2 depth, 3 motion, 4 DoF, 5 bloom
	pp_stats       = FpRegister("cl_pp_stats", "1", FCVAR_ARCHIVE);     // log fps/cost every 10 s
	pp_aa          = FpRegister("cl_pp_aa", "1", FCVAR_ARCHIVE);         // FXAA
	pp_tonemap     = FpRegister("cl_pp_tonemap", "1", FCVAR_ARCHIVE);    // filmic curve
	pp_exposure    = FpRegister("cl_pp_exposure", "1", FCVAR_ARCHIVE);
	pp_adapt       = FpRegister("cl_pp_adapt", "1", FCVAR_ARCHIVE);      // eye adaptation strength
	pp_saturation  = FpRegister("cl_pp_saturation", "0.85", FCVAR_ARCHIVE);
	pp_contrast    = FpRegister("cl_pp_contrast", "1.05", FCVAR_ARCHIVE);
	pp_tint        = FpRegister("cl_pp_tint", "0.94 1.0 1.08", FCVAR_ARCHIVE); // shadow tint RGB
	pp_vignette    = FpRegister("cl_pp_vignette", "0.25", FCVAR_ARCHIVE);
	pp_grain       = FpRegister("cl_pp_grain", "0.04", FCVAR_ARCHIVE);
	pp_ca          = FpRegister("cl_pp_ca", "0.4", FCVAR_ARCHIVE);       // chromatic aberration
	pp_lensdirt    = FpRegister("cl_pp_lensdirt", "0.3", FCVAR_ARCHIVE);
	pp_hurt        = FpRegister("cl_pp_hurt", "1", FCVAR_ARCHIVE);       // damage / low health pulse
	pp_flshadow    = FpRegister("cl_pp_flashshadows", "0.8", FCVAR_ARCHIVE); // shadows cast by the flashlight
	pp_vol         = FpRegister("cl_pp_volumetric", "0.5", FCVAR_ARCHIVE);   // flashlight beam
	pp_vol_always  = FpRegister("cl_pp_volumetric_always", "0", FCVAR_ARCHIVE); // 1 = beam even if flashlight looks off
	pp_gi          = FpRegister("cl_pp_gi", "0.6", FCVAR_ARCHIVE);           // bounce light
	pp_gi_radius   = FpRegister("cl_pp_gi_radius", "64", FCVAR_ARCHIVE);
	pp_contact     = FpRegister("cl_pp_contact", "0.4", FCVAR_ARCHIVE);      // contact shadows
	pp_contact_len = FpRegister("cl_pp_contact_len", "14", FCVAR_ARCHIVE);
	pp_ssr         = FpRegister("cl_pp_ssr", "0", FCVAR_ARCHIVE);            // wet floor reflections (off: unreliable on CoF maps)
	pp_ssr_puddles = FpRegister("cl_pp_ssr_puddles", "1", FCVAR_ARCHIVE);    // 1 = puddle patches, 0 = whole floor
}

enum { S_UNTRIED, S_READY, S_FAILED };
static int    s_state = S_UNTRIED;
static bool   s_have3D;
static float  s_mv[16], s_proj[16], s_range[2];
static float  s_prevVP[16];
static bool   s_havePrev;
static float  s_focus = 200.0f;
static float  s_lightU = 0.5f, s_lightV = 0.5f, s_lightAmt;
static float  s_lastTime = -1.0f;
static float  s_exposure = 1.0f;
static float  s_flash;
static int    s_lastEffects = -1, s_effectsLogged;
static float  s_hurt;
static int    s_lastHealth = -1;

// Readbacks go through pixel buffers and are used 2 frames later, so the CPU
// never waits for the GPU.
#define RING 3
static GLuint s_pboDepth[RING], s_pboTiny[RING];
static bool   s_pboFilled[RING];
static int    s_frame;

static GLuint s_query[RING];
static bool   s_queryPending[RING];
static double s_gpuMsSum, s_cpuMsSum, s_frameSum;
static int    s_statFrames, s_gpuSamples;
static float  s_statNext;

#define LUM_W 64
#define LUM_H 36
static GLuint s_pboLum[RING];

// Procedural lens dirt: soft smudges and specks of different sizes.
static void CreateDirt(void)
{
	const int W = 256, H = 144;
	static float acc[W * H];
	static unsigned char px[W * H * 4];
	memset(acc, 0, sizeof(acc));
	unsigned int rng = 0x1234567u;
	auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
	for (int i = 0; i < 110; i++)
	{
		bool big = i < 10;
		float cx = rnd() * W, cy = rnd() * H;
		float r = big ? 25.0f + rnd() * 45.0f : 1.5f + rnd() * rnd() * 14.0f;
		float a = big ? 0.10f + rnd() * 0.12f : 0.15f + rnd() * 0.45f;
		int x0 = (int)(cx - r * 2), x1 = (int)(cx + r * 2), y0 = (int)(cy - r * 2), y1 = (int)(cy + r * 2);
		for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < H; y++)
			for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < W; x++)
			{
				float dx = (x - cx) / r, dy = (y - cy) / r;
				acc[y * W + x] += a * expf(-(dx * dx + dy * dy) * 1.5f);
			}
	}
	for (int i = 0; i < W * H; i++)
	{
		float v = acc[i] > 1.0f ? 1.0f : acc[i];
		px[i * 4 + 0] = (unsigned char)(v * 255.0f);
		px[i * 4 + 1] = (unsigned char)(v * 245.0f);
		px[i * 4 + 2] = (unsigned char)(v * 225.0f);
		px[i * 4 + 3] = 255;
	}
	s_dirtTex = MakeTex(W, H, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void CreateReadback(void)
{
	CreateDirt();
	static const unsigned char black[4] = { 0, 0, 0, 0 };
	s_blackTex = MakeTex(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
	qglGenBuffers(RING, s_pboLum);
	for (int i = 0; i < RING; i++)
	{
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboLum[i]);
		qglBufferData(GL_PIXEL_PACK_BUFFER, LUM_W * LUM_H * 4, NULL, GL_STREAM_READ);
	}
	qglGenBuffers(RING, s_pboDepth);
	qglGenBuffers(RING, s_pboTiny);
	for (int i = 0; i < RING; i++)
	{
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboDepth[i]);
		qglBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float), NULL, GL_STREAM_READ);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboTiny[i]);
		qglBufferData(GL_PIXEL_PACK_BUFFER, 32 * 18 * 4, NULL, GL_STREAM_READ);
	}
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	if (qglGenQueries && qglBeginQuery && qglEndQuery && qglGetQueryObjectiv && qglGetQueryObjectui64v)
		qglGenQueries(RING, s_query);
}

static void MatMul(const float *a, const float *b, float *out)  // column-major: out = a * b
{
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1]
			               + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

static bool MatInvert(const float *m, float *out)
{
	float inv[16];
	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf(det) < 1e-12f)
		return false;
	det = 1.0f / det;
	for (int i = 0; i < 16; i++)
		out[i] = inv[i] * det;
	return true;
}

// Called while the 3D view is set up (HUD_DrawTransparentTriangles).
void FpPost_Capture3D(void)
{
	glGetFloatv(GL_MODELVIEW_MATRIX, s_mv);
	glGetFloatv(GL_PROJECTION_MATRIX, s_proj);
	glGetFloatv(GL_DEPTH_RANGE, s_range);
	// Ignore odd passes (mirrors, 2D): a perspective projection has proj[11] == -1.
	if (s_proj[11] < -0.5f && s_range[0] != s_range[1])
		s_have3D = true;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

static GLuint s_cur;

static void Use(int p) { s_cur = s_prog[p]; qglUseProgram(s_cur); }
// Uniform locations are looked up once: every glGetUniformLocation is a driver
// round trip (and a stall under NVIDIA's threaded optimisation).
static GLint Loc(const char *n)
{
	static struct { GLuint prog; const char *name; GLint loc; } cache[256];
	static int count;
	for (int i = 0; i < count; i++)
		if (cache[i].prog == s_cur && cache[i].name == n)
			return cache[i].loc;
	GLint loc = qglGetUniformLocation(s_cur, n);
	if (count < 256)
	{
		cache[count].prog = s_cur;
		cache[count].name = n;
		cache[count].loc = loc;
		count++;
	}
	return loc;
}
static void U1f(const char *n, float v) { qglUniform1f(Loc(n), v); }
static void U2f(const char *n, float a, float b) { qglUniform2f(Loc(n), a, b); }

static void Bind(int unit, const char *name, GLuint tex)
{
	qglActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, tex);
	qglUniform1i(Loc(name), unit);
}

static void DepthUniforms(void)
{
	Bind(4, "depthTex", s_depthTex);
	qglUniform4f(Loc("proj"), s_proj[0], s_proj[5], s_proj[10], s_proj[14]);
	U2f("range", s_range[0], s_range[1]);
}

static void Quad(void)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
}

static void Into(const Target &t)
{
	qglBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
	glViewport(0, 0, t.w, t.h);
}

static void BlurPass(const Target &src, const Target &tmp, bool bilateral)
{
	Use(P_BLUR);
	DepthUniforms();
	U1f("bilateral", bilateral ? 1.0f : 0.0f);
	Into(tmp);
	Bind(0, "src", src.tex);
	U2f("dir", 1.0f / src.w, 0.0f);
	Quad();
	Into(src);
	Bind(0, "src", tmp.tex);
	U2f("dir", 0.0f, 1.0f / src.h);
	Quad();
}

static float CvarOr(cvar_t *c, float def) { return c ? c->value : def; }

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

void FpPost_Render(float time)
{
	float dt = (s_lastTime < 0.0f) ? 0.016f : time - s_lastTime;
	if (dt <= 0.0f || dt > 0.25f) dt = 0.016f;
	s_lastTime = time;

	bool want = s_have3D && pp_enable && pp_enable->value != 0.0f;
	s_have3D = false;
	if (!want || s_state == S_FAILED)
		return;

	if (s_state == S_UNTRIED)
	{
		FpLog("fppost: GL %s | %s | %s\n", (const char *)glGetString(GL_VERSION),
			(const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VENDOR));
		if (!FpGL_Init() || !BuildPrograms())
		{
			s_state = S_FAILED;
			FpLog("fppost: post-processing disabled\n");
			return;
		}
		CreateReadback();
		s_state = S_READY;
		FpLog("fppost: shaders ready\n");
	}

	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	int w = vp[2], h = vp[3];
	if (w < 64 || h < 64)
		return;

	LARGE_INTEGER t0, t1, freq;
	QueryPerformanceCounter(&t0);
	int slot = s_frame % RING;
	int old  = (s_frame + 1) % RING;     // written 2 frames ago
	s_frame++;
	bool stats = pp_stats && pp_stats->value != 0.0f && s_query[0];
	bool timeThis = stats && !s_queryPending[slot] && (s_frame % 10) == 0;
	if (timeThis)
		qglBeginQuery(GL_TIME_ELAPSED, s_query[slot]);

	GLint prevFbo = 0, prevProg = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);

	if (w != s_w || h != s_h)
	{
		if (!CreateTargets(w, h, prevFbo))
		{
			s_state = S_FAILED;
			FpLog("fppost: couldn't create render targets (%dx%d), post-processing disabled\n", w, h);
			return;
		}
		FpLog("fppost: render targets %dx%d\n", w, h);
	}

	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
	glMatrixMode(GL_TEXTURE);    glPushMatrix(); glLoadIdentity();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_FRAGMENT_PROGRAM_ARB);
	glDisable(GL_VERTEX_PROGRAM_ARB);
	glDisable(GL_TEXTURE_RECTANGLE);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glColor4f(1, 1, 1, 1);

	// Focus distance for DoF: the depth at the crosshair.
	float rawCenter = -1.0f;
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboDepth[slot]);
	glReadPixels(vp[0] + w / 2, vp[1] + h / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
	if (s_pboFilled[old])
	{
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboDepth[old]);
		float *p = (float *)qglMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
		if (p) { rawCenter = *p; qglUnmapBuffer(GL_PIXEL_PACK_BUFFER); }
	}
	qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	float cn = (rawCenter - s_range[0]) / (s_range[1] - s_range[0]);
	if (rawCenter >= 0.0f && cn >= 0.3f)
	{
		float d = s_proj[14] / (cn * 2.0f - 1.0f + s_proj[10]);
		if (d > 0.0f)
			s_focus += (fminf(d, 4000.0f) - s_focus) * (1.0f - expf(-6.0f * dt));
	}

	// Grab the finished 3D frame.
	qglActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, s_sceneTex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);
	glBindTexture(GL_TEXTURE_2D, s_depthTex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);

	float debug = CvarOr(pp_debug, 0.0f);

	// 1. Ambient occlusion (half res) + depth-aware blur.
	float aoAmt = CvarOr(pp_ssao, 0.8f);
	float contactAmt = CvarOr(pp_contact, 0.6f);
	bool aoPass = aoAmt > 0.0f || contactAmt > 0.0f || debug == 1.0f || debug == 9.0f;
	if (aoPass)
	{
		Use(P_SSAO);
		DepthUniforms();
		U1f("radius", CvarOr(pp_ssao_radius, 28.0f));
		U1f("intensity", 1.0f);
		qglUniform3f(Loc("upView"), s_mv[8], s_mv[9], s_mv[10]);
		U1f("contactLen", contactAmt > 0.0f || debug == 9.0f ? CvarOr(pp_contact_len, 14.0f) : 0.0f);
		Into(s_aoA);
		Quad();
		BlurPass(s_aoA, s_aoB, true);
	}

	// 2. Bloom: bright pass (1/4) -> blur, downsample (1/8) -> blur.
	Use(P_BRIGHT);
	DepthUniforms();
	Bind(0, "src", s_sceneTex);
	U2f("texel", 1.0f / w, 1.0f / h);
	U1f("threshold", CvarOr(pp_bloom_thr, 0.6f));
	Into(s_q1);
	Quad();

	// Scene brightness for eye adaptation: 64x36 copy read back 2 frames later.
	float adapt = CvarOr(pp_adapt, 1.0f);
	if (adapt > 0.0f)
	{
		Use(P_DOWN);
		DepthUniforms();
		Bind(0, "src", s_sceneTex);
		U2f("texel", 4.0f / w, 4.0f / h);
		Into(s_lum);
		Quad();
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboLum[slot]);
		glReadPixels(0, 0, LUM_W, LUM_H, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		if (s_pboFilled[old])
		{
			qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboLum[old]);
			const unsigned char *px = (const unsigned char *)qglMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
			if (px)
			{
				// Centre-weighted geometric mean of linear luminance.
				double logSum = 0.0, wSum = 0.0;
				for (int y = 0; y < LUM_H; y++)
					for (int x = 0; x < LUM_W; x++)
					{
						const unsigned char *p = px + (y * LUM_W + x) * 4;
						float l = (p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f) / 255.0f;
						l = powf(l, 2.2f);
						float dx = (x + 0.5f) / LUM_W - 0.5f, dy = (y + 0.5f) / LUM_H - 0.5f;
						float wgt = 1.0f - fminf(1.0f, (dx * dx + dy * dy) * 2.5f) * 0.7f;
						logSum += wgt * logf(l + 0.0005f);
						wSum += wgt;
					}
				qglUnmapBuffer(GL_PIXEL_PACK_BUFFER);
				float avg = expf((float)(logSum / wSum));
				// Dark scenes open up a little, bright scenes stop down; limited so horror stays dark.
				float target = powf(0.12f / fmaxf(avg, 0.001f), 0.4f);
				target = 1.0f + (fminf(fmaxf(target, 0.7f), 2.0f) - 1.0f) * fminf(adapt, 1.5f);
				// Eyes adapt to the dark slowly and to bright light quickly.
				float rate = target > s_exposure ? 0.8f : 3.0f;
				s_exposure += (target - s_exposure) * (1.0f - expf(-rate * dt));
			}
		}
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	else
		s_exposure = 1.0f;

	// Damage pulse and low-health edge darkening.
	int hp = FpCam_Health();
	if (hp >= 0 && s_lastHealth >= 0 && hp < s_lastHealth)
		s_hurt = fminf(1.0f, s_hurt + 0.4f + (s_lastHealth - hp) / 40.0f);
	s_lastHealth = hp;
	s_hurt *= expf(-2.2f * dt);
	float lowHp = (hp >= 0 && hp < 35) ? (1.0f - hp / 35.0f) * 0.35f : 0.0f;
	float hurt = fmaxf(s_hurt, lowHp) * CvarOr(pp_hurt, 1.0f);

	// Blur the bright pass first: shafts radiating from single bright pixels turn into thin lines.
	BlurPass(s_q1, s_q2, false);

	// 3. Light shafts: find the brightest spot in a tiny copy of the bright pass.
	float shaftAmt = CvarOr(pp_shafts, 0.5f);
	if (shaftAmt > 0.0f)
	{
		Use(P_DOWN);
		DepthUniforms();
		Bind(0, "src", s_q1.tex);
		U2f("texel", 1.0f / s_q1.w, 1.0f / s_q1.h);
		Into(s_tiny);
		Quad();
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboTiny[slot]);
		glReadPixels(0, 0, 32, 18, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		float best = 0.0f, bu = s_lightU, bv = s_lightV;
		if (s_pboFilled[old])
		{
			qglBindBuffer(GL_PIXEL_PACK_BUFFER, s_pboTiny[old]);
			const unsigned char *px = (const unsigned char *)qglMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
			if (px)
			{
				for (int y = 0; y < 18; y++)
					for (int x = 0; x < 32; x++)
					{
						const unsigned char *p = px + (y * 32 + x) * 4;
						float l = (p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f) / 255.0f;
						if (l > best) { best = l; bu = (x + 0.5f) / 32.0f; bv = (y + 0.5f) / 18.0f; }
					}
				qglUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
		}
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		float k = 1.0f - expf(-4.0f * dt);
		float jump = fabsf(bu - s_lightU) + fabsf(bv - s_lightV);
		if (jump > 0.25f)
			s_lightAmt *= 0.8f;          // fade out before moving to a new light
		if (jump <= 0.25f || s_lightAmt < 0.02f)
		{
			s_lightU += (bu - s_lightU) * k;
			s_lightV += (bv - s_lightV) * k;
		}
		float want = best < 0.3f ? 0.0f : fminf((best - 0.3f) / 0.4f, 1.0f);
		s_lightAmt += (want - s_lightAmt) * k;

		Use(P_SHAFTS);
		DepthUniforms();
		Bind(0, "src", s_q1.tex);
		U2f("light", s_lightU, s_lightV);
		U1f("strength", s_lightAmt);
		Into(s_shaft);
		Quad();
		BlurPass(s_shaft, s_q2, false);
	}

	Use(P_DOWN);
	DepthUniforms();
	Bind(0, "src", s_q1.tex);
	U2f("texel", 1.0f / s_q1.w, 1.0f / s_q1.h);
	Into(s_e1);
	Quad();
	BlurPass(s_e1, s_e2, false);

	// Camera-relative helpers for the new passes.
	float upView[3] = { s_mv[8], s_mv[9], s_mv[10] };           // world up in view space
	float invMV[16];
	bool haveInvMV = MatInvert(s_mv, invMV);

	// 4a. Bounce light (half res) + depth-aware blur.
	float giAmt = CvarOr(pp_gi, 0.6f);
	if (giAmt > 0.0f || debug == 6.0f)
	{
		Use(P_GI);
		DepthUniforms();
		Bind(0, "scene", s_sceneTex);
		U1f("radius", CvarOr(pp_gi_radius, 64.0f));
		Into(s_giA);
		Quad();
		BlurPass(s_giA, s_giB, true);
	}

	// 4b. Volumetric flashlight, while the flashlight is on (EF_DIMLIGHT on the player).
	cl_entity_t *local = eng ? eng->GetLocalPlayer() : NULL;
	int effects = local ? local->curstate.effects : 0;
	if (effects != s_lastEffects && s_effectsLogged < 12)
	{
		FpLog("fppost: player effects = 0x%x (flashlight %s)\n", effects, (effects & EF_DIMLIGHT) ? "on" : "off");
		s_effectsLogged++;
	}
	s_lastEffects = effects;
	bool flashOn = (effects & EF_DIMLIGHT) || FpFlashlightOn() || FpLight_EngineFlash() || CvarOr(pp_vol_always, 0.0f) != 0.0f;
	s_flash += ((flashOn ? 1.0f : 0.0f) - s_flash) * (1.0f - expf(-12.0f * dt));
	float volAmt = CvarOr(pp_vol, 0.5f) * s_flash;

	// The torch in view space: same hand position and swaying aim as fplight.cpp.
	float lp[3] = { 8.0f, -9.0f, -2.0f }, ld[3] = { -8.0f, 9.0f, -300.0f }, coneCos = 0.91f;
	{
		float wp[3], wd[3];
		FpLight_Get(wp, wd, &coneCos);
		if (wd[0] != 0.0f || wd[1] != 0.0f || wd[2] != 0.0f)
		{
			const float *m = s_mv;   // column-major
			for (int i = 0; i < 3; i++)
			{
				lp[i] = m[i] * wp[0] + m[4 + i] * wp[1] + m[8 + i] * wp[2] + m[12 + i];
				ld[i] = m[i] * wd[0] + m[4 + i] * wd[1] + m[8 + i] * wd[2];
			}
		}
	}
	float len = sqrtf(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]);

	// 4a2. Flashlight shadows (half res) + depth-aware blur for soft edges.
	float shAmt = CvarOr(pp_flshadow, 0.8f) * s_flash;
	bool shPass = shAmt > 0.01f || debug == 10.0f;
	if (shPass)
	{
		Use(P_FLSHADOW);
		DepthUniforms();
		qglUniform3f(Loc("lightPos"), lp[0], lp[1], lp[2]);
		qglUniform3f(Loc("lightDir"), ld[0] / len, ld[1] / len, ld[2] / len);
		U1f("coneCos", coneCos);
		U1f("beamRange", 700.0f);
		Into(s_shA);
		Quad();
		BlurPass(s_shA, s_shB, true);
	}

	if (volAmt > 0.01f || debug == 7.0f)
	{
		Use(P_VOL);
		DepthUniforms();
		qglUniform3f(Loc("lightPos"), lp[0], lp[1], lp[2]);
		qglUniform3f(Loc("lightDir"), ld[0] / len, ld[1] / len, ld[2] / len);
		U1f("coneCos", coneCos);
		U1f("beamRange", 700.0f);
		U1f("time", time);
		Into(s_volA);
		Quad();
		BlurPass(s_volA, s_volB, true);
	}

	// 4c. Wet floor reflections (half res).
	float ssrAmt = CvarOr(pp_ssr, 0.0f);
	if ((ssrAmt > 0.0f || debug == 8.0f) && haveInvMV)
	{
		Use(P_SSR);
		DepthUniforms();
		Bind(0, "scene", s_sceneTex);
		qglUniform3f(Loc("upView"), upView[0], upView[1], upView[2]);
		qglUniformMatrix4fv(Loc("invMV"), 1, GL_FALSE, invMV);
		U1f("wet", debug == 8.0f ? fmaxf(ssrAmt, 0.5f) : ssrAmt);
		U1f("puddles", CvarOr(pp_ssr_puddles, 1.0f));
		Into(s_ssr);
		Quad();
	}

	// 4. Main: sharpen + AO + contact + bounce + reflections + fog + beam -> full[0]
	float fogColor[3] = { 0.07f, 0.075f, 0.08f };
	if (pp_fog_color && pp_fog_color->string)
		sscanf(pp_fog_color->string, "%f %f %f", &fogColor[0], &fogColor[1], &fogColor[2]);
	Use(P_MAIN);
	DepthUniforms();
	Bind(0, "scene", s_sceneTex);
	Bind(1, "aoTex", aoPass ? s_aoA.tex : s_blackTex);
	Bind(2, "giTex", (giAmt > 0.0f || debug == 6.0f) ? s_giA.tex : s_blackTex);
	Bind(3, "ssrTex", ((ssrAmt > 0.0f || debug == 8.0f) && haveInvMV) ? s_ssr.tex : s_blackTex);
	Bind(5, "volTex", (volAmt > 0.01f || debug == 7.0f) ? s_volA.tex : s_blackTex);
	Bind(6, "shTex", shPass ? s_shA.tex : s_blackTex);
	U1f("shStrength", debug == 10.0f ? fmaxf(shAmt, 0.8f) : shAmt);
	U1f("contactStrength", aoPass ? fminf(contactAmt, 1.5f) : 0.0f);
	U1f("giStrength", giAmt);
	U1f("volStrength", debug == 7.0f ? fmaxf(volAmt, 0.5f) : volAmt);
	qglUniform3f(Loc("volColor"), 1.0f, 0.93f, 0.8f);
	U2f("texel", 1.0f / w, 1.0f / h);
	U1f("sharpen", CvarOr(pp_sharpen, 0.4f));
	U1f("aoStrength", (aoPass && aoAmt > 0.0f) ? fminf(aoAmt, 1.5f) : 0.0f);
	U1f("fogDensity", CvarOr(pp_fog, 0.6f) * 0.00035f);
	qglUniform3f(Loc("fogColor"), fogColor[0], fogColor[1], fogColor[2]);
	U1f("debug", debug);
	Into(s_full[0]);
	Quad();
	int cur = 0;

	// 5. Motion blur -> full[1]
	float vpNow[16], inv[16];
	MatMul(s_proj, s_mv, vpNow);
	float motion = CvarOr(pp_motion, 0.5f);
	if ((motion > 0.0f || debug == 3.0f) && s_havePrev && MatInvert(vpNow, inv))
	{
		Use(P_MOTION);
		DepthUniforms();
		Bind(0, "src", s_full[cur].tex);
		qglUniformMatrix4fv(Loc("invVP"), 1, GL_FALSE, inv);
		qglUniformMatrix4fv(Loc("prevVP"), 1, GL_FALSE, s_prevVP);
		// Normalise to a 1/60 s shutter so the amount doesn't depend on framerate.
		U1f("scale", motion * (1.0f / 60.0f) / dt);
		U1f("maxLen", 0.022f);   // cap: fast flicks shouldn't smear the whole screen
		U1f("debug", debug);
		Into(s_full[cur ^ 1]);
		Quad();
		cur ^= 1;
	}
	memcpy(s_prevVP, vpNow, sizeof(s_prevVP));
	s_havePrev = true;

	// 6. Depth of field -> other full target
	float dofAmt = CvarOr(pp_dof, 1.0f);
	float farAmt = CvarOr(pp_dof_far, 0.35f);
	float aim = FpCam_AimWeight() * dofAmt;
	if (aim > 0.01f || farAmt > 0.0f || debug == 4.0f)
	{
		Use(P_DOF);
		DepthUniforms();
		Bind(0, "src", s_full[cur].tex);
		U2f("texel", 1.0f / w, 1.0f / h);
		U1f("focus", s_focus);
		U1f("aim", fminf(aim, 1.5f));
		U1f("farAmt", farAmt);
		U1f("maxRadius", h / 180.0f);
		U1f("debug", debug);
		Into(s_full[cur ^ 1]);
		Quad();
		cur ^= 1;
	}

	// 7. Grade: bloom, shafts, lens dirt, exposure, tonemap, colour, CA, vignette -> full target
	float tint[3] = { 0.94f, 1.0f, 1.08f };
	if (pp_tint && pp_tint->string)
		sscanf(pp_tint->string, "%f %f %f", &tint[0], &tint[1], &tint[2]);
	Use(P_FINAL);
	DepthUniforms();
	Bind(0, "src", s_full[cur].tex);
	Bind(1, "bloom1", s_q1.tex);
	Bind(2, "bloom2", s_e1.tex);
	Bind(3, "shafts", s_shaft.tex);
	Bind(5, "dirt", s_dirtTex);
	U1f("bloomAmt", CvarOr(pp_bloom, 0.35f));
	U1f("shaftAmt", shaftAmt);
	U1f("dirtAmt", CvarOr(pp_lensdirt, 0.6f));
	U1f("exposure", s_exposure * CvarOr(pp_exposure, 1.0f));
	U1f("tonemap", CvarOr(pp_tonemap, 1.0f));
	U1f("saturation", CvarOr(pp_saturation, 0.85f));
	U1f("contrast", CvarOr(pp_contrast, 1.05f));
	qglUniform3f(Loc("tint"), tint[0], tint[1], tint[2]);
	U1f("vignette", CvarOr(pp_vignette, 0.35f));
	U1f("ca", CvarOr(pp_ca, 0.4f));
	U1f("hurt", hurt);
	U1f("debug", debug);
	Into(s_full[cur ^ 1]);
	Quad();
	cur ^= 1;

	// 8. Anti-aliasing + film grain onto the screen.
	qglBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	glViewport(vp[0], vp[1], w, h);
	Use(P_FXAA);
	DepthUniforms();
	Bind(0, "src", s_full[cur].tex);
	U2f("texel", 1.0f / w, 1.0f / h);
	U1f("aa", (CvarOr(pp_aa, 1.0f) != 0.0f && debug == 0.0f) ? 1.0f : 0.0f);
	U1f("grain", debug == 0.0f ? CvarOr(pp_grain, 0.04f) : 0.0f);
	U1f("seed", (float)(s_frame % 97));
	Quad();

	// Restore everything the game expects.
	qglUseProgram(prevProg);
	qglActiveTexture(GL_TEXTURE0);
	glMatrixMode(GL_TEXTURE);    glPopMatrix();
	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glPopAttrib();

	s_pboFilled[slot] = true;

	if (timeThis)
	{
		qglEndQuery(GL_TIME_ELAPSED);
		s_queryPending[slot] = true;
	}
	if (stats)
	{
		for (int i = 0; i < RING; i++)
		{
			if (!s_queryPending[i] || i == slot)
				continue;
			GLint ready = 0;
			qglGetQueryObjectiv(s_query[i], GL_QUERY_RESULT_AVAILABLE, &ready);
			if (!ready)
				continue;
			unsigned __int64 ns = 0;
			qglGetQueryObjectui64v(s_query[i], GL_QUERY_RESULT, &ns);
			s_queryPending[i] = false;
			s_gpuMsSum += ns / 1.0e6;
			s_gpuSamples++;
		}
		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&freq);
		s_cpuMsSum += (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
		s_frameSum += dt;
		s_statFrames++;
		if (time >= s_statNext)
		{
			if (s_statFrames > 30)
				FpLog("fppost: %.0f fps | effects GPU %.2f ms, CPU %.2f ms per frame\n",
					s_statFrames / s_frameSum, s_gpuSamples ? s_gpuMsSum / s_gpuSamples : -1.0,
					s_cpuMsSum / s_statFrames);
			s_gpuMsSum = s_cpuMsSum = s_frameSum = 0.0;
			s_statFrames = s_gpuSamples = 0;
			s_statNext = time + 10.0f;
		}
	}

	// Report the first GL error once (glDisable of an unsupported cap is expected and harmless).
	static int reported;
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR)
		if (reported < 3 && err != GL_INVALID_ENUM)
		{
			FpLog("fppost: GL error 0x%x\n", err);
			reported++;
		}
}
