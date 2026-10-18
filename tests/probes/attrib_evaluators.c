/* APPLE_vertex_program_evaluators as Apple's OpenGL answers it: the maps'
   errors, storage and queries (glGetVertexAttrib*vARB), their enables, and
   what evaluation generates with a vertex program (read back through
   feedback mode: the program copies attributes to the colour and texture
   coordinates): sizes 1-4, 1D and 2D maps, the vertex from attribute 0
   or the conventional maps, conventional maps beside attribute maps, the
   current attributes left alone, GLSL programs, display lists and
   glPushAttrib. make test-attrib-evaluators diffs a run on Apple's
   against one on GLMetal.

   attrib_evaluators [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

static void *lib;

static void values(const char *what, int n, const float *v)
{
    printf("%s:", what);
    for (int i = 0; i < n; ++i) printf(" %.3f", fabs(v[i]) < 5e-4 ? 0.0 : v[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glGetError); F(glGetFloatv); F(glIsEnabled); F(glEnable); F(glDisable); F(glBegin); F(glEnd);
    F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT); F(glBindRenderbufferEXT);
    F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport); F(glFeedbackBuffer);
    F(glRenderMode); F(glMap1f); F(glMap2f); F(glMapGrid1f); F(glMapGrid2f); F(glEvalCoord1f); F(glEvalCoord2f);
    F(glEvalMesh1); F(glEvalMesh2); F(glEvalPoint1); F(glColor4f); F(glVertexAttrib4fARB); F(glGetVertexAttribfvARB);
    F(glGetVertexAttribivARB); F(glGetVertexAttribdvARB); F(glGenProgramsARB); F(glBindProgramARB);
    F(glProgramStringARB); F(glMapVertexAttrib1fAPPLE); F(glMapVertexAttrib1dAPPLE); F(glMapVertexAttrib2fAPPLE);
    F(glMapVertexAttrib2dAPPLE); F(glEnableVertexAttribAPPLE); F(glDisableVertexAttribAPPLE);
    F(glIsVertexAttribEnabledAPPLE); F(glPushAttrib); F(glPopAttrib); F(glNewList); F(glEndList); F(glCallList);
    F(glGenLists); F(glCreateShader); F(glShaderSource); F(glCompileShader); F(glCreateProgram); F(glAttachShader);
    F(glLinkProgram); F(glUseProgram); F(glBindAttribLocation); F(glGetProgramiv);

    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);
    GLuint fbo, rb;
    pglGenFramebuffersEXT(1, &fbo);
    pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    pglGenRenderbuffersEXT(1, &rb);
    pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 16, 16);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 16, 16);
    while (pglGetError()) {}
#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())
    GLfloat v[512];
    GLint iv[512];
    GLdouble dv[512];
#define QUERY(what, index, pname, n) do { for (int i_ = 0; i_ < 512; ++i_) v[i_] = -7; \
        pglGetVertexAttribfvARB(index, pname, v); values(what, n, v); ERR(what); } while (0)

    /* ---- defaults and errors ---- */
    QUERY("default map1 size", 1, GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE, 1);
    QUERY("default map1 coeff", 1, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, 5);
    QUERY("default map1 order", 1, GL_VERTEX_ATTRIB_MAP1_ORDER_APPLE, 2);
    QUERY("default map1 domain", 1, GL_VERTEX_ATTRIB_MAP1_DOMAIN_APPLE, 3);
    QUERY("default map2 size", 7, GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE, 1);
    QUERY("default map2 coeff", 7, GL_VERTEX_ATTRIB_MAP2_COEFF_APPLE, 5);
    QUERY("default map2 order", 7, GL_VERTEX_ATTRIB_MAP2_ORDER_APPLE, 3);
    QUERY("default map2 domain", 7, GL_VERTEX_ATTRIB_MAP2_DOMAIN_APPLE, 5);
    QUERY("map query index 16", 16, GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE, 1);
    printf("default enabled: %d %d\n", pglIsVertexAttribEnabledAPPLE(2, GL_VERTEX_ATTRIB_MAP1_APPLE),
           pglIsVertexAttribEnabledAPPLE(2, GL_VERTEX_ATTRIB_MAP2_APPLE));
    ERR("default enabled");
    const float points[] = {0.5f, -0.25f, 0.125f, 1, 0.25f, 0.75f, 1, 0.5f, -1, 0.5f, 0.25f, 0,
                            1, 0, 0.5f, 1, 0.75f, 0.25f, 0, 0.25f, 0.125f, 0.625f, 0.875f, 1};
    const double dpoints[] = {0.5, -0.25, 0.125, 1, 0.25, 0.75, 1, 0.5, -1, 0.5, 0.25, 0};
    pglMapVertexAttrib1fAPPLE(16, 4, 0, 1, 4, 2, points); ERR("map1 index 16");
    pglMapVertexAttrib1fAPPLE(1, 0, 0, 1, 4, 2, points); ERR("map1 size 0");
    pglMapVertexAttrib1fAPPLE(1, 5, 0, 1, 5, 2, points); ERR("map1 size 5");
    pglMapVertexAttrib1fAPPLE(1, 4, 0, 1, 3, 2, points); ERR("map1 stride 3");
    pglMapVertexAttrib1fAPPLE(1, 4, 0, 1, 4, 0, points); ERR("map1 order 0");
    pglMapVertexAttrib1fAPPLE(1, 4, 0, 1, 4, 11, points); ERR("map1 order 11");
    pglMapVertexAttrib1fAPPLE(1, 4, 1, 1, 4, 2, points); ERR("map1 empty domain");
    pglMapVertexAttrib2fAPPLE(1, 4, 0, 1, 4, 2, 0, 1, 2, 2, points); ERR("map2 vstride 2");
    pglMapVertexAttrib2fAPPLE(1, 4, 0, 1, 4, 2, 0, 0, 8, 2, points); ERR("map2 empty v domain");
    pglMapVertexAttrib2fAPPLE(1, 4, 0, 1, 4, 2, 0, 1, 8, 11, points); ERR("map2 vorder 11");
    pglEnableVertexAttribAPPLE(16, GL_VERTEX_ATTRIB_MAP1_APPLE); ERR("enable index 16");
    pglEnableVertexAttribAPPLE(1, GL_MAP1_VERTEX_3); ERR("enable bad pname");
    pglIsVertexAttribEnabledAPPLE(1, GL_MAP1_VERTEX_3); ERR("is enabled bad pname");
    pglIsVertexAttribEnabledAPPLE(16, GL_VERTEX_ATTRIB_MAP1_APPLE); ERR("is enabled index 16");
    pglBegin(GL_POINTS);
    pglMapVertexAttrib1fAPPLE(1, 4, 0, 1, 4, 2, points);
    pglEnd();
    ERR("map1 in begin");
    QUERY("bad pname", 1, GL_VERTEX_ATTRIB_MAP1_APPLE, 1);

    /* ---- storage ---- */
    for (int size = 1; size <= 4; ++size) {
        char what[64];
        pglMapVertexAttrib1fAPPLE(2, (GLuint)size, -1, 3, 5, 3, points);
        snprintf(what, sizeof what, "map1 size %d", size);
        ERR(what);
        QUERY(what, 2, GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE, 1);
        QUERY(what, 2, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, size * 3 + 1);
        QUERY(what, 2, GL_VERTEX_ATTRIB_MAP1_ORDER_APPLE, 1);
        QUERY(what, 2, GL_VERTEX_ATTRIB_MAP1_DOMAIN_APPLE, 2);
    }
    pglMapVertexAttrib1dAPPLE(3, 3, 0.5, 2, 4, 3, dpoints); ERR("map1 double");
    QUERY("map1 double coeff", 3, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, 10);
    for (int i = 0; i < 512; ++i) dv[i] = -7;
    pglGetVertexAttribdvARB(3, GL_VERTEX_ATTRIB_MAP1_DOMAIN_APPLE, dv);
    printf("map1 double domain dv: %g %g %g\n", dv[0], dv[1], dv[2]);
    ERR("dv");
    for (int i = 0; i < 512; ++i) iv[i] = -7;
    pglGetVertexAttribivARB(3, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, iv);
    printf("map1 coeff iv: %d %d %d %d %d %d\n", iv[0], iv[1], iv[2], iv[3], iv[4], iv[5]);
    ERR("iv");
    pglMapVertexAttrib2fAPPLE(4, 3, 0, 2, 3, 2, -1, 1, 6, 3, points); ERR("map2");
    QUERY("map2 size", 4, GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE, 1);
    QUERY("map2 coeff", 4, GL_VERTEX_ATTRIB_MAP2_COEFF_APPLE, 19);
    QUERY("map2 order", 4, GL_VERTEX_ATTRIB_MAP2_ORDER_APPLE, 3);
    QUERY("map2 domain", 4, GL_VERTEX_ATTRIB_MAP2_DOMAIN_APPLE, 5);
    QUERY("map1 of map2 index", 4, GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE, 1);
    pglMapVertexAttrib2dAPPLE(5, 2, 0, 1, 2, 2, 0, 1, 4, 3, dpoints); ERR("map2 double");
    QUERY("map2 double coeff", 5, GL_VERTEX_ATTRIB_MAP2_COEFF_APPLE, 13);
    for (int i = 0; i < 512; ++i) iv[i] = -7;
    pglGetVertexAttribivARB(5, GL_VERTEX_ATTRIB_MAP2_ORDER_APPLE, iv);
    printf("map2 order iv: %d %d %d\n", iv[0], iv[1], iv[2]);
    ERR("map2 order iv");
    /* Conventional maps are separate storage. */
    float conventional[4] = {-7, -7, -7, -7};
    pglMap1f(GL_MAP1_COLOR_4, 0, 1, 4, 1, points + 12);
    QUERY("attrib 3 after map1 color", 3, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, 4);
    (void)conventional;

    /* ---- enables ---- */
    pglEnableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglEnableVertexAttribAPPLE(5, GL_VERTEX_ATTRIB_MAP2_APPLE);
    printf("enabled: %d %d %d %d\n", pglIsVertexAttribEnabledAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE),
           pglIsVertexAttribEnabledAPPLE(3, GL_VERTEX_ATTRIB_MAP2_APPLE),
           pglIsVertexAttribEnabledAPPLE(5, GL_VERTEX_ATTRIB_MAP2_APPLE), pglIsEnabled(GL_MAP1_COLOR_4));
    pglPushAttrib(GL_EVAL_BIT);
    pglDisableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglEnableVertexAttribAPPLE(6, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglPopAttrib();
    printf("after eval bit: %d %d\n", pglIsVertexAttribEnabledAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE),
           pglIsVertexAttribEnabledAPPLE(6, GL_VERTEX_ATTRIB_MAP1_APPLE));
    pglPushAttrib(GL_ENABLE_BIT);
    pglDisableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglPopAttrib();
    printf("after enable bit: %d\n", pglIsVertexAttribEnabledAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE));
    pglDisableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglDisableVertexAttribAPPLE(5, GL_VERTEX_ATTRIB_MAP2_APPLE);
    pglDisableVertexAttribAPPLE(6, GL_VERTEX_ATTRIB_MAP1_APPLE);
    ERR("enables");

    /* ---- evaluation ---- */
    const char *vp = "!!ARBvp1.0\n"
                     "MOV result.position, vertex.attrib[0];\n"
                     "MOV result.color, vertex.attrib[6];\n"
                     "MOV result.texcoord[0], vertex.attrib[9];\n"
                     "MOV result.texcoord[1], vertex.attrib[3];\n"
                     "END\n";
    GLuint program;
    pglGenProgramsARB(1, &program);
    pglBindProgramARB(GL_VERTEX_PROGRAM_ARB, program);
    pglProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(vp), vp);
    ERR("program");
    GLfloat buffer[2048];
