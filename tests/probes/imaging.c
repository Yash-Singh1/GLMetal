/* The imaging subset (ARB_imaging, SGI_color_matrix; GL 2.1 3.6.5) as
   Apple's OpenGL answers it: colour tables, convolution filters, the
   colour matrix, histogram and minmax. Their state, errors and queries;
   then what they do to pixels on each path (glDrawPixels, glReadPixels,
   glCopyPixels, glTexImage, glTexSubImage, glCopyTexImage) and the pixel
   transfer scale, bias and maps on the read paths. make test-imaging
   diffs a run on Apple's against one on GLMetal.

   imaging [provider.dylib] */
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
#define D(name) static typeof(&name) p##name

D(glGetError); D(glGetIntegerv); D(glGetFloatv); D(glIsEnabled); D(glEnable); D(glDisable);
D(glClear); D(glClearColor); D(glReadPixels); D(glDrawPixels); D(glCopyPixels); D(glWindowPos2i);
D(glPixelTransferf); D(glPixelMapfv); D(glPixelStorei); D(glMatrixMode); D(glLoadMatrixf); D(glLoadIdentity);
D(glPushMatrix); D(glPopMatrix); D(glPushAttrib); D(glPopAttrib); D(glGenTextures); D(glBindTexture);
D(glTexImage1D); D(glTexImage2D); D(glTexImage3D); D(glTexSubImage2D); D(glCopyTexImage2D);
D(glCopyTexSubImage2D); D(glGetTexImage); D(glGetTexLevelParameteriv); D(glDeleteTextures);
D(glNewList); D(glEndList); D(glCallList); D(glGenLists); D(glBegin); D(glEnd);
D(glColorTable); D(glColorSubTable); D(glCopyColorTable); D(glCopyColorSubTable); D(glColorTableParameterfv);
D(glColorTableParameteriv); D(glGetColorTable); D(glGetColorTableParameterfv); D(glGetColorTableParameteriv);
D(glConvolutionFilter1D); D(glConvolutionFilter2D); D(glSeparableFilter2D); D(glCopyConvolutionFilter1D);
D(glCopyConvolutionFilter2D); D(glConvolutionParameteri); D(glConvolutionParameterf);
D(glConvolutionParameteriv); D(glConvolutionParameterfv); D(glGetConvolutionFilter); D(glGetSeparableFilter);
D(glGetConvolutionParameteriv); D(glGetConvolutionParameterfv); D(glHistogram); D(glResetHistogram);
D(glGetHistogram); D(glGetHistogramParameteriv); D(glGetHistogramParameterfv); D(glMinmax); D(glResetMinmax);
D(glGetMinmax); D(glGetMinmaxParameteriv); D(glGetMinmaxParameterfv);

#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())

static const GLenum enables[] = {GL_COLOR_TABLE, GL_POST_CONVOLUTION_COLOR_TABLE, GL_POST_COLOR_MATRIX_COLOR_TABLE,
                                 GL_CONVOLUTION_1D, GL_CONVOLUTION_2D, GL_SEPARABLE_2D, GL_HISTOGRAM, GL_MINMAX};
static const GLenum transfer[] = {GL_POST_CONVOLUTION_RED_SCALE, GL_POST_CONVOLUTION_GREEN_SCALE,
                                  GL_POST_CONVOLUTION_BLUE_SCALE, GL_POST_CONVOLUTION_ALPHA_SCALE,
                                  GL_POST_CONVOLUTION_RED_BIAS, GL_POST_CONVOLUTION_GREEN_BIAS,
                                  GL_POST_CONVOLUTION_BLUE_BIAS, GL_POST_CONVOLUTION_ALPHA_BIAS,
                                  GL_POST_COLOR_MATRIX_RED_SCALE, GL_POST_COLOR_MATRIX_GREEN_SCALE,
                                  GL_POST_COLOR_MATRIX_BLUE_SCALE, GL_POST_COLOR_MATRIX_ALPHA_SCALE,
                                  GL_POST_COLOR_MATRIX_RED_BIAS, GL_POST_COLOR_MATRIX_GREEN_BIAS,
                                  GL_POST_COLOR_MATRIX_BLUE_BIAS, GL_POST_COLOR_MATRIX_ALPHA_BIAS};

/* Everything off and at its default: imaging, pixel transfer, the colour matrix. */
static void reset(void)
{
    for (size_t i = 0; i < sizeof enables / sizeof *enables; ++i) pglDisable(enables[i]);
    for (size_t i = 0; i < sizeof transfer / sizeof *transfer; ++i) pglPixelTransferf(transfer[i], (i & 4) ? 0 : 1);
    const GLenum scales[] = {GL_RED_SCALE, GL_GREEN_SCALE, GL_BLUE_SCALE, GL_ALPHA_SCALE};
    const GLenum biases[] = {GL_RED_BIAS, GL_GREEN_BIAS, GL_BLUE_BIAS, GL_ALPHA_BIAS};
    for (int i = 0; i < 4; ++i) pglPixelTransferf(scales[i], 1), pglPixelTransferf(biases[i], 0);
    pglPixelTransferf(GL_MAP_COLOR, 0);
    pglMatrixMode(GL_COLOR);
    pglLoadIdentity();
    pglMatrixMode(GL_MODELVIEW);
}

static void bytes(const char *what, const GLubyte *p, int n)
{
    printf("%s:", what);
    for (int i = 0; i < n; ++i) printf(" %02x%02x%02x%02x", p[i * 4], p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3]);
    printf("\n");
}

static void floats(const char *what, const GLfloat *v, int n)
{
    printf("%s:", what);
    for (int i = 0; i < n; ++i) printf(" %.3f", fabs(v[i]) < 5e-4 ? 0.0 : v[i]);
    printf("\n");
}

