/* ARB_vertex_blend and APPLE_vertex_point_size as Apple's OpenGL answers
   them: the vertex units' matrices, enables and limits, then what blending
   does to positions and (lit) normals, read back through feedback mode,
   from current weights and weight arrays; and the per-vertex point sizes
   of glVertexPointSizefAPPLE and GL_POINT_SIZE_ARRAY_APPLE, read back as
   the pixels points cover. make test-vertex-blend diffs a run on Apple's
   against one on GLMetal.

   vertex_blend [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GL_MODELVIEW1_STACK_DEPTH_EXT 0x8502
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
    F(glGetError); F(glGetFloatv); F(glGetIntegerv); F(glIsEnabled); F(glEnable); F(glDisable); F(glBegin); F(glEnd);
    F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT); F(glBindRenderbufferEXT);
    F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport); F(glFeedbackBuffer);
    F(glRenderMode); F(glMatrixMode); F(glLoadIdentity); F(glTranslatef); F(glRotatef); F(glScalef); F(glPushMatrix);
    F(glPopMatrix); F(glVertex3f); F(glNormal3f); F(glColor4f); F(glWeightfvARB); F(glWeightubvARB);
    F(glVertexBlendARB); F(glWeightPointerARB); F(glVertexPointer); F(glNormalPointer); F(glEnableClientState);
    F(glDisableClientState); F(glDrawArrays); F(glDrawElements); F(glLightfv); F(glLightModeli); F(glClear);
    F(glClearColor); F(glReadPixels); F(glPointSize); F(glVertexPointSizefAPPLE); F(glPointSizePointerAPPLE);
    F(glPointParameterfv); F(glGenProgramsARB); F(glBindProgramARB); F(glProgramStringARB); F(glOrtho);

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
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 64, 64);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 64, 64);
    while (pglGetError()) {}
#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())
    GLfloat v[16];
    GLint iv[4];
#define GETF(what, pname, n) do { for (int i_ = 0; i_ < 16; ++i_) v[i_] = -7; pglGetFloatv(pname, v); \
        values(what, n, v); ERR(what); } while (0)
    GLfloat buffer[1024];
#define FEEDBACK(what, ...) do { pglFeedbackBuffer(1024, GL_3D_COLOR, buffer); pglRenderMode(GL_FEEDBACK); \
        __VA_ARGS__; GLint n_ = pglRenderMode(GL_RENDER); printf("%s: %d\n", what, n_); \
        for (int i_ = 0; i_ < n_; i_ += 8) values("  ", n_ - i_ < 8 ? n_ - i_ : 8, buffer + i_); ERR(what); } while (0)

    /* ---- state ---- */
    pglGetIntegerv(GL_MAX_VERTEX_UNITS_ARB, iv); printf("max vertex units %d\n", iv[0]);
    printf("enabled: %d %d\n", pglIsEnabled(GL_VERTEX_BLEND_ARB), pglIsEnabled(GL_WEIGHT_SUM_UNITY_ARB));
    ERR("enabled");
    static const GLenum units[] = {GL_MODELVIEW0_ARB, GL_MODELVIEW1_ARB, GL_MODELVIEW2_ARB, GL_MODELVIEW3_ARB,
                                   GL_MODELVIEW4_ARB, GL_MODELVIEW31_ARB};
    for (int u = 0; u < 6; ++u) {
        char what[64];
        snprintf(what, sizeof what, "matrix mode %#x", units[u]);
        pglMatrixMode(units[u]);
        ERR(what);
        pglLoadIdentity();
        pglTranslatef((float)u * 0.125f, -(float)u * 0.0625f, 0);
        pglRotatef(30.0f * (float)u, 0, 0, 1);
        ERR(what);
        pglGetIntegerv(GL_MATRIX_MODE, iv);
        printf("%s: mode %#x\n", what, iv[0]);
        GETF(what, units[u], 16);
    }
    pglMatrixMode(GL_MODELVIEW1_ARB);
    pglPushMatrix();
    pglScalef(2, 2, 2);
    pglGetIntegerv(GL_MODELVIEW1_STACK_DEPTH_EXT, iv); printf("modelview1 stack depth %d\n", iv[0]); ERR("depth");
    pglGetIntegerv(GL_MODELVIEW_STACK_DEPTH, iv); printf("modelview stack depth %d\n", iv[0]);
    GETF("pushed modelview1", GL_MODELVIEW1_ARB, 16);
    pglPopMatrix();
    GETF("popped modelview1", GL_MODELVIEW1_ARB, 16);
    pglMatrixMode(GL_MODELVIEW);
    GETF("modelview is modelview0", GL_MODELVIEW_MATRIX, 16);
    pglEnable(GL_WEIGHT_SUM_UNITY_ARB); ERR("enable weight sum unity");
    printf("weight sum unity %d\n", pglIsEnabled(GL_WEIGHT_SUM_UNITY_ARB));
    const GLfloat w3[3] = {0.25f, 0.5f, 0.125f};
    pglWeightfvARB(3, w3); ERR("weights 3 sum unity");
    GETF("current weight sum unity", GL_CURRENT_WEIGHT_ARB, 4);
    pglWeightfvARB(4, w3); ERR("weights 4 sum unity");
    pglDisable(GL_WEIGHT_SUM_UNITY_ARB);
    pglWeightfvARB(2, w3);
    GETF("current weight 2", GL_CURRENT_WEIGHT_ARB, 4);

    /* ---- blended positions ---- */
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    pglEnable(GL_VERTEX_BLEND_ARB);
    for (int n = 1; n <= 4; ++n) {
        char what[64];
        snprintf(what, sizeof what, "blend %d units", n);
        pglVertexBlendARB(n);
        const GLfloat w[4] = {0.5f, 0.25f, 0.125f, 0.0625f};
        FEEDBACK(what, pglWeightfvARB(4, w); pglBegin(GL_POINTS); pglVertex3f(0.25f, 0.5f, 0.125f);
                 pglVertex3f(-0.5f, 0.25f, 0.5f); pglEnd());
    }
    pglVertexBlendARB(2);
    FEEDBACK("weights per vertex", pglBegin(GL_POINTS); const GLfloat a[2] = {1, 0}; pglWeightfvARB(2, a);
             pglVertex3f(0.25f, 0.5f, 0); const GLfloat b[2] = {0, 1}; pglWeightfvARB(2, b);
             pglVertex3f(0.25f, 0.5f, 0); const GLfloat c[2] = {0.75f, 0.75f}; pglWeightfvARB(2, c);
             pglVertex3f(0.25f, 0.5f, 0); pglEnd());
    FEEDBACK("unsigned byte weights", const GLubyte ub[2] = {255, 64}; pglWeightubvARB(2, ub); pglBegin(GL_POINTS);
             pglVertex3f(0.25f, 0.5f, 0); pglEnd());
    pglEnable(GL_WEIGHT_SUM_UNITY_ARB);
    FEEDBACK("sum unity", const GLfloat one[1] = {0.25f}; pglWeightfvARB(1, one); pglBegin(GL_POINTS);
             pglVertex3f(0.25f, 0.5f, 0); pglEnd());
    pglDisable(GL_WEIGHT_SUM_UNITY_ARB);
    pglDisable(GL_VERTEX_BLEND_ARB);
    FEEDBACK("blend disabled", pglBegin(GL_POINTS); pglVertex3f(0.25f, 0.5f, 0); pglEnd());
    pglEnable(GL_VERTEX_BLEND_ARB);
    /* Weight arrays. */
    const GLfloat positions[] = {0.25f, 0.5f, 0, -0.5f, 0.25f, 0.5f, 0.5f, -0.5f, -0.25f};
    const GLfloat weights[] = {1, 0, 0, 0.5f, 0.5f, 0, 0.25f, 0.25f, 0.5f};
    const GLubyte ubweights[] = {255, 0, 128, 128, 64, 191};
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglVertexPointer(3, GL_FLOAT, 0, positions);
    pglEnableClientState(GL_WEIGHT_ARRAY_ARB);
    pglVertexBlendARB(3);
    pglWeightPointerARB(3, GL_FLOAT, 0, weights);
    FEEDBACK("weight array", pglDrawArrays(GL_POINTS, 0, 3));
    pglVertexBlendARB(2);
    pglWeightPointerARB(2, GL_UNSIGNED_BYTE, 0, ubweights);
    FEEDBACK("unsigned byte weight array", pglDrawArrays(GL_POINTS, 0, 3));
    pglWeightPointerARB(1, GL_FLOAT, 0, weights);
    FEEDBACK("weight array size 1", pglDrawArrays(GL_POINTS, 0, 3));
    const GLushort indices[] = {2, 0};
    FEEDBACK("weight array elements", pglDrawElements(GL_POINTS, 2, GL_UNSIGNED_SHORT, indices));
    pglDisableClientState(GL_WEIGHT_ARRAY_ARB);
    const GLfloat current[2] = {0.25f, 0.75f};
    pglWeightfvARB(2, current);
    FEEDBACK("arrays with current weights", pglDrawArrays(GL_POINTS, 0, 3));
    pglDisableClientState(GL_VERTEX_ARRAY);
    /* With a vertex program blending does nothing. */
    const char *vp = "!!ARBvp1.0\nOPTION ARB_position_invariant;\nMOV result.color, vertex.color;\nEND\n";
    GLuint program;
    pglGenProgramsARB(1, &program);
    pglBindProgramARB(GL_VERTEX_PROGRAM_ARB, program);
    pglProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(vp), vp);
    pglEnable(GL_VERTEX_PROGRAM_ARB);
    FEEDBACK("vertex program", pglBegin(GL_POINTS); pglVertex3f(0.25f, 0.5f, 0); pglEnd());
    pglDisable(GL_VERTEX_PROGRAM_ARB);

    /* ---- blended normals (lighting) ---- */
    const GLfloat light[4] = {0.6f, 0.0f, 0.8f, 0}, white[4] = {1, 1, 1, 1}, dark[4] = {0, 0, 0, 1};
    pglEnable(GL_LIGHTING);
    pglEnable(GL_LIGHT0);
    pglLightfv(GL_LIGHT0, GL_POSITION, light);
    pglLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    pglLightfv(GL_LIGHT0, GL_AMBIENT, dark);
    pglMatrixMode(GL_MODELVIEW1_ARB);
    pglLoadIdentity();
    pglRotatef(60, 0, 1, 0);
    pglScalef(1, 2, 1);
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    pglRotatef(-20, 1, 0, 0);
    for (int i = 0; i < 4; ++i) {
        char what[64];
        const GLfloat w[2] = {1 - (float)i / 3, (float)i / 3};
        snprintf(what, sizeof what, "lit normal weights %.2f %.2f", w[0], w[1]);
        FEEDBACK(what, pglWeightfvARB(2, w); pglBegin(GL_POINTS); pglNormal3f(0, 0, 1); pglVertex3f(0, 0, 0);
                 pglNormal3f(0.6f, 0.8f, 0); pglVertex3f(0, 0, 0); pglEnd());
    }
    pglEnable(GL_NORMALIZE);
    FEEDBACK("normalized", const GLfloat w[2] = {0.5f, 0.5f}; pglWeightfvARB(2, w); pglBegin(GL_POINTS);
             pglNormal3f(0, 0, 1); pglVertex3f(0, 0, 0); pglEnd());
    pglDisable(GL_NORMALIZE);
    pglDisable(GL_LIGHTING);
    pglDisable(GL_VERTEX_BLEND_ARB);
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    ERR("blend");

    /* ---- per-vertex point sizes ---- */
    pglMatrixMode(GL_PROJECTION);
    pglLoadIdentity();
    pglOrtho(0, 64, 0, 64, -1, 1);
    pglMatrixMode(GL_MODELVIEW);
    pglClearColor(0, 0, 0, 0);
    pglColor4f(1, 1, 1, 1);
    static GLubyte pixels[64 * 64 * 4];
