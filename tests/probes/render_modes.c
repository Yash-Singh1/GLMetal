/* Selection and feedback render modes (GL 2.1 5.2, 5.3) as Apple's OpenGL
   answers them: hit records (name stack, depth range, clipping, culling,
   raster positions, overflow; depths to 24 bits), feedback tokens for every
   vertex type and primitive (clipping, polygon modes, lighting, bitmaps,
   pixel rectangles, pass-through), and the errors. make test-render-modes
   diffs a run on Apple's against one on GLMetal.

   render_modes [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

static void *lib;

/* Hit records with depths shown to 24 bits: their lowest byte is where
   Apple's scaling (24-bit depth expanded to 32) and GLMetal's (z * (2^32 -
   1)) differ. Past the records, the untouched buffer words. */
static void print_hits(const char *what, GLint hits, const GLuint *buffer, int size)
{
    printf("%s: hits %d:", what, hits);
    int i = 0;
    for (int record = 0; i < size && (hits < 0 || record < hits); ++record) {
        int names = (int)buffer[i];
        printf(" [%d", names);
        if (i + 1 < size) printf(" %06x", buffer[i + 1] >> 8);
        if (i + 2 < size) printf(" %06x", buffer[i + 2] >> 8);
        for (int k = 0; k < names && i + 3 + k < size; ++k) printf(" %u", buffer[i + 3 + k]);
        printf("]");
        i += 3 + names;
        if (hits < 0 && i >= size) break;
    }
    for (; i < size; ++i) printf(" %u", buffer[i]);
    printf("\n");
}