/* The framebuffer's row `y` (`n` pixels), read with imaging off. */
static void row(const char *what, int y, int n)
{
    GLubyte p[32 * 4];
    memset(p, 0xEE, sizeof p);
    pglReadPixels(0, y, n, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    bytes(what, p, n);
}

static GLubyte image[8 * 8 * 4]; /* an 8x8 ramp */

static void draw(int y)
{
    pglWindowPos2i(0, y);
    pglDrawPixels(8, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
}

static void clear(void)
{
    reset();
    pglClear(GL_COLOR_BUFFER_BIT);
}

static void table_parameters(const char *what, GLenum target)
{
    static const GLenum pnames[] = {GL_COLOR_TABLE_FORMAT, GL_COLOR_TABLE_WIDTH, GL_COLOR_TABLE_RED_SIZE,
                                    GL_COLOR_TABLE_GREEN_SIZE, GL_COLOR_TABLE_BLUE_SIZE, GL_COLOR_TABLE_ALPHA_SIZE,
                                    GL_COLOR_TABLE_LUMINANCE_SIZE, GL_COLOR_TABLE_INTENSITY_SIZE};
    printf("%s params:", what);
    for (size_t i = 0; i < sizeof pnames / sizeof *pnames; ++i) {
        GLint v = -7;
        pglGetColorTableParameteriv(target, pnames[i], &v);
        printf(" %#x", v);
    }
    if (target == GL_COLOR_TABLE || target == GL_POST_CONVOLUTION_COLOR_TABLE || target == GL_POST_COLOR_MATRIX_COLOR_TABLE) {
        GLfloat scale[4] = {-7, -7, -7, -7}, bias[4] = {-7, -7, -7, -7};
        pglGetColorTableParameterfv(target, GL_COLOR_TABLE_SCALE, scale);
        pglGetColorTableParameterfv(target, GL_COLOR_TABLE_BIAS, bias);
        printf(" scale %g %g %g %g bias %g %g %g %g", scale[0], scale[1], scale[2], scale[3], bias[0], bias[1], bias[2],
               bias[3]);
    }
    printf("\n");
    ERR(what);
}

static void convolution_parameters(const char *what, GLenum target)
{
    static const GLenum pnames[] = {GL_CONVOLUTION_BORDER_MODE, GL_CONVOLUTION_FORMAT, GL_CONVOLUTION_WIDTH,
                                    GL_CONVOLUTION_HEIGHT, GL_MAX_CONVOLUTION_WIDTH, GL_MAX_CONVOLUTION_HEIGHT};
    printf("%s params:", what);
    for (size_t i = 0; i < sizeof pnames / sizeof *pnames; ++i) {
        GLint v = -7;
        pglGetConvolutionParameteriv(target, pnames[i], &v);
        printf(" %#x", v);
    }
    GLfloat color[4] = {-7, -7, -7, -7}, scale[4] = {-7, -7, -7, -7}, bias[4] = {-7, -7, -7, -7};
    pglGetConvolutionParameterfv(target, GL_CONVOLUTION_BORDER_COLOR, color);
    pglGetConvolutionParameterfv(target, GL_CONVOLUTION_FILTER_SCALE, scale);
    pglGetConvolutionParameterfv(target, GL_CONVOLUTION_FILTER_BIAS, bias);
    printf(" color %g %g %g %g scale %g %g %g %g bias %g %g %g %g\n", color[0], color[1], color[2], color[3], scale[0],
           scale[1], scale[2], scale[3], bias[0], bias[1], bias[2], bias[3]);
    ERR(what);
}

static void histogram_parameters(const char *what, GLenum target)
{
    static const GLenum pnames[] = {GL_HISTOGRAM_WIDTH, GL_HISTOGRAM_FORMAT, GL_HISTOGRAM_RED_SIZE,
                                    GL_HISTOGRAM_GREEN_SIZE, GL_HISTOGRAM_BLUE_SIZE, GL_HISTOGRAM_ALPHA_SIZE,
                                    GL_HISTOGRAM_LUMINANCE_SIZE, GL_HISTOGRAM_SINK};
    printf("%s params:", what);
    for (size_t i = 0; i < sizeof pnames / sizeof *pnames; ++i) {
        GLint v = -7;
        pglGetHistogramParameteriv(target, pnames[i], &v);
        printf(" %#x", v);
    }
    printf("\n");
    ERR(what);
}

static void histogram(const char *what, int width)
{
    GLuint h[64 * 4];
    memset(h, 0xEE, sizeof h);
    pglGetHistogram(GL_HISTOGRAM, GL_FALSE, GL_RGBA, GL_UNSIGNED_INT, h);
    printf("%s:", what);
    for (int i = 0; i < width * 4; ++i) printf(" %u", h[i]);
    printf("\n");
    ERR(what);
}

static void minmax(const char *what)
{
    GLfloat m[8];
    for (int i = 0; i < 8; ++i) m[i] = -7;
    pglGetMinmax(GL_MINMAX, GL_FALSE, GL_RGBA, GL_FLOAT, m);
    floats(what, m, 8);
    ERR(what);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
#define L(name) p##name = dlsym(lib, #name)
    L(glGetError); L(glGetIntegerv); L(glGetFloatv); L(glIsEnabled); L(glEnable); L(glDisable);
    L(glClear); L(glClearColor); L(glReadPixels); L(glDrawPixels); L(glCopyPixels); L(glWindowPos2i);
    L(glPixelTransferf); L(glPixelMapfv); L(glPixelStorei); L(glMatrixMode); L(glLoadMatrixf); L(glLoadIdentity);
    L(glPushMatrix); L(glPopMatrix); L(glPushAttrib); L(glPopAttrib); L(glGenTextures); L(glBindTexture);
    L(glTexImage1D); L(glTexImage2D); L(glTexImage3D); L(glTexSubImage2D); L(glCopyTexImage2D);
    L(glCopyTexSubImage2D); L(glGetTexImage); L(glGetTexLevelParameteriv); L(glDeleteTextures);
    L(glNewList); L(glEndList); L(glCallList); L(glGenLists); L(glBegin); L(glEnd);
    L(glColorTable); L(glColorSubTable); L(glCopyColorTable); L(glCopyColorSubTable); L(glColorTableParameterfv);
    L(glColorTableParameteriv); L(glGetColorTable); L(glGetColorTableParameterfv); L(glGetColorTableParameteriv);
    L(glConvolutionFilter1D); L(glConvolutionFilter2D); L(glSeparableFilter2D); L(glCopyConvolutionFilter1D);
    L(glCopyConvolutionFilter2D); L(glConvolutionParameteri); L(glConvolutionParameterf);
    L(glConvolutionParameteriv); L(glConvolutionParameterfv); L(glGetConvolutionFilter); L(glGetSeparableFilter);
    L(glGetConvolutionParameteriv); L(glGetConvolutionParameterfv); L(glHistogram); L(glResetHistogram);
    L(glGetHistogram); L(glGetHistogramParameteriv); L(glGetHistogramParameterfv); L(glMinmax); L(glResetMinmax);
    L(glGetMinmax); L(glGetMinmaxParameteriv); L(glGetMinmaxParameterfv);
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT); F(glBindRenderbufferEXT);
    F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport);

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
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 32, 32);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 32, 32);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pglPixelStorei(GL_PACK_ALIGNMENT, 1);
    pglClearColor(0, 0, 0, 0);
    for (int i = 0; i < 64; ++i) {
        image[i * 4 + 0] = (GLubyte)(i * 4);
        image[i * 4 + 1] = (GLubyte)(255 - i * 4);
        image[i * 4 + 2] = (GLubyte)(i % 8 * 32);
        image[i * 4 + 3] = (GLubyte)(128 + i);
    }
    while (pglGetError()) {}

    /* ---- defaults ---- */
    printf("enabled:");
    for (size_t i = 0; i < sizeof enables / sizeof *enables; ++i) printf(" %d", pglIsEnabled(enables[i]));
    printf("\n");
    printf("transfer:");
    for (size_t i = 0; i < sizeof transfer / sizeof *transfer; ++i) {
        GLfloat v = -7;
        pglGetFloatv(transfer[i], &v);
        printf(" %g", v);
    }
    printf("\n");
    GLint v[4] = {-7, -7, -7, -7};
    pglGetIntegerv(GL_MAX_COLOR_MATRIX_STACK_DEPTH, v); printf("max color matrix stack %d\n", v[0]);
    pglGetIntegerv(GL_COLOR_MATRIX_STACK_DEPTH, v); printf("color matrix stack %d\n", v[0]);
    ERR("defaults");
    table_parameters("color table", GL_COLOR_TABLE);
    table_parameters("post convolution table", GL_POST_CONVOLUTION_COLOR_TABLE);
    table_parameters("post color matrix table", GL_POST_COLOR_MATRIX_COLOR_TABLE);
    table_parameters("proxy table", GL_PROXY_COLOR_TABLE);
    convolution_parameters("convolution 1d", GL_CONVOLUTION_1D);
    convolution_parameters("convolution 2d", GL_CONVOLUTION_2D);
    convolution_parameters("separable 2d", GL_SEPARABLE_2D);
    histogram_parameters("histogram", GL_HISTOGRAM);
    histogram_parameters("proxy histogram", GL_PROXY_HISTOGRAM);
    pglGetMinmaxParameteriv(GL_MINMAX, GL_MINMAX_FORMAT, v); printf("minmax format %#x\n", v[0]);
    pglGetMinmaxParameteriv(GL_MINMAX, GL_MINMAX_SINK, v); printf("minmax sink %d\n", v[0]);
    ERR("minmax params");
    minmax("default minmax");
    histogram("default histogram", 1);

    /* ---- errors ---- */
    GLubyte table[256 * 4];
    for (int i = 0; i < 256; ++i) {
        table[i * 4 + 0] = (GLubyte)(255 - i);
        table[i * 4 + 1] = (GLubyte)(i * 7);
        table[i * 4 + 2] = (GLubyte)(i ^ 0x5a);
        table[i * 4 + 3] = (GLubyte)(i / 2 + 64);
    }
    pglColorTable(GL_TEXTURE_2D, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table bad target");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 3, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table width 3");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, -4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table width -4");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 0, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table width 0");
    { static GLubyte big[8192 * 4]; pglColorTable(GL_COLOR_TABLE, GL_RGBA, 8192, GL_RGBA, GL_UNSIGNED_BYTE, big); ERR("table width 8192"); }
    pglColorTable(GL_COLOR_TABLE, GL_DEPTH_COMPONENT, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table depth format");
    pglColorTable(GL_COLOR_TABLE, 7, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table internal 7");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, table); ERR("table format depth");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_BITMAP, table); ERR("table type bitmap");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, table); ERR("table format index");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA_INTEGER_EXT, GL_UNSIGNED_BYTE, table); ERR("table format integer");
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4, table); ERR("table packed mismatch");
    pglColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, 4096, GL_RGBA, GL_UNSIGNED_BYTE, NULL); ERR("proxy 4096");
    table_parameters("proxy 4096", GL_PROXY_COLOR_TABLE);
    pglColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, 8192, GL_RGBA, GL_UNSIGNED_BYTE, NULL); ERR("proxy 8192");
    table_parameters("proxy 8192", GL_PROXY_COLOR_TABLE);
    pglColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, 512, GL_RGBA, GL_UNSIGNED_BYTE, NULL); ERR("proxy 512");
    table_parameters("proxy 512", GL_PROXY_COLOR_TABLE);
    pglColorTable(GL_PROXY_POST_CONVOLUTION_COLOR_TABLE, GL_LUMINANCE8, 16, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    table_parameters("proxy post convolution", GL_PROXY_POST_CONVOLUTION_COLOR_TABLE);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("table 4");
    pglColorSubTable(GL_COLOR_TABLE, 2, 3, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("subtable past end");
    pglColorSubTable(GL_COLOR_TABLE, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("subtable negative start");
    pglColorSubTable(GL_PROXY_COLOR_TABLE, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("subtable proxy");
    pglColorSubTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("subtable undefined");
    pglCopyColorTable(GL_COLOR_TABLE, GL_RGBA, 0, 0, 5); ERR("copy table width 5");
    pglCopyColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, 0, 0, 4); ERR("copy table proxy");
    { const GLint ones[4] = {1, 1, 1, 1}; /* Apple applies these to GL_COLOR_TABLE */
      pglColorTableParameteriv(GL_PROXY_COLOR_TABLE, GL_COLOR_TABLE_SCALE, ones); ERR("table parameter proxy"); }
    pglColorTableParameteriv(GL_COLOR_TABLE, GL_COLOR_TABLE_WIDTH, v); ERR("table parameter width");
    pglGetColorTableParameteriv(GL_COLOR_TABLE, GL_TEXTURE_WIDTH, v); ERR("get table bad pname");
    pglGetColorTableParameteriv(GL_PROXY_COLOR_TABLE, GL_COLOR_TABLE_SCALE, v); ERR("get proxy scale");
    pglGetColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("get proxy table");
    pglGetColorTable(GL_COLOR_TABLE, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, table); ERR("get table depth");
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_BITMAP, table); ERR("get table bitmap");
    pglConvolutionFilter1D(GL_CONVOLUTION_2D, GL_RGBA, 3, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d target 2d");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 12, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d width 12");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, -1, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d width -1");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 0, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d width 0");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d width 4");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_DEPTH_COMPONENT, 3, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter1d depth");
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 3, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, table); ERR("filter1d index");
    pglConvolutionFilter2D(GL_CONVOLUTION_2D, GL_RGBA, 3, 12, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter2d height 12");
    pglConvolutionFilter2D(GL_SEPARABLE_2D, GL_RGBA, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("filter2d separable");
    pglSeparableFilter2D(GL_CONVOLUTION_2D, GL_RGBA, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, table, table); ERR("separable 2d target");
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REPEAT); ERR("border mode repeat");
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_WIDTH, 3); ERR("parameter width");
    pglConvolutionParameteri(GL_COLOR_TABLE, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE); ERR("parameter bad target");
    pglGetConvolutionParameteriv(GL_CONVOLUTION_1D, GL_COLOR_TABLE_WIDTH, v); ERR("get parameter bad pname");
    pglGetConvolutionFilter(GL_SEPARABLE_2D, GL_RGBA, GL_UNSIGNED_BYTE, table); ERR("get filter separable");
    pglGetSeparableFilter(GL_CONVOLUTION_2D, GL_RGBA, GL_UNSIGNED_BYTE, table, table, NULL); ERR("get separable 2d");
    pglHistogram(GL_HISTOGRAM, 3, GL_RGBA, GL_FALSE); ERR("histogram width 3");
    pglHistogram(GL_HISTOGRAM, -2, GL_RGBA, GL_FALSE); ERR("histogram width -2");
    pglHistogram(GL_HISTOGRAM, 0, GL_RGBA, GL_FALSE); ERR("histogram width 0");
    pglHistogram(GL_HISTOGRAM, 8192, GL_RGBA, GL_FALSE); ERR("histogram width 8192");
    pglHistogram(GL_COLOR_TABLE, 4, GL_RGBA, GL_FALSE); ERR("histogram bad target");
    pglHistogram(GL_HISTOGRAM, 4, GL_INTENSITY, GL_FALSE); ERR("histogram intensity");
    pglHistogram(GL_PROXY_HISTOGRAM, 8192, GL_RGBA, GL_FALSE); ERR("proxy histogram 8192");
    histogram_parameters("proxy histogram 8192", GL_PROXY_HISTOGRAM);
    /* Apple defines GL_HISTOGRAM from proxies (a sink there breaks its later lookups). */
    pglHistogram(GL_PROXY_HISTOGRAM, 64, GL_LUMINANCE_ALPHA, GL_FALSE); ERR("proxy histogram 64");
    histogram_parameters("proxy histogram 64", GL_PROXY_HISTOGRAM);
    histogram_parameters("histogram after proxy", GL_HISTOGRAM);
    pglGetHistogram(GL_PROXY_HISTOGRAM, GL_FALSE, GL_RGBA, GL_UNSIGNED_INT, table); ERR("get proxy histogram");
    pglGetHistogram(GL_HISTOGRAM, GL_FALSE, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, table); ERR("get histogram depth");
    pglResetHistogram(GL_PROXY_HISTOGRAM); ERR("reset proxy histogram");
    pglGetHistogramParameteriv(GL_HISTOGRAM, GL_MINMAX_SINK, v); ERR("histogram bad pname");
    pglMinmax(GL_HISTOGRAM, GL_RGBA, GL_FALSE); ERR("minmax bad target");
    pglMinmax(GL_MINMAX, GL_INTENSITY, GL_FALSE); ERR("minmax intensity");
    pglMinmax(GL_MINMAX, 3, GL_FALSE); ERR("minmax internal 3");
    pglGetMinmax(GL_MINMAX, GL_FALSE, GL_COLOR_INDEX, GL_FLOAT, table); ERR("get minmax index");
    pglGetMinmaxParameteriv(GL_MINMAX, GL_HISTOGRAM_SINK, v); ERR("minmax bad pname");
    pglResetMinmax(GL_HISTOGRAM); ERR("reset minmax bad target");
    pglBegin(GL_POINTS); pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table); pglEnd();
    ERR("table in begin");
    pglBegin(GL_POINTS); pglHistogram(GL_HISTOGRAM, 4, GL_RGBA, GL_FALSE); pglEnd(); ERR("histogram in begin");
    pglBegin(GL_POINTS); pglResetMinmax(GL_MINMAX); pglEnd(); ERR("reset minmax in begin");
    while (pglGetError()) {}

    /* ---- colour table storage ---- */
    static const GLenum table_formats[] = {GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_INTENSITY, GL_RGB, GL_RGBA,
                                           GL_ALPHA4, GL_LUMINANCE12, GL_R3_G3_B2, GL_RGB5_A1, GL_RGBA16, 1, 2, 3, 4};
    for (size_t i = 0; i < sizeof table_formats / sizeof *table_formats; ++i) {
        char what[64];
        snprintf(what, sizeof what, "table %#x", table_formats[i]);
        pglColorTable(GL_COLOR_TABLE, table_formats[i], 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 40);
        table_parameters(what, GL_COLOR_TABLE);
        GLfloat stored[4 * 4];
        for (int k = 0; k < 16; ++k) stored[k] = -7;
        pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored);
        floats(what, stored, 16);
    }
    /* Table data in other formats and types. */
    const GLfloat ftable[8] = {0.25f, -0.5f, 1.5f, 0.75f, 0.125f, 0.5f, 0.375f, 0.625f};
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 2, GL_RGBA, GL_FLOAT, ftable);
    GLfloat stored[64];
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("float table", stored, 8);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_LUMINANCE_ALPHA, GL_FLOAT, ftable);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("la float table", stored, 16);
    pglColorTable(GL_COLOR_TABLE, GL_RGB, 4, GL_BGRA, GL_UNSIGNED_BYTE, table);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("bgra table", stored, 16);
    GLubyte back[256 * 4];
    memset(back, 0xEE, sizeof back);
    pglGetColorTable(GL_COLOR_TABLE, GL_LUMINANCE, GL_UNSIGNED_BYTE, back); bytes("get table luminance", back, 1);
    memset(back, 0xEE, sizeof back);
    pglGetColorTable(GL_COLOR_TABLE, GL_BGR, GL_UNSIGNED_BYTE, back); bytes("get table bgr", back, 3);
    ERR("table formats");
    /* Scale and bias at specification. */
    const GLfloat tscale[4] = {0.5f, 2, 1, -1}, tbias[4] = {0.25f, 0, -0.5f, 1};
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, tscale);
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_BIAS, tbias);
    table_parameters("scaled table", GL_COLOR_TABLE);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 100);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("scaled table", stored, 16);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 2, GL_RGBA, GL_FLOAT, ftable);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("scaled float table", stored, 8);
    table_parameters("post convolution unscaled", GL_POST_CONVOLUTION_COLOR_TABLE);
    const GLint iscale[4] = {1, 2, 3, 4};
    pglColorTableParameteriv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, iscale);
    table_parameters("integer scale", GL_COLOR_TABLE);
    const GLfloat one[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 0};
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, one);
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_BIAS, zero);
    /* Pixel transfer at specification. */
    pglPixelTransferf(GL_RED_SCALE, 0.5f);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 100);
    pglPixelTransferf(GL_RED_SCALE, 1);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("table with red scale", stored, 16);
    pglPixelTransferf(GL_RED_SCALE, 0.5f);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_FLOAT, stored); floats("get table with red scale", stored, 16);
    pglPixelTransferf(GL_RED_SCALE, 1);
    /* Sub tables. */
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 8, GL_RGBA, GL_UNSIGNED_BYTE, table);
    pglColorSubTable(GL_COLOR_TABLE, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, table + 200);
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("subtable", back, 8);
    ERR("subtable");
    /* Copies from the framebuffer. */
    clear();
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglCopyColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_RGB, 1, 2, 4);
    table_parameters("copied table", GL_POST_COLOR_MATRIX_COLOR_TABLE);
    pglGetColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied table", back, 4);
    pglCopyColorSubTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, 1, 0, 5, 2);
    pglGetColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied subtable", back, 4);
    pglPixelTransferf(GL_GREEN_BIAS, -0.25f);
    pglCopyColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_RGBA, 0, 3, 4);
    pglPixelTransferf(GL_GREEN_BIAS, 0);
    pglGetColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied biased table", back, 4);
    ERR("copy table");

    /* ---- colour table lookups ---- */
    static const GLenum lookup_formats[] = {GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_INTENSITY, GL_RGB, GL_RGBA};
    for (size_t i = 0; i < sizeof lookup_formats / sizeof *lookup_formats; ++i) {
        char what[64];
        clear();
        pglColorTable(GL_COLOR_TABLE, lookup_formats[i], 16, GL_RGBA, GL_UNSIGNED_BYTE, table + 12);
        pglEnable(GL_COLOR_TABLE);
        draw(0);
        pglDisable(GL_COLOR_TABLE);
        snprintf(what, sizeof what, "lookup %#x", lookup_formats[i]);
        row(what, 0, 8);
    }
    for (int width = 1; width <= 256; width *= 4) {
        char what[64];
        clear();
        pglColorTable(GL_COLOR_TABLE, GL_RGBA, width, GL_RGBA, GL_UNSIGNED_BYTE, table);
        pglEnable(GL_COLOR_TABLE);
        draw(0);
        pglDisable(GL_COLOR_TABLE);
        snprintf(what, sizeof what, "lookup width %d", width);
        row(what, 0, 8);
    }
    /* Out of range values clamp to the table's ends. */
    clear();
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglPixelTransferf(GL_RED_SCALE, 3);
    pglPixelTransferf(GL_GREEN_BIAS, -0.5f);
    pglEnable(GL_COLOR_TABLE);
    draw(0);
    reset();
    row("lookup clamped", 0, 8);
    /* All three tables. */
    clear();
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 8, GL_RGBA, GL_UNSIGNED_BYTE, table + 4);
    pglColorTable(GL_POST_CONVOLUTION_COLOR_TABLE, GL_RGB, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 64);
    pglColorTable(GL_POST_COLOR_MATRIX_COLOR_TABLE, GL_LUMINANCE, 2, GL_RGBA, GL_UNSIGNED_BYTE, table + 128);
    pglEnable(GL_COLOR_TABLE); draw(0); pglDisable(GL_COLOR_TABLE);
    pglEnable(GL_POST_CONVOLUTION_COLOR_TABLE); draw(1); pglDisable(GL_POST_CONVOLUTION_COLOR_TABLE);
    pglEnable(GL_POST_COLOR_MATRIX_COLOR_TABLE); draw(2); pglDisable(GL_POST_COLOR_MATRIX_COLOR_TABLE);
    pglEnable(GL_COLOR_TABLE); pglEnable(GL_POST_CONVOLUTION_COLOR_TABLE); pglEnable(GL_POST_COLOR_MATRIX_COLOR_TABLE);
    draw(3);
    reset();
    row("table only", 0, 8);
    row("post convolution only", 1, 8);
    row("post color matrix only", 2, 8);
    row("all tables", 3, 8);
    /* A float colour table's values out of [0, 1]. */
    clear();
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 2, GL_RGBA, GL_FLOAT, ftable);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_RED_SCALE, 0.5f);
    pglEnable(GL_COLOR_TABLE);
    draw(0);
    reset();
    row("float table lookup", 0, 8);
    ERR("lookups");

    /* ---- colour matrix ---- */
    const GLfloat swap[16] = {0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0.5f, 0, 0.25f, 0, 0, 1};
    clear();
    pglMatrixMode(GL_COLOR);
    pglLoadMatrixf(swap);
    pglMatrixMode(GL_MODELVIEW);
    draw(0);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_RED_SCALE, 0.5f);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_BLUE_BIAS, 0.25f);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_ALPHA_SCALE, -1);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_ALPHA_BIAS, 1);
    draw(1);
    reset();
    pglPixelTransferf(GL_POST_COLOR_MATRIX_GREEN_SCALE, 0.5f);
    draw(2);
    reset();
    row("color matrix", 0, 8);
    row("color matrix scale bias", 1, 8);
    row("post matrix scale only", 2, 8);
    pglMatrixMode(GL_COLOR);
    pglPushMatrix();
    pglLoadMatrixf(swap);
    pglGetIntegerv(GL_COLOR_MATRIX_STACK_DEPTH, v); printf("color matrix stack pushed %d\n", v[0]);
    pglPopMatrix();
    pglMatrixMode(GL_MODELVIEW);
    ERR("color matrix");

    /* ---- convolution ---- */
    const GLfloat filter3[3 * 4] = {0.25f, 0.5f, 0, 1, 0.5f, 0.25f, 1, 0, 0.25f, 0.25f, 0, 0};
    const GLfloat border[4] = {1, 0.5f, 0.25f, 1};
    static const GLenum modes[] = {GL_REDUCE, GL_CONSTANT_BORDER, GL_REPLICATE_BORDER};
    for (int m = 0; m < 3; ++m) {
        char what[64];
        clear();
        pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 3, GL_RGBA, GL_FLOAT, filter3);
        pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, (GLint)modes[m]);
        pglConvolutionParameterfv(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_COLOR, border);
        pglEnable(GL_CONVOLUTION_1D);
        draw(0);
        pglWindowPos2i(0, 4);
        pglDrawPixels(8, 4, GL_RGBA, GL_UNSIGNED_BYTE, image);
        reset();
        snprintf(what, sizeof what, "convolve 1d mode %#x", modes[m]);
        row(what, 0, 9);
        for (int y = 4; y < 9; ++y) row(what, y, 9);
        convolution_parameters(what, GL_CONVOLUTION_1D);
    }
    pglGetConvolutionFilter(GL_CONVOLUTION_1D, GL_RGBA, GL_FLOAT, stored); floats("filter 1d", stored, 12);
    /* Filter scale and bias, internal formats. */
    const GLfloat fscale[4] = {2, 1, 0.5f, 1}, fbias[4] = {0, 0.125f, 0, -0.25f};
    pglConvolutionParameterfv(GL_CONVOLUTION_1D, GL_CONVOLUTION_FILTER_SCALE, fscale);
    pglConvolutionParameterfv(GL_CONVOLUTION_1D, GL_CONVOLUTION_FILTER_BIAS, fbias);
    static const GLenum filter_formats[] = {GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_INTENSITY, GL_RGB, GL_RGBA};
    for (size_t i = 0; i < sizeof filter_formats / sizeof *filter_formats; ++i) {
        char what[64];
        pglConvolutionFilter1D(GL_CONVOLUTION_1D, filter_formats[i], 3, GL_RGBA, GL_FLOAT, filter3);
        snprintf(what, sizeof what, "filter %#x", filter_formats[i]);
        convolution_parameters(what, GL_CONVOLUTION_1D);
        for (int k = 0; k < 12; ++k) stored[k] = -7;
        pglGetConvolutionFilter(GL_CONVOLUTION_1D, GL_RGBA, GL_FLOAT, stored);
        floats(what, stored, 12);
        clear();
        pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REPLICATE_BORDER);
        pglEnable(GL_CONVOLUTION_1D);
        draw(0);
        reset();
        row(what, 0, 8);
    }
    pglConvolutionParameterfv(GL_CONVOLUTION_1D, GL_CONVOLUTION_FILTER_SCALE, one);
    pglConvolutionParameterfv(GL_CONVOLUTION_1D, GL_CONVOLUTION_FILTER_BIAS, zero);
    /* 2D and separable filters. */
    GLfloat filter2d[3 * 3 * 4];
    for (int i = 0; i < 9; ++i)
        for (int c = 0; c < 4; ++c) filter2d[i * 4 + c] = (float)((i * 3 + c * 5) % 7) / 16.0f - (c == 3 ? 0.1f : 0);
    for (int m = 0; m < 3; ++m) {
        char what[64];
        clear();
        pglConvolutionFilter2D(GL_CONVOLUTION_2D, GL_RGBA, 3, 3, GL_RGBA, GL_FLOAT, filter2d);
        pglConvolutionParameteri(GL_CONVOLUTION_2D, GL_CONVOLUTION_BORDER_MODE, (GLint)modes[m]);
        pglConvolutionParameterfv(GL_CONVOLUTION_2D, GL_CONVOLUTION_BORDER_COLOR, border);
        pglEnable(GL_CONVOLUTION_2D);
        pglWindowPos2i(1, 1);
        pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
        reset();
        snprintf(what, sizeof what, "convolve 2d mode %#x", modes[m]);
        for (int y = 0; y < 10; ++y) row(what, y, 10);
    }
    convolution_parameters("convolution 2d", GL_CONVOLUTION_2D);
    pglGetConvolutionFilter(GL_CONVOLUTION_2D, GL_RGBA, GL_FLOAT, stored); floats("filter 2d", stored, 36);
    pglConvolutionFilter2D(GL_CONVOLUTION_2D, GL_LUMINANCE, 2, 3, GL_RGBA, GL_FLOAT, filter2d);
    convolution_parameters("luminance 2d", GL_CONVOLUTION_2D);
    pglGetConvolutionFilter(GL_CONVOLUTION_2D, GL_LUMINANCE, GL_FLOAT, stored); floats("luminance 2d", stored, 6);
    const GLfloat rows3[3 * 4] = {0.5f, 0.25f, 1, 0.5f, 0.25f, 0.5f, 0, 0.25f, 0.25f, 0.25f, 0, 0.25f};
    const GLfloat cols3[3 * 4] = {0.25f, 1, 0.5f, 1, 0.5f, 0, 0.25f, 1, 0.25f, 0, 0.25f, 0};
    for (int m = 0; m < 3; ++m) {
        char what[64];
        clear();
        pglSeparableFilter2D(GL_SEPARABLE_2D, GL_RGBA, 3, 3, GL_RGBA, GL_FLOAT, rows3, cols3);
        pglConvolutionParameteri(GL_SEPARABLE_2D, GL_CONVOLUTION_BORDER_MODE, (GLint)modes[m]);
        pglConvolutionParameterfv(GL_SEPARABLE_2D, GL_CONVOLUTION_BORDER_COLOR, border);
        pglEnable(GL_SEPARABLE_2D);
        pglWindowPos2i(1, 1);
        pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
        reset();
        snprintf(what, sizeof what, "separable mode %#x", modes[m]);
        for (int y = 0; y < 10; ++y) row(what, y, 10);
    }
    convolution_parameters("separable", GL_SEPARABLE_2D);
    GLfloat rows_back[12], cols_back[12];
    for (int k = 0; k < 12; ++k) rows_back[k] = cols_back[k] = -7;
    pglGetSeparableFilter(GL_SEPARABLE_2D, GL_RGBA, GL_FLOAT, rows_back, cols_back, NULL);
    floats("separable rows", rows_back, 12);
    floats("separable columns", cols_back, 12);
    /* 2D over separable: both enabled. */
    clear();
    pglEnable(GL_CONVOLUTION_2D);
    pglEnable(GL_SEPARABLE_2D);
    pglWindowPos2i(1, 1);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    reset();
    for (int y = 0; y < 3; ++y) row("2d and separable", y, 10);
    /* 1D filters on 2D images; post-convolution scale and bias. */
    clear();
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 3, GL_RGBA, GL_FLOAT, filter3);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_1D);
    pglEnable(GL_CONVOLUTION_2D);
    pglPixelTransferf(GL_POST_CONVOLUTION_RED_SCALE, 0.5f);
    pglPixelTransferf(GL_POST_CONVOLUTION_GREEN_BIAS, 0.25f);
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 3, GL_RGBA, GL_UNSIGNED_BYTE, image);
    reset();
    for (int y = 0; y < 3; ++y) row("1d and 2d enabled", y, 9);
    clear();
    pglPixelTransferf(GL_POST_CONVOLUTION_RED_SCALE, 0.5f);
    pglPixelTransferf(GL_POST_CONVOLUTION_GREEN_BIAS, 0.25f);
    draw(0);
    reset();
    row("post convolution scale without filter", 0, 8);
    /* Copied filters. */
    clear();
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglCopyConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 2, 3, 5);
    convolution_parameters("copied 1d", GL_CONVOLUTION_1D);
    pglGetConvolutionFilter(GL_CONVOLUTION_1D, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied 1d", back, 5);
    pglCopyConvolutionFilter2D(GL_CONVOLUTION_2D, GL_LUMINANCE_ALPHA, 1, 1, 3, 2);
    convolution_parameters("copied 2d", GL_CONVOLUTION_2D);
    pglGetConvolutionFilter(GL_CONVOLUTION_2D, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied 2d", back, 6);
    ERR("convolution");

    /* ---- histogram ---- */
    clear();
    pglHistogram(GL_HISTOGRAM, 8, GL_RGBA, GL_FALSE);
    histogram_parameters("histogram 8", GL_HISTOGRAM);
    histogram("empty histogram", 8);
    pglEnable(GL_HISTOGRAM);
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglDisable(GL_HISTOGRAM);
    histogram("histogram draw", 8);
    row("histogram drawn", 0, 8);
    pglEnable(GL_HISTOGRAM);
    GLubyte read[8 * 8 * 4];
    pglReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, read);
    pglDisable(GL_HISTOGRAM);
    histogram("histogram draw and read", 8);
    GLushort hs[32];
    pglGetHistogram(GL_HISTOGRAM, GL_TRUE, GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT, hs);
    printf("histogram luminance alpha:");
    for (int i = 0; i < 16; ++i) printf(" %u", hs[i]);
    printf("\n");
    histogram("histogram after reset", 8);
    GLfloat hf[32];
    pglHistogram(GL_HISTOGRAM, 4, GL_LUMINANCE, GL_TRUE);
    histogram_parameters("luminance histogram", GL_HISTOGRAM);
    clear();
    pglEnable(GL_HISTOGRAM);
    draw(0);
    pglDisable(GL_HISTOGRAM);
    row("histogram sink", 0, 8);
    histogram("luminance histogram", 4);
    pglGetHistogram(GL_HISTOGRAM, GL_FALSE, GL_RGB, GL_FLOAT, hf); floats("luminance histogram rgb float", hf, 12);
    pglResetHistogram(GL_HISTOGRAM);
    histogram("reset histogram", 4);
    pglHistogram(GL_HISTOGRAM, 2, GL_ALPHA8, GL_FALSE);
    pglEnable(GL_HISTOGRAM);
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglDisable(GL_HISTOGRAM);
    histogram("alpha histogram", 2);
    histogram_parameters("alpha histogram", GL_HISTOGRAM);
    ERR("histogram");

    /* ---- minmax ---- */
    clear();
    pglMinmax(GL_MINMAX, GL_RGBA, GL_FALSE);
    minmax("empty minmax");
    pglEnable(GL_MINMAX);
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglDisable(GL_MINMAX);
    minmax("minmax draw");
    row("minmax drawn", 0, 8);
    GLubyte mb[8];
    pglGetMinmax(GL_MINMAX, GL_TRUE, GL_RGBA, GL_UNSIGNED_BYTE, mb); bytes("minmax bytes", mb, 2);
    minmax("minmax after reset");
    pglMinmax(GL_MINMAX, GL_LUMINANCE, GL_TRUE);
    pglGetMinmaxParameteriv(GL_MINMAX, GL_MINMAX_FORMAT, v); printf("minmax format %#x\n", v[0]);
    pglGetMinmaxParameteriv(GL_MINMAX, GL_MINMAX_SINK, v); printf("minmax sink %d\n", v[0]);
    clear();
    pglEnable(GL_MINMAX);
    draw(0);
    pglDisable(GL_MINMAX);
    row("minmax sink", 0, 8);
    minmax("luminance minmax");
    pglResetMinmax(GL_MINMAX);
    minmax("reset minmax");
    /* Histogram sink stops minmax; histogram and minmax together. */
    pglMinmax(GL_MINMAX, GL_RGBA, GL_FALSE);
    pglHistogram(GL_HISTOGRAM, 2, GL_RGBA, GL_TRUE);
    clear();
    pglEnable(GL_MINMAX);
    pglEnable(GL_HISTOGRAM);
    draw(0);
    pglDisable(GL_MINMAX);
    pglDisable(GL_HISTOGRAM);
    minmax("minmax after histogram sink");
    histogram("histogram before minmax", 2);
    ERR("minmax");

    /* ---- the read paths ---- */
    clear();
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    GLubyte out[16 * 4];
    pglPixelTransferf(GL_RED_SCALE, 0.5f);
    pglPixelTransferf(GL_BLUE_BIAS, 0.25f);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read scale bias", out, 8);
    reset();
    GLfloat map[4] = {1, 0.25f, 0.5f, 0};
    pglPixelMapfv(GL_PIXEL_MAP_G_TO_G, 4, map);
    pglPixelTransferf(GL_MAP_COLOR, 1);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read map color", out, 8);
    reset();
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglEnable(GL_COLOR_TABLE);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read color table", out, 8);
    GLfloat fout[8 * 4];
    for (int i = 0; i < 32; ++i) fout[i] = -7;
    pglReadPixels(0, 0, 2, 1, GL_RGBA, GL_FLOAT, fout); floats("read color table float", fout, 8);
    reset();
    pglMatrixMode(GL_COLOR);
    pglLoadMatrixf(swap);
    pglMatrixMode(GL_MODELVIEW);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read color matrix", out, 8);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, out); bytes("read color matrix luminance", out, 2);
    reset();
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 3, GL_RGBA, GL_FLOAT, filter3);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_1D);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read convolve reduce", out, 8);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REPLICATE_BORDER);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read convolve replicate", out, 8);
    reset();
    pglConvolutionFilter2D(GL_CONVOLUTION_2D, GL_RGBA, 3, 3, GL_RGBA, GL_FLOAT, filter2d);
    pglConvolutionParameteri(GL_CONVOLUTION_2D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_2D);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read convolve 2d reduce", out, 16);
    reset();
    pglHistogram(GL_HISTOGRAM, 2, GL_RGBA, GL_TRUE);
    pglEnable(GL_HISTOGRAM);
    memset(out, 0xEE, sizeof out);
    pglReadPixels(0, 0, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, out); bytes("read histogram sink", out, 8);
    reset();
    histogram("read histogram", 2);
    /* Depth reads skip the imaging operations. */
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglEnable(GL_COLOR_TABLE);
    pglReadPixels(0, 0, 1, 1, GL_RGBA_INTEGER_EXT, GL_UNSIGNED_BYTE, out); ERR("read integer with table");
    reset();
    /* glCopyPixels: transfer and imaging once. */
    clear();
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglPixelTransferf(GL_RED_SCALE, 0.5f);
    pglPixelTransferf(GL_BLUE_BIAS, 0.25f);
    pglWindowPos2i(0, 10);
    pglCopyPixels(0, 0, 8, 1, GL_COLOR);
    reset();
    row("copy scale bias", 10, 8);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglEnable(GL_COLOR_TABLE);
    pglWindowPos2i(0, 11);
    pglCopyPixels(0, 0, 8, 1, GL_COLOR);
    reset();
    row("copy color table", 11, 8);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_1D);
    pglWindowPos2i(0, 12);
    pglCopyPixels(0, 0, 8, 1, GL_COLOR);
    reset();
    row("copy convolve", 12, 8);
    pglMatrixMode(GL_COLOR);
    pglLoadMatrixf(swap);
    pglMatrixMode(GL_MODELVIEW);
    pglWindowPos2i(0, 13);
    pglCopyPixels(0, 0, 8, 1, GL_COLOR);
    reset();
    row("copy color matrix", 13, 8);
    ERR("read paths");

    /* ---- textures ---- */
    GLuint textures[5];
    pglGenTextures(5, textures);
    GLint tw = -7, th = -7;
    pglBindTexture(GL_TEXTURE_2D, textures[0]);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("texture plain", back, 8);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglEnable(GL_COLOR_TABLE);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    ERR("teximage color table");
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("texture color table", back, 8);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglEnable(GL_COLOR_TABLE);
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("get texture with table", back, 8);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, image + 64);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("subtexture color table", back, 8);
    pglPixelTransferf(GL_RED_SCALE, 0.5f);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("texture and get with red scale", back, 8);
    reset();
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, 8, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglMatrixMode(GL_COLOR);
    pglLoadMatrixf(swap);
    pglMatrixMode(GL_MODELVIEW);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("luminance texture color matrix", back, 8);
    /* Convolution changes the image's size. */
    pglConvolutionFilter2D(GL_CONVOLUTION_2D, GL_RGBA, 3, 3, GL_RGBA, GL_FLOAT, filter2d);
    pglConvolutionParameteri(GL_CONVOLUTION_2D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_2D);
    pglBindTexture(GL_TEXTURE_2D, textures[1]);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 6, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    ERR("teximage convolve");
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    printf("convolved texture %d x %d\n", tw, th);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("convolved texture", back, 16);
    pglConvolutionParameteri(GL_CONVOLUTION_2D, GL_CONVOLUTION_BORDER_MODE, GL_CONSTANT_BORDER);
    pglEnable(GL_CONVOLUTION_2D);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, image + 64);
    ERR("subimage convolve");
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("convolved subtexture", back, 16);
    pglConvolutionFilter1D(GL_CONVOLUTION_1D, GL_RGBA, 3, GL_RGBA, GL_FLOAT, filter3);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglEnable(GL_CONVOLUTION_1D);
    pglBindTexture(GL_TEXTURE_1D, textures[2]);
    pglTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 10, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &tw);
    printf("convolved 1d texture %d\n", tw);
    ERR("teximage1d convolve");
    pglBindTexture(GL_TEXTURE_2D, textures[3]);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 6, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    printf("2d texture with 1d filter %d x %d\n", tw, th);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("convolved 1d texture", back, 8);
    ERR("textures");
    /* 3D textures: tables, no convolution. */
    pglBindTexture(GL_TEXTURE_3D, textures[4]);
    pglEnable(GL_COLOR_TABLE);
    pglEnable(GL_CONVOLUTION_2D);
    pglTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    reset();
    pglGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_WIDTH, &tw);
    printf("3d texture width %d\n", tw);
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("3d texture", back, 8);
    ERR("3d texture");
    /* Copies into textures. */
    clear();
    pglWindowPos2i(0, 0);
    pglDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, image);
    pglBindTexture(GL_TEXTURE_2D, textures[0]);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table + 20);
    pglEnable(GL_COLOR_TABLE);
    pglPixelTransferf(GL_GREEN_SCALE, 0.5f);
    pglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 8, 2, 0);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied texture", back, 8);
    pglPixelTransferf(GL_GREEN_SCALE, 0.5f);
    pglPixelTransferf(GL_BLUE_BIAS, 0.25f);
    pglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 8, 2, 0);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied texture scale bias", back, 8);
    pglMatrixMode(GL_COLOR);
    pglLoadMatrixf(swap);
    pglMatrixMode(GL_MODELVIEW);
    pglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 4, 0, 0, 0, 4, 1);
    reset();
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied subtexture", back, 8);
    pglEnable(GL_CONVOLUTION_2D);
    pglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 6, 6, 0);
    reset();
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    printf("copied convolved texture %d x %d\n", tw, th);
    memset(back, 0xEE, sizeof back);
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("copied convolved texture", back, 8);
    pglDeleteTextures(5, textures);
    ERR("copied textures");

    /* ---- state: attributes and display lists ---- */
    pglEnable(GL_COLOR_TABLE);
    pglEnable(GL_HISTOGRAM);
    pglPixelTransferf(GL_POST_CONVOLUTION_BLUE_SCALE, 3);
    pglPixelTransferf(GL_POST_COLOR_MATRIX_ALPHA_BIAS, 0.5f);
    pglPushAttrib(GL_PIXEL_MODE_BIT);
    reset();
    pglPopAttrib();
    printf("pixel mode bit:");
    for (size_t i = 0; i < sizeof enables / sizeof *enables; ++i) printf(" %d", pglIsEnabled(enables[i]));
    GLfloat f = -7;
    pglGetFloatv(GL_POST_CONVOLUTION_BLUE_SCALE, &f); printf(" %g", f);
    pglGetFloatv(GL_POST_COLOR_MATRIX_ALPHA_BIAS, &f); printf(" %g\n", f);
    pglPushAttrib(GL_ENABLE_BIT);
    reset();
    pglPopAttrib();
    printf("enable bit:");
    for (size_t i = 0; i < sizeof enables / sizeof *enables; ++i) printf(" %d", pglIsEnabled(enables[i]));
    pglGetFloatv(GL_POST_CONVOLUTION_BLUE_SCALE, &f); printf(" %g\n", f);
    reset();
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, tscale);
    pglPushAttrib(GL_PIXEL_MODE_BIT);
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, one);
    pglPopAttrib();
    table_parameters("table scale after pop", GL_COLOR_TABLE);
    pglColorTableParameterfv(GL_COLOR_TABLE, GL_COLOR_TABLE_SCALE, one);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_CONSTANT_BORDER);
    pglPushAttrib(GL_PIXEL_MODE_BIT);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REDUCE);
    pglPopAttrib();
    convolution_parameters("convolution after pop", GL_CONVOLUTION_1D);
    ERR("attributes");
    GLuint list = pglGenLists(1);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, table);
    GLubyte listed[16];
    memcpy(listed, table + 40, sizeof listed);
    pglNewList(list, GL_COMPILE);
    pglColorTable(GL_COLOR_TABLE, GL_RGBA, 4, GL_RGBA, GL_UNSIGNED_BYTE, listed);
    pglColorTable(GL_PROXY_COLOR_TABLE, GL_RGBA, 64, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pglHistogram(GL_HISTOGRAM, 16, GL_RGB, GL_FALSE);
    pglConvolutionParameteri(GL_CONVOLUTION_1D, GL_CONVOLUTION_BORDER_MODE, GL_REPLICATE_BORDER);
    pglEndList();
    ERR("compile list");
    table_parameters("table after compile", GL_PROXY_COLOR_TABLE);
    histogram_parameters("histogram after compile", GL_HISTOGRAM);
    memset(listed, 0, sizeof listed);
    pglCallList(list);
    ERR("call list");
    pglGetColorTable(GL_COLOR_TABLE, GL_RGBA, GL_UNSIGNED_BYTE, back); bytes("listed table", back, 4);
    histogram_parameters("listed histogram", GL_HISTOGRAM);
    convolution_parameters("listed convolution", GL_CONVOLUTION_1D);
    ERR("end");
    return 0;
}