#define POINTS(what, ...) do { pglClear(GL_COLOR_BUFFER_BIT); __VA_ARGS__; \
        pglReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels); \
        int columns[4] = {0, 0, 0, 0}; \
        for (int y_ = 0; y_ < 64; ++y_) for (int x_ = 0; x_ < 64; ++x_) \
            if (pixels[(y_ * 64 + x_) * 4]) ++columns[x_ / 16]; \
        printf("%s: %d %d %d %d\n", what, columns[0], columns[1], columns[2], columns[3]); ERR(what); } while (0)
    pglPointSize(3);
    POINTS("point size 3", pglBegin(GL_POINTS); pglVertexPointSizefAPPLE(6); pglVertex3f(8, 32, 0); pglEnd());
    pglEnable(GL_VERTEX_POINT_SIZE_APPLE);
    POINTS("vertex point sizes", pglBegin(GL_POINTS); pglVertexPointSizefAPPLE(1); pglVertex3f(8, 32, 0);
           pglVertexPointSizefAPPLE(4); pglVertex3f(24, 32, 0); pglVertexPointSizefAPPLE(7); pglVertex3f(40, 32, 0);
           pglVertexPointSizefAPPLE(10.5f); pglVertex3f(56, 32, 0); pglEnd());
    POINTS("current point size kept", pglBegin(GL_POINTS); pglVertex3f(8, 32, 0); pglEnd());
    const GLfloat point_positions[] = {8, 32, 0, 24, 32, 0, 40, 32, 0, 56, 32, 0};
    const GLfloat sizes[] = {2, 5, 8, 12};
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglVertexPointer(3, GL_FLOAT, 0, point_positions);
    pglEnableClientState(GL_POINT_SIZE_ARRAY_APPLE);
    pglPointSizePointerAPPLE(GL_FLOAT, 0, sizes);
    ERR("point size pointer");
    POINTS("point size array", pglDrawArrays(GL_POINTS, 0, 4));
    pglDisable(GL_VERTEX_POINT_SIZE_APPLE);
    POINTS("point size array, not enabled", pglDrawArrays(GL_POINTS, 0, 4));
    pglEnable(GL_VERTEX_POINT_SIZE_APPLE);
    pglDisableClientState(GL_POINT_SIZE_ARRAY_APPLE);
    pglVertexPointSizefAPPLE(9);
    POINTS("arrays with current point size", pglDrawArrays(GL_POINTS, 0, 4));
    const GLfloat attenuation[3] = {0.5f, 0, 0};
    pglPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, attenuation);
    POINTS("attenuated", pglDrawArrays(GL_POINTS, 0, 4));
    const GLfloat no_attenuation[3] = {1, 0, 0};
    pglPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, no_attenuation);
    pglDisableClientState(GL_VERTEX_ARRAY);
    pglDisable(GL_VERTEX_POINT_SIZE_APPLE);
    pglPointSize(1);
    ERR("end");
    return 0;
}