static void print_feedback(const char *what, GLint count, const GLfloat *buffer)
{
    printf("%s: values %d:", what, count);
    for (int i = 0; i < count && i < 512; ++i) printf(" %.4g", buffer[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glGetError); F(glGetIntegerv); F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT);
    F(glBindRenderbufferEXT); F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport);
    F(glMatrixMode); F(glLoadIdentity); F(glOrtho); F(glSelectBuffer); F(glRenderMode); F(glInitNames);
    F(glPushName); F(glPopName); F(glLoadName); F(glBegin); F(glEnd); F(glVertex3f); F(glVertex2f); F(glColor4f);
    F(glTexCoord4f); F(glRasterPos2f); F(glFeedbackBuffer); F(glPassThrough); F(glBitmap); F(glDrawPixels);
    F(glCopyPixels); F(glEnable); F(glDisable); F(glCullFace); F(glPolygonMode); F(glDepthRange); F(glLightfv);
    F(glMaterialfv); F(glNormal3f); F(glClipPlane); F(glPointSize); F(glDrawArrays); F(glVertexPointer);
    F(glEnableClientState); F(glDisableClientState); F(glEdgeFlag); F(glShadeModel); F(glPushAttrib); F(glPopAttrib);
    F(glGetPointerv); F(glFrontFace); F(glTranslatef);

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
    pglOrtho(0, 100, 0, 100, -1, 1);
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    GLint v;
    pglGetIntegerv(GL_MAX_NAME_STACK_DEPTH, &v);
    printf("max name stack depth %d\n", v);
    pglGetIntegerv(GL_RENDER_MODE, &v);
    printf("render mode %#x\n", v);

    /* ---- selection ---- */
    GLuint select[64];
    memset(select, 0xEE, sizeof select);
    pglSelectBuffer(64, select);
    printf("select buffer error %#x\n", pglGetError());
    GLint hits = pglRenderMode(GL_SELECT);
    printf("enter select: returned %d error %#x\n", hits, pglGetError());
    pglGetIntegerv(GL_RENDER_MODE, &v);
    printf("render mode %#x\n", v);
    pglLoadName(9);
    printf("load name on empty stack: error %#x\n", pglGetError());
    pglInitNames();
    pglPushName(1);
    pglBegin(GL_TRIANGLES);                       /* inside, z 0.5 and -0.25 */
    pglVertex3f(10, 10, -0.5f); pglVertex3f(30, 10, 0.25f); pglVertex3f(10, 30, 0);
    pglEnd();
    pglLoadName(2);
    pglBegin(GL_TRIANGLES);                       /* outside: no hit */
    pglVertex3f(200, 200, 0); pglVertex3f(230, 200, 0); pglVertex3f(200, 230, 0);
    pglEnd();
    pglLoadName(3);
    pglBegin(GL_LINES);                           /* crosses the right edge: clipped depth */
    pglVertex3f(90, 50, -1); pglVertex3f(110, 50, 1);
    pglEnd();
    pglPushName(4);
    pglBegin(GL_POINTS);
    pglVertex3f(50, 50, 0.75f);
    pglEnd();
    pglPopName();
    pglLoadName(5);
    pglRasterPos2f(60, 60);                       /* a valid raster position is a hit */
    pglLoadName(6);
    pglEnable(GL_CULL_FACE);
    pglBegin(GL_TRIANGLES);                       /* clockwise: culled, no hit */
    pglVertex3f(10, 10, 0); pglVertex3f(10, 30, 0); pglVertex3f(30, 10, 0);
    pglEnd();
    pglDisable(GL_CULL_FACE);
    pglLoadName(7);
    pglDepthRange(0.25, 0.75);
    pglBegin(GL_QUADS);
    pglVertex3f(10, 10, -1); pglVertex3f(20, 10, -1); pglVertex3f(20, 20, 1); pglVertex3f(10, 20, 1);
    pglEnd();
    pglDepthRange(0, 1);
    pglLoadName(8);
    static const GLfloat array[] = {40, 40, 50, 40, 40, 50};
    pglVertexPointer(2, GL_FLOAT, 0, array);
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglDrawArrays(GL_TRIANGLES, 0, 3);
    pglDisableClientState(GL_VERTEX_ARRAY);
    pglPopName();
    pglPopName();
    printf("pop empty stack: error %#x\n", pglGetError());
    for (int i = 0; i < 70; ++i) pglPushName((GLuint)i);
    printf("push past the depth: error %#x\n", pglGetError());
    pglGetIntegerv(GL_NAME_STACK_DEPTH, &v);
    printf("name stack depth %d\n", v);
    pglInitNames();
    pglSelectBuffer(64, select);
    printf("select buffer while selecting: error %#x\n", pglGetError());
    hits = pglRenderMode(GL_RENDER);
    print_hits("selection", hits, select, 40);
    GLvoid *pointer = NULL;
    pglGetPointerv(GL_SELECTION_BUFFER_POINTER, &pointer);
    printf("selection buffer pointer %s\n", pointer == select ? "ok" : "wrong");
    pglGetIntegerv(GL_SELECTION_BUFFER_SIZE, &v);
    printf("selection buffer size %d\n", v);

    /* Overflow: hits that do not fit. */
    GLuint small[6];
    memset(small, 0xEE, sizeof small);
    pglSelectBuffer(6, small);
    pglRenderMode(GL_SELECT);
    pglInitNames();
    pglPushName(11);
    pglBegin(GL_POINTS); pglVertex2f(5, 5); pglEnd();
    pglLoadName(12);
    pglBegin(GL_POINTS); pglVertex2f(6, 6); pglEnd();
    pglLoadName(13);
    hits = pglRenderMode(GL_RENDER);
    print_hits("overflow", hits, small, 6);
    /* Name stack calls outside selection mode are ignored. */
    pglPushName(1);
    pglLoadName(2);
    printf("name calls in render mode: error %#x\n", pglGetError());

    /* ---- feedback ---- */
    static GLfloat feedback[1024];
    const GLenum types[] = {GL_2D, GL_3D, GL_3D_COLOR, GL_3D_COLOR_TEXTURE, GL_4D_COLOR_TEXTURE};
    for (int t = 0; t < 5; ++t) {
        memset(feedback, 0, sizeof feedback);
        pglFeedbackBuffer(1024, types[t], feedback);
        pglRenderMode(GL_FEEDBACK);
        pglColor4f(0.25f, 0.5f, 0.75f, 1);
        pglTexCoord4f(1, 2, 3, 4);
        pglBegin(GL_POINTS); pglVertex3f(10, 20, 0.5f); pglEnd();
        pglBegin(GL_LINE_STRIP); pglVertex2f(10, 10); pglVertex2f(20, 10); pglVertex2f(20, 20); pglEnd();
        pglBegin(GL_LINE_LOOP); pglVertex2f(30, 30); pglVertex2f(40, 30); pglVertex2f(40, 40); pglEnd();
        pglBegin(GL_TRIANGLES); pglVertex2f(10, 10); pglVertex2f(30, 10); pglVertex2f(10, 30); pglEnd();
        pglBegin(GL_QUADS); pglVertex2f(50, 50); pglVertex2f(60, 50); pglVertex2f(60, 60); pglVertex2f(50, 60); pglEnd();
        pglBegin(GL_POLYGON); pglVertex2f(70, 70); pglVertex2f(80, 70); pglVertex2f(85, 80); pglVertex2f(75, 85); pglVertex2f(65, 80); pglEnd();
        pglPassThrough(42);
        pglRasterPos2f(5, 5);
        static const GLubyte bits[8] = {0xff};
        pglBitmap(8, 1, 0, 0, 2, 0, bits);
        static const GLubyte pixel[4] = {1, 2, 3, 4};
        pglDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        pglCopyPixels(0, 0, 1, 1, GL_COLOR);
        GLint n = pglRenderMode(GL_RENDER);
        char what[32];
        snprintf(what, sizeof what, "feedback type %#x", types[t]);
        print_feedback(what, n, feedback);
    }
    /* Clipping, polygon modes, edge flags, culling, flat shading and lighting. */
    memset(feedback, 0, sizeof feedback);
    pglFeedbackBuffer(1024, GL_3D_COLOR, feedback);
    pglRenderMode(GL_FEEDBACK);
    pglColor4f(1, 0, 0, 1);
    pglBegin(GL_TRIANGLES); pglVertex2f(90, 10); pglVertex2f(120, 10); pglVertex2f(90, 40); pglEnd();
    pglBegin(GL_LINES); pglVertex2f(-10, 50); pglVertex2f(50, 50); pglEnd();
    pglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    pglBegin(GL_QUADS);
    pglEdgeFlag(GL_TRUE); pglVertex2f(10, 60); pglEdgeFlag(GL_FALSE); pglVertex2f(20, 60);
    pglEdgeFlag(GL_TRUE); pglVertex2f(20, 70); pglVertex2f(10, 70);
    pglEnd();
    pglPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    pglBegin(GL_TRIANGLES); pglVertex2f(30, 60); pglVertex2f(40, 60); pglVertex2f(30, 70); pglEnd();
    pglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    pglEnable(GL_CULL_FACE);
    pglBegin(GL_TRIANGLES); pglVertex2f(10, 10); pglVertex2f(10, 30); pglVertex2f(30, 10); pglEnd();
    pglDisable(GL_CULL_FACE);
    pglShadeModel(GL_FLAT);
    pglBegin(GL_TRIANGLES);
    pglColor4f(1, 0, 0, 1); pglVertex2f(50, 10); pglColor4f(0, 1, 0, 1); pglVertex2f(60, 10);
    pglColor4f(0, 0, 1, 1); pglVertex2f(50, 20);
    pglEnd();
    pglShadeModel(GL_SMOOTH);
    pglEnable(GL_LIGHTING);
    pglEnable(GL_LIGHT0);
    static const GLfloat diffuse[4] = {0.5f, 0.25f, 1, 1}, position[4] = {0, 0, 1, 0}, material[4] = {1, 1, 0.5f, 1};
    pglLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    pglLightfv(GL_LIGHT0, GL_POSITION, position);
    pglMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material);
    pglNormal3f(0, 0, 1);
    pglBegin(GL_POINTS); pglVertex2f(70, 70); pglEnd();
    pglDisable(GL_LIGHTING);
    pglPointSize(4);
    pglBegin(GL_POINTS); pglVertex2f(-5, 50); pglVertex2f(5, 50); pglEnd();
    pglPointSize(1);
    GLint n = pglRenderMode(GL_RENDER);
    print_feedback("feedback states", n, feedback);
    /* Overflow and errors. */
    GLfloat tiny[5];
    memset(tiny, 0, sizeof tiny);
    pglFeedbackBuffer(5, GL_2D, tiny);
    pglRenderMode(GL_FEEDBACK);
    pglBegin(GL_TRIANGLES); pglVertex2f(10, 10); pglVertex2f(30, 10); pglVertex2f(10, 30); pglEnd();
    n = pglRenderMode(GL_RENDER);
    print_feedback("feedback overflow", 5, tiny);
    printf("feedback overflow returned %d\n", n);
    pglFeedbackBuffer(-1, GL_2D, tiny);
    printf("negative feedback size: error %#x\n", pglGetError());
    pglFeedbackBuffer(5, GL_RGBA, tiny);
    printf("bad feedback type: error %#x\n", pglGetError());
    pglRenderMode(GL_FEEDBACK);
    pglFeedbackBuffer(5, GL_2D, tiny);
    printf("feedback buffer while in feedback: error %#x\n", pglGetError());
    pglRenderMode(GL_RENDER);
    pglRenderMode(0x1234);
    printf("bad render mode: error %#x\n", pglGetError());
    pglGetPointerv(GL_FEEDBACK_BUFFER_POINTER, &pointer);
    printf("feedback buffer pointer %s\n", pointer == tiny ? "ok" : "wrong");
    pglGetIntegerv(GL_FEEDBACK_BUFFER_SIZE, &v);
    printf("feedback buffer size %d\n", v);
    pglGetIntegerv(GL_FEEDBACK_BUFFER_TYPE, &v);
    printf("feedback buffer type %#x\n", v);
    printf("final error %#x\n", pglGetError());
    return 0;
}
