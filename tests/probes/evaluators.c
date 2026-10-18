/* Evaluators (GL 2.1 5.1) as Apple's OpenGL answers them: glMap1/glMap2
   errors and storage (glGetMap), grids, the getters and enables, and what
   evaluation generates, read back through feedback mode (positions,
   colours, texture coordinates; lighting shows the normals): curves and
   surfaces of every map type, glEvalCoord/glEvalPoint/glEvalMesh, map
   priorities, automatic normals, the current values left alone, display
   lists and glPushAttrib(GL_EVAL_BIT). make test-evaluators diffs a run on
   Apple's against one on GLMetal.

   evaluators [provider.dylib] */
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

/* Values to 3 decimals, with tiny values as 0 (rounding differs). */
static void value(double v)
{
    if (fabs(v) < 5e-4) v = 0;
    printf(" %.3f", v);
}

static void print_feedback(const char *what, GLint count, const GLfloat *buffer)
{
    printf("%s: %d:", what, count);
    for (int i = 0; i < count && i < 4096; ++i) {
        if (buffer[i] == GL_POINT_TOKEN || buffer[i] == GL_LINE_TOKEN || buffer[i] == GL_LINE_RESET_TOKEN ||
            buffer[i] == GL_POLYGON_TOKEN) {
            printf("\n  tok %g", buffer[i]);
            continue;
        }
        value(buffer[i]);
    }
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
    F(glGetError); F(glGetIntegerv); F(glGetFloatv); F(glGetDoublev); F(glGetBooleanv); F(glIsEnabled);
    F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT);
    F(glBindRenderbufferEXT); F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport);
    F(glMatrixMode); F(glLoadIdentity); F(glOrtho); F(glRenderMode); F(glFeedbackBuffer); F(glBegin); F(glEnd);
    F(glEnable); F(glDisable); F(glColor4f); F(glNormal3f); F(glTexCoord4f); F(glLightfv); F(glMaterialfv);
    F(glPushAttrib); F(glPopAttrib); F(glActiveTexture); F(glNewList); F(glEndList); F(glCallList); F(glGenLists);
    F(glMap1f); F(glMap1d); F(glMap2f); F(glMap2d); F(glMapGrid1f); F(glMapGrid1d); F(glMapGrid2f); F(glMapGrid2d);
    F(glEvalCoord1f); F(glEvalCoord1d); F(glEvalCoord1fv); F(glEvalCoord2f); F(glEvalCoord2dv); F(glEvalPoint1);
    F(glEvalPoint2); F(glEvalMesh1); F(glEvalMesh2); F(glGetMapfv); F(glGetMapdv); F(glGetMapiv); F(glShadeModel);
    F(glPolygonMode); F(glLightModeli);

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
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 100, 100);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 100, 100);
    pglMatrixMode(GL_PROJECTION);
    pglLoadIdentity();
    pglOrtho(-2, 2, -2, 2, -10, 10);
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    while (pglGetError()) {}