#define FEEDBACK(what, body) do { pglFeedbackBuffer(2048, GL_4D_COLOR_TEXTURE, buffer); pglRenderMode(GL_FEEDBACK); \
        body; GLint n_ = pglRenderMode(GL_RENDER); printf("%s: %d\n", what, n_); \
        for (int i_ = 0; i_ < n_; i_ += 13) values("  ", n_ - i_ < 13 ? n_ - i_ : 13, buffer + i_); ERR(what); } while (0)
    const float positions[] = {-0.75f, -0.5f, 0, 1, 0.5f, 0.75f, 0.25f, 1, 0.75f, -0.25f, -0.5f, 2};
    pglMapVertexAttrib1fAPPLE(0, 4, 0, 1, 4, 3, positions);
    pglMapVertexAttrib1fAPPLE(6, 4, 0, 1, 4, 3, points);
    pglMapVertexAttrib1fAPPLE(9, 2, 0, 1, 3, 2, points + 4);
    pglEnableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglEnableVertexAttribAPPLE(6, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglEnableVertexAttribAPPLE(9, GL_VERTEX_ATTRIB_MAP1_APPLE);
    FEEDBACK("fixed function", pglBegin(GL_POINTS); pglEvalCoord1f(0.5f); pglEnd());
    pglEnable(GL_VERTEX_PROGRAM_ARB);
    FEEDBACK("program", pglBegin(GL_POINTS); pglEvalCoord1f(0); pglEvalCoord1f(0.25f); pglEvalCoord1f(0.6f);
             pglEvalCoord1f(1); pglEnd());
    pglVertexAttrib4fARB(6, 0.1f, 0.2f, 0.3f, 0.4f);
    pglVertexAttrib4fARB(9, 0.9f, 0.8f, 0.7f, 0.6f);
    FEEDBACK("program, map 9 disabled", pglDisableVertexAttribAPPLE(9, GL_VERTEX_ATTRIB_MAP1_APPLE);
             pglBegin(GL_POINTS); pglEvalCoord1f(0.5f); pglEnd());
    QUERY("current attrib 6", 6, GL_CURRENT_VERTEX_ATTRIB_ARB, 4);
    QUERY("current attrib 9", 9, GL_CURRENT_VERTEX_ATTRIB_ARB, 4);
    pglMapGrid1f(4, 0, 1);
    FEEDBACK("mesh1 points", pglEvalMesh1(GL_POINT, 0, 4));
    FEEDBACK("mesh1 lines", pglEvalMesh1(GL_LINE, 1, 3));
    FEEDBACK("point1", pglBegin(GL_POINTS); pglEvalPoint1(3); pglEnd());
    /* The vertex from conventional maps (attribute 0's disabled), or both. */
    const float vertex3[] = {-1, 1, 0.5f, 1, -1, -0.5f};
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, 2, vertex3);
    pglEnable(GL_MAP1_VERTEX_3);
    FEEDBACK("vertex 3 and attribute 0", pglBegin(GL_POINTS); pglEvalCoord1f(0.25f); pglEnd());
    pglDisableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE);
    FEEDBACK("vertex 3 only", pglBegin(GL_POINTS); pglEvalCoord1f(0.25f); pglEnd());
    pglDisable(GL_MAP1_VERTEX_3);
    FEEDBACK("no vertex map", pglBegin(GL_POINTS); pglEvalCoord1f(0.25f); pglEnd());
    pglEnableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE);
    /* Conventional maps beside attribute maps. */
    pglEnable(GL_MAP1_COLOR_4);
    pglColor4f(0.5f, 0.5f, 0.5f, 0.5f);
    FEEDBACK("color map and attribute 3", pglMapVertexAttrib1fAPPLE(3, 4, 0, 1, 4, 2, points + 8);
             pglEnableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE); pglBegin(GL_POINTS); pglEvalCoord1f(0.5f);
             pglEnd());
    pglDisableVertexAttribAPPLE(3, GL_VERTEX_ATTRIB_MAP1_APPLE);
    FEEDBACK("color map only", pglBegin(GL_POINTS); pglEvalCoord1f(0.5f); pglEnd());
    pglDisable(GL_MAP1_COLOR_4);
    pglGetFloatv(GL_CURRENT_COLOR, v);
    values("current color", 4, v);
    /* Sizes below 4 fill in 0, 0, 1. */
    for (int size = 1; size <= 3; ++size) {
        char what[64];
        snprintf(what, sizeof what, "attribute 6 size %d", size);
        pglMapVertexAttrib1fAPPLE(6, (GLuint)size, 0, 1, 4, 3, points);
        FEEDBACK(what, pglBegin(GL_POINTS); pglEvalCoord1f(0.4f); pglEnd());
    }
    /* Two-dimensional maps. */
    const float patch[] = {-0.5f, -0.5f, 0, 1, 0.5f, -0.5f, 0, 1, -0.5f, 0.5f, 0, 1, 0.5f, 0.5f, 0.5f, 1};
    pglDisableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglDisableVertexAttribAPPLE(6, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglMapVertexAttrib2fAPPLE(0, 4, 0, 1, 4, 2, 0, 1, 8, 2, patch);
    pglMapVertexAttrib2fAPPLE(6, 3, -1, 1, 3, 3, 0, 2, 9, 2, points);
    pglEnableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP2_APPLE);
    pglEnableVertexAttribAPPLE(6, GL_VERTEX_ATTRIB_MAP2_APPLE);
    FEEDBACK("map2", pglBegin(GL_POINTS); pglEvalCoord2f(0.25f, 0.5f); pglEvalCoord2f(0.75f, 1); pglEnd());
    FEEDBACK("map1 at 2D", pglBegin(GL_POINTS); pglEvalCoord1f(0.25f); pglEnd());
    pglMapGrid2f(2, 0, 1, 2, 0, 1);
    FEEDBACK("mesh2 fill", pglEvalMesh2(GL_FILL, 0, 2, 0, 1));
    pglDisable(GL_VERTEX_PROGRAM_ARB);
    FEEDBACK("map2 fixed function", pglBegin(GL_POINTS); pglEvalCoord2f(0.25f, 0.5f); pglEnd());

    /* ---- GLSL programs ---- */
    const char *vs = "attribute vec4 position; attribute vec4 value;\n"
                     "void main() { gl_Position = position; gl_FrontColor = value; gl_TexCoord[0] = value.zyxw; }\n";
    GLuint shader = pglCreateShader(GL_VERTEX_SHADER), glsl = pglCreateProgram();
    pglShaderSource(shader, 1, &vs, NULL);
    pglCompileShader(shader);
    pglAttachShader(glsl, shader);
    pglBindAttribLocation(glsl, 0, "position");
    pglBindAttribLocation(glsl, 6, "value");
    pglLinkProgram(glsl);
    GLint linked = 0;
    pglGetProgramiv(glsl, GL_LINK_STATUS, &linked);
    printf("glsl linked %d\n", linked);
    pglUseProgram(glsl);
    FEEDBACK("glsl map2", pglBegin(GL_POINTS); pglEvalCoord2f(0.25f, 0.5f); pglEnd());
    pglUseProgram(0);

    /* ---- display lists ---- */
    pglEnable(GL_VERTEX_PROGRAM_ARB);
    GLuint list = pglGenLists(1);
    float listed[12];
    memcpy(listed, positions, sizeof listed);
    pglNewList(list, GL_COMPILE);
    pglMapVertexAttrib1fAPPLE(0, 4, 0, 1, 4, 3, listed);
    pglEnableVertexAttribAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE);
    pglBegin(GL_POINTS);
    pglEvalCoord1f(0.5f);
    pglEnd();
    pglEndList();
    ERR("compile");
    printf("enabled after compile: %d\n", pglIsVertexAttribEnabledAPPLE(0, GL_VERTEX_ATTRIB_MAP1_APPLE));
    memset(listed, 0, sizeof listed);
    pglMapVertexAttrib1fAPPLE(0, 4, 0, 1, 4, 1, listed);
    FEEDBACK("call list", pglCallList(list));
    QUERY("listed coeff", 0, GL_VERTEX_ATTRIB_MAP1_COEFF_APPLE, 12);
    pglDisable(GL_VERTEX_PROGRAM_ARB);
    ERR("end");
    return 0;
}