#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())
    static const GLenum targets1[] = {GL_MAP1_COLOR_4, GL_MAP1_INDEX, GL_MAP1_NORMAL, GL_MAP1_TEXTURE_COORD_1,
                                      GL_MAP1_TEXTURE_COORD_2, GL_MAP1_TEXTURE_COORD_3, GL_MAP1_TEXTURE_COORD_4,
                                      GL_MAP1_VERTEX_3, GL_MAP1_VERTEX_4};
    /* Defaults. */
    printf("max order:");
    GLint max_order = 0;
    pglGetIntegerv(GL_MAX_EVAL_ORDER, &max_order);
    printf(" %d\n", max_order);
    for (int d = 0; d < 2; ++d)
        for (int t = 0; t < 9; ++t) {
            GLenum target = targets1[t] + (GLenum)(d * 0x20);
            GLfloat v[64];
            for (int i = 0; i < 64; ++i) v[i] = -7;
            printf("default %04x coeff", target);
            pglGetMapfv(target, GL_COEFF, v);
            for (int i = 0; i < 5; ++i) value(v[i]);
            printf(" order");
            pglGetMapfv(target, GL_ORDER, v);
            for (int i = 0; i < 3; ++i) value(v[i]);
            printf(" domain");
            pglGetMapfv(target, GL_DOMAIN, v);
            for (int i = 0; i < 5; ++i) value(v[i]);
            printf(" enabled %d\n", pglIsEnabled(target));
            ERR("default");
        }
    {
        GLfloat v[4];
        GLint iv[4];
        pglGetFloatv(GL_MAP1_GRID_DOMAIN, v);
        printf("grid1 domain %g %g", v[0], v[1]);
        pglGetIntegerv(GL_MAP1_GRID_SEGMENTS, iv);
        printf(" segments %d\n", iv[0]);
        pglGetFloatv(GL_MAP2_GRID_DOMAIN, v);
        printf("grid2 domain %g %g %g %g", v[0], v[1], v[2], v[3]);
        pglGetIntegerv(GL_MAP2_GRID_SEGMENTS, iv);
        printf(" segments %d %d auto normal %d\n", iv[0], iv[1], pglIsEnabled(GL_AUTO_NORMAL));
    }

    /* Errors. */
    GLfloat points[256];
    for (int i = 0; i < 256; ++i) points[i] = (GLfloat)((i * 37) % 23) / 11.0f - 1.0f;
    GLdouble dpoints[256];
    for (int i = 0; i < 256; ++i) dpoints[i] = points[i];
    pglMap1f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, points); ERR("map1 2d target");
    pglMap1f(GL_TEXTURE_2D, 0, 1, 3, 2, points); ERR("map1 bad target");
    pglMap1f(GL_MAP1_VERTEX_3, 1, 1, 3, 2, points); ERR("map1 u1 == u2");
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 2, 2, points); ERR("map1 stride < k");
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, 0, points); ERR("map1 order 0");
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, max_order + 1, points); ERR("map1 order max+1");
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, max_order, points); ERR("map1 order max");
    pglMap1f(GL_MAP1_VERTEX_3, 1, 1, 2, 0, points); ERR("map1 every error");
    pglMap1f(GL_TEXTURE_2D, 1, 1, 2, 0, points); ERR("map1 bad target and values");
    pglActiveTexture(GL_TEXTURE1);
    pglMap1f(GL_MAP1_TEXTURE_COORD_2, 0, 1, 2, 2, points); ERR("map1 texture unit 1");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, 0, 1, 6, 2, points); ERR("map2 texture unit 1");
    pglActiveTexture(GL_TEXTURE0);
    pglBegin(GL_POINTS);
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, 2, points);
    pglEnd();
    ERR("map1 in begin");
    pglMap2f(GL_MAP1_VERTEX_3, 0, 1, 3, 2, 0, 1, 6, 2, points); ERR("map2 1d target");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, 0.5f, 0.5f, 6, 2, points); ERR("map2 v1 == v2");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, 0, 1, 2, 2, points); ERR("map2 vstride < k");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 2, 0, 1, 6, 0, points); ERR("map2 vorder 0");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, max_order + 1, 0, 1, 6, 2, points); ERR("map2 uorder max+1");
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 1, 2, 0, 1, 6, 2, points); ERR("map2 ustride < k");
    pglMapGrid1f(0, 0, 1); ERR("grid1 0 segments");
    pglMapGrid1f(-2, 0, 1); ERR("grid1 negative");
    pglMapGrid2f(3, 0, 1, 0, 0, 1); ERR("grid2 0 v segments");
    pglMapGrid1f(4, 1, 1); ERR("grid1 empty domain");
    pglEvalMesh1(GL_FILL, 0, 1); ERR("mesh1 fill");
    pglEvalMesh1(GL_TRIANGLES, 0, 1); ERR("mesh1 bad mode");
    pglEvalMesh2(GL_LINES, 0, 1, 0, 1); ERR("mesh2 bad mode");
    pglBegin(GL_POINTS);
    pglEvalMesh1(GL_POINT, 0, 1);
    pglEnd();
    ERR("mesh1 in begin");
    {
        GLfloat v[4];
        pglGetMapfv(GL_TEXTURE_2D, GL_COEFF, v); ERR("getmap bad target");
        pglGetMapfv(GL_MAP1_VERTEX_3, 0x0A03, v); ERR("getmap bad query");
    }
    pglEnable(GL_MAP1_VERTEX_3); ERR("enable map1");
    pglEvalCoord1f(0.5f); ERR("eval outside begin");
    pglEvalPoint1(1); ERR("evalpoint outside begin");
    pglDisable(GL_MAP1_VERTEX_3);

    /* Storage: strides, doubles, integer queries. */
    pglMap1f(GL_MAP1_VERTEX_4, -1, 3, 7, 4, points);
    pglMap1d(GL_MAP1_COLOR_4, 2, -1, 5, 3, dpoints + 9);
    pglMap2f(GL_MAP2_TEXTURE_COORD_2, 0.25f, 4, 5, 3, -2, 2, 17, 2, points + 3);
    pglMap2d(GL_MAP2_NORMAL, 1, 0, 3, 2, 0, 1, 6, 3, dpoints + 30);
    ERR("store");
    static const GLenum stored[] = {GL_MAP1_VERTEX_4, GL_MAP1_COLOR_4, GL_MAP2_TEXTURE_COORD_2, GL_MAP2_NORMAL};
    for (int s = 0; s < 4; ++s) {
        GLfloat v[64];
        GLdouble dv[64];
        GLint iv[64];
        memset(v, 0, sizeof v);
        pglGetMapfv(stored[s], GL_COEFF, v);
        printf("stored %04x coeff f", stored[s]);
        for (int i = 0; i < 20; ++i) value(v[i]);
        pglGetMapdv(stored[s], GL_COEFF, dv);
        printf(" d");
        for (int i = 0; i < 6; ++i) value(dv[i]);
        memset(iv, 0, sizeof iv);
        pglGetMapiv(stored[s], GL_COEFF, iv);
        printf(" i");
        for (int i = 0; i < 12; ++i) printf(" %d", iv[i]);
        pglGetMapiv(stored[s], GL_ORDER, iv);
        printf(" order %d %d", iv[0], iv[1]);
        pglGetMapiv(stored[s], GL_DOMAIN, iv);
        printf(" idomain %d %d %d %d", iv[0], iv[1], iv[2], iv[3]);
        pglGetMapfv(stored[s], GL_DOMAIN, v);
        printf(" domain %g %g %g %g\n", v[0], v[1], v[2], v[3]);
        ERR("stored");
    }

    /* Evaluation through feedback. */
    static GLfloat feedback[8192];
    const GLfloat white[] = {1, 1, 1, 1}, light[] = {0.3f, 0.5f, 0.8f, 0};
#define FEEDBACK_BEGIN() do { pglFeedbackBuffer(8192, GL_4D_COLOR_TEXTURE, feedback); pglRenderMode(GL_FEEDBACK); } while (0)
#define FEEDBACK_END(what) do { GLint n = pglRenderMode(GL_RENDER); print_feedback(what, n, feedback); ERR(what); } while (0)

    GLfloat curve[] = {-1.5f, -1, 0, 1, -0.5f, 1, 0, 1, 0.5f, -1, 0.5f, 1, 1.5f, 1, -0.5f, 0.5f};
    GLfloat colors[] = {1, 0, 0, 1, 0, 1, 0, 0.5f, 0, 0, 1, 1};
    GLfloat tex[] = {0, 0, 0, 1, 1, 0.5f, 0.25f, 2};
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 4, 4, curve);
    pglMap1f(GL_MAP1_VERTEX_4, 0, 1, 4, 4, curve);
    pglMap1f(GL_MAP1_COLOR_4, 0, 2, 4, 3, colors);
    pglMap1f(GL_MAP1_TEXTURE_COORD_1, 0, 1, 1, 2, tex);
    pglMap1f(GL_MAP1_TEXTURE_COORD_4, 0, 1, 4, 2, tex);
    GLfloat normals1[2][3] = {{0, 0, 1}, {1, 1, 0}};
    pglMap1f(GL_MAP1_NORMAL, 0, 1, 3, 2, &normals1[0][0]);
    pglLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    pglLightfv(GL_LIGHT0, GL_POSITION, light);
    pglMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
    pglColor4f(0.2f, 0.3f, 0.4f, 0.5f);
    pglTexCoord4f(9, 8, 7, 6);
    pglNormal3f(0, 1, 0);

    FEEDBACK_BEGIN();
    pglBegin(GL_POINTS);
    pglEvalCoord1f(0.5f); /* no vertex map: nothing */
    pglEnd();
    pglEnable(GL_MAP1_VERTEX_3);
    pglBegin(GL_POINTS);
    const float us[] = {0, 0.25f, 0.5f, 0.8f, 1, -0.5f, 1.5f};
    for (int i = 0; i < 7; ++i) pglEvalCoord1f(us[i]);
    pglEnd();
    pglEnable(GL_MAP1_VERTEX_4); /* vertex 4 wins */
    pglBegin(GL_POINTS);
    pglEvalCoord1d(0.3);
    pglEnd();
    pglEnable(GL_MAP1_COLOR_4);
    pglEnable(GL_MAP1_TEXTURE_COORD_1);
    pglBegin(GL_POINTS);
    pglEvalCoord1f(0.6f);
    pglEnd();
    pglEnable(GL_MAP1_TEXTURE_COORD_4); /* the higher dimension wins */
    pglBegin(GL_POINTS);
    GLfloat u = 0.9f;
    pglEvalCoord1fv(&u);
    pglEnd();
    pglEnable(GL_MAP1_NORMAL);
    pglEnable(GL_LIGHTING); /* the normal map, seen through lighting */
    pglEnable(GL_LIGHT0);
    pglBegin(GL_POINTS);
    pglEvalCoord1f(0.4f);
    pglEnd();
    pglDisable(GL_LIGHTING);
    pglDisable(GL_MAP1_NORMAL);
    FEEDBACK_END("curve points");
    {
        GLfloat c[4], t[4];
        pglGetFloatv(GL_CURRENT_COLOR, c);
        pglGetFloatv(GL_CURRENT_TEXTURE_COORDS, t);
        printf("current after eval: %g %g %g %g / %g %g %g %g\n", c[0], c[1], c[2], c[3], t[0], t[1], t[2], t[3]);
    }
    /* A plain vertex after evaluation uses the current values. */
    FEEDBACK_BEGIN();
    pglBegin(GL_LINES);
    pglEvalCoord1f(0.1f);
    pglEnd();
    F(glVertex2f);
    pglBegin(GL_LINES);
    pglEvalCoord1f(0.1f);
    pglVertex2f(0.5f, 0.5f);
    pglEnd();
    FEEDBACK_END("odd line");

    pglDisable(GL_MAP1_TEXTURE_COORD_4);
    pglDisable(GL_MAP1_VERTEX_4);
    pglMapGrid1f(5, 0.2f, 0.9f);
    FEEDBACK_BEGIN();
    pglEvalMesh1(GL_LINE, 0, 5);
    pglEvalMesh1(GL_POINT, 1, 3);
    pglEvalMesh1(GL_LINE, 4, 2); /* empty */
    pglBegin(GL_LINE_STRIP);
    for (int i = -1; i <= 6; ++i) pglEvalPoint1(i);
    pglEnd();
    pglMapGrid1d(3, 1, 0);
    pglEvalMesh1(GL_LINE, 0, 3);
    FEEDBACK_END("mesh1");
    pglDisable(GL_MAP1_VERTEX_3);
    pglDisable(GL_MAP1_COLOR_4);
    pglDisable(GL_MAP1_TEXTURE_COORD_1);

    /* Surfaces. */
    GLfloat patch[4][4][3];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            patch[i][j][0] = -1.5f + (float)j;
            patch[i][j][1] = -1.5f + (float)i;
            patch[i][j][2] = ((i == 1 || i == 2) && (j == 1 || j == 2)) ? 1.5f : (float)((i + j) % 3) * 0.3f - 0.3f;
        }
    GLfloat patch4[3][2][4];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j) {
            float w = 1 + 0.5f * (float)(i * 2 + j);
            patch4[i][j][0] = (-1 + 2.0f * (float)j) * w;
            patch4[i][j][1] = (-1 + (float)i) * w;
            patch4[i][j][2] = 0.4f * (float)(i - j) * w;
            patch4[i][j][3] = w;
        }
    GLfloat colors2[2][2][4] = {{{1, 0, 0, 1}, {0, 1, 0, 1}}, {{0, 0, 1, 1}, {1, 1, 1, 0}}};
    GLfloat normals2[2][2][3] = {{{0, 0, 1}, {1, 0, 1}}, {{0, 1, 1}, {-1, -1, 2}}};
    GLfloat tex2[2][2][3] = {{{0, 0, 0}, {1, 0, 0.5f}}, {{0, 1, 0.25f}, {1, 1, 1}}};
    pglMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &patch[0][0][0]);
    pglMap2f(GL_MAP2_VERTEX_4, 0, 1, 4, 2, 0, 1, 8, 3, &patch4[0][0][0]);
    pglMap2f(GL_MAP2_COLOR_4, 0, 1, 4, 2, 0, 1, 8, 2, &colors2[0][0][0]);
    pglMap2f(GL_MAP2_NORMAL, 0, 1, 3, 2, 0, 1, 6, 2, &normals2[0][0][0]);
    pglMap2f(GL_MAP2_TEXTURE_COORD_3, 0, 1, 3, 2, 0, 1, 6, 2, &tex2[0][0][0]);
    pglMap2f(GL_MAP2_TEXTURE_COORD_2, 0, 1, 3, 2, 0, 1, 6, 2, &tex2[0][0][0]);
    pglEnable(GL_MAP2_VERTEX_3);
    pglEnable(GL_MAP2_COLOR_4);
    pglEnable(GL_MAP2_TEXTURE_COORD_2);
    pglMapGrid2f(3, 0, 1, 2, 0, 1);
    FEEDBACK_BEGIN();
    pglBegin(GL_POINTS);
    pglEvalCoord2f(0.5f, 0.5f);
    const GLdouble uv[2] = {0.2, 0.7};
    pglEvalCoord2dv(uv);
    pglEvalPoint2(1, 1);
    pglEnd();
    pglEvalMesh2(GL_POINT, 0, 3, 0, 2);
    pglEvalMesh2(GL_LINE, 0, 2, 1, 2);
    pglEvalMesh2(GL_FILL, 1, 3, 0, 2);
    pglEnable(GL_MAP2_TEXTURE_COORD_3);
    pglEnable(GL_MAP2_VERTEX_4);
    pglMapGrid2d(2, 1, 0, 2, 0.5, 1);
    pglEvalMesh2(GL_FILL, 0, 2, 0, 2);
    FEEDBACK_END("surface");
    pglDisable(GL_MAP2_VERTEX_4);
    pglDisable(GL_MAP2_COLOR_4);
    pglDisable(GL_MAP2_TEXTURE_COORD_2);
    pglDisable(GL_MAP2_TEXTURE_COORD_3);

    /* Normals seen through lighting: the current one, the normal map, and
       automatic normals (also over a reversed domain, with a rational
       patch, and with GL_NORMALIZE). */
    pglEnable(GL_LIGHTING);
    pglEnable(GL_LIGHT0);
    pglLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    pglLightfv(GL_LIGHT0, GL_POSITION, light);
    pglLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    pglMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white);
    pglNormal3f(0.6f, 0, 0.8f);
    pglMapGrid2f(2, 0, 1, 2, 0, 1);
    FEEDBACK_BEGIN();
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglEnable(GL_MAP2_NORMAL);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglEnable(GL_AUTO_NORMAL);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglMapGrid2f(2, 1, 0, 2, 0, 1);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglMap2f(GL_MAP2_VERTEX_3, 1, 0, 3, 4, 0, 1, 12, 4, &patch[0][0][0]);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglMap2f(GL_MAP2_VERTEX_3, 0, 4, 3, 4, 0, 0.5f, 12, 4, &patch[0][0][0]);
    pglMapGrid2f(2, 0, 4, 2, 0, 0.5f);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglEnable(GL_NORMALIZE);
    pglEvalMesh2(GL_POINT, 0, 2, 0, 2);
    pglDisable(GL_NORMALIZE);
    FEEDBACK_END("normals");
    /* (Rational patches' automatic normals are not compared: Apple's do not
       follow the projected surface's, nor any formula found.) */
    {
        GLfloat n[3];
        pglGetFloatv(GL_CURRENT_NORMAL, n);
        printf("current normal %g %g %g\n", n[0], n[1], n[2]);
    }
    pglDisable(GL_AUTO_NORMAL);
    pglDisable(GL_MAP2_NORMAL);
    pglDisable(GL_MAP2_VERTEX_4);
    pglDisable(GL_LIGHTING);
    pglLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

    /* Display lists: maps copied at compile time, meshes and grids recorded. */
    GLuint list = pglGenLists(1);
    GLfloat line[] = {-1, -1, 0, 1, 1, 0.5f};
    pglNewList(list, GL_COMPILE);
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, 2, line);
    pglEnable(GL_MAP1_VERTEX_3);
    pglMapGrid1f(4, 0, 1);
    pglEvalMesh1(GL_LINE, 0, 4);
    pglBegin(GL_POINTS);
    pglEvalCoord1f(0.75f);
    pglEvalPoint1(1);
    pglEnd();
    pglEndList();
    line[0] = 5;
    pglDisable(GL_MAP1_VERTEX_3);
    {
        GLfloat v[6];
        pglGetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, v);
        printf("after compile: coeff %g %g %g order", v[0], v[1], v[2]);
        GLint o;
        pglGetMapiv(GL_MAP1_VERTEX_3, GL_ORDER, &o);
        printf(" %d enabled %d\n", o, pglIsEnabled(GL_MAP1_VERTEX_3));
    }
    FEEDBACK_BEGIN();
    pglCallList(list);
    FEEDBACK_END("list");

    /* Attribute groups. */
    pglPushAttrib(GL_EVAL_BIT);
    pglDisable(GL_MAP1_VERTEX_3);
    pglEnable(GL_MAP2_COLOR_4);
    pglEnable(GL_AUTO_NORMAL);
    pglMapGrid1f(9, -1, 2);
    pglMapGrid2f(7, 3, 4, 8, 5, 6);
    pglMap1f(GL_MAP1_VERTEX_3, 0, 1, 3, 2, points);
    pglPopAttrib();
    {
        GLfloat v[4];
        GLint iv[2];
        pglGetFloatv(GL_MAP1_GRID_DOMAIN, v);
        pglGetIntegerv(GL_MAP1_GRID_SEGMENTS, iv);
        printf("pop eval: map1 vertex3 %d map2 color %d auto %d grid1 %g %g %d", pglIsEnabled(GL_MAP1_VERTEX_3),
               pglIsEnabled(GL_MAP2_COLOR_4), pglIsEnabled(GL_AUTO_NORMAL), v[0], v[1], iv[0]);
        pglGetFloatv(GL_MAP2_GRID_DOMAIN, v);
        pglGetIntegerv(GL_MAP2_GRID_SEGMENTS, iv);
        printf(" grid2 %g %g %g %g %d %d", v[0], v[1], v[2], v[3], iv[0], iv[1]);
        pglGetFloatv(GL_MAP1_VERTEX_3, v);
        printf(" get enable %g", v[0]);
        pglGetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, v);
        printf(" coeff %g\n", v[0]);
    }
    pglPushAttrib(GL_ENABLE_BIT);
    pglDisable(GL_MAP1_VERTEX_3);
    pglEnable(GL_MAP2_VERTEX_4);
    pglEnable(GL_AUTO_NORMAL);
    pglPopAttrib();
    printf("pop enable: map1 vertex3 %d map2 vertex4 %d auto %d\n", pglIsEnabled(GL_MAP1_VERTEX_3),
           pglIsEnabled(GL_MAP2_VERTEX_4), pglIsEnabled(GL_AUTO_NORMAL));
    ERR("end");
    return 0;
}
