/* Aux colour buffers of the default framebuffer (AGL_AUX_BUFFERS /
   kCGLPFAAuxBuffers; Call of Duty 2's Direct3D wrapper renders into
   GL_AUX0/GL_AUX1), and accumulation buffer pixel formats.

   aux_buffers [provider.dylib]   (default: Apple's OpenGL)

   Prints pixel-format answers and draw/read buffer validation; the output
   for GLMetal must equal Apple's (make test-aux-buffers diffs them). With
   GLMetal it then also attaches a view that is never put in a window
   (GLMCGLSetView) and checks clears, draws, glReadPixels,
   glCopyTexSubImage2D, glCopyPixels, glBlitFramebuffer and glDrawBuffers
   on the aux images; those lines start with "glmetal:" and fail the run
   with a non-zero exit when an image holds the wrong colour. */
#define GL_SILENCE_DEPRECATION 1
#import <Cocoa/Cocoa.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

static int failures;

static void expect(const char *what, const unsigned char *p, int r, int g, int b)
{
    bool ok = p[0] == r && p[1] == g && p[2] == b;
    printf("glmetal: %s = %d %d %d %s\n", what, p[0], p[1], p[2], ok ? "ok" : "WRONG");
    failures += !ok;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    int (*set_view)(CGLContextObj, void *) = dlsym(lib, "GLMCGLSetView");
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(CGLDescribePixelFormat); F(CGLDestroyPixelFormat);
    F(glGetError); F(glGetIntegerv); F(glDrawBuffer); F(glReadBuffer); F(glDrawBuffers); F(glClearColor); F(glClear);
    F(glReadPixels); F(glGetFramebufferAttachmentParameteriv); F(glGenTextures); F(glBindTexture); F(glTexImage2D);
    F(glCopyTexSubImage2D); F(glGetTexImage); F(glFinish); F(glBegin); F(glEnd); F(glVertex2f); F(glColor4f);
    F(glCopyPixels); F(glRasterPos2f); F(glBlitFramebuffer); F(glViewport);
    F(glEnable); F(glDisable); F(glClearDepth); F(glDepthFunc);

    for (int core = 0; core < 2; ++core)
        for (int aux = -1; aux <= 4; ++aux) {
            CGLPixelFormatAttribute at[] = {kCGLPFAAccelerated, kCGLPFADoubleBuffer, kCGLPFAAuxBuffers, aux, kCGLPFAOpenGLProfile,
                                            (CGLPixelFormatAttribute)(core ? kCGLOGLPVersion_GL4_Core : kCGLOGLPVersion_Legacy), 0};
            CGLPixelFormatObj pf = NULL;
            GLint n = 0, v = -1;
            CGLError e = pCGLChoosePixelFormat(at, &pf, &n);
            if (pf) pCGLDescribePixelFormat(pf, 0, kCGLPFAAuxBuffers, &v);
            printf("choose core=%d aux=%d: error %d aux %d\n", core, aux, e, v);
            if (pf) pCGLDestroyPixelFormat(pf);
        }
    /* Apple's software renderer has no aux depth/stencil: only formats that
       exclude it (kCGLPFANoRecovery) have them. */
    for (int no_recovery = 0; no_recovery < 2; ++no_recovery)
        for (int aux = 0; aux <= 2; aux += 2) {
            CGLPixelFormatAttribute at[] = {kCGLPFAAccelerated, kCGLPFADepthSize, 24, kCGLPFAAuxDepthStencil, kCGLPFAAuxBuffers, aux,
                                            no_recovery ? kCGLPFANoRecovery : kCGLPFAAccelerated, 0};
            CGLPixelFormatObj pf = NULL;
            GLint n = 0, v = -1;
            CGLError e = pCGLChoosePixelFormat(at, &pf, &n);
            if (pf) pCGLDescribePixelFormat(pf, 0, kCGLPFAAuxDepthStencil, &v);
            printf("choose aux-depth-stencil no-recovery=%d aux=%d: error %d aux-depth-stencil %d\n", no_recovery, aux, e, v);
            if (pf) pCGLDestroyPixelFormat(pf);
        }

    /* Accumulation buffers: any legacy request gets 32-bit float channels;
       core profiles have none. */
    for (int core = 0; core < 2; ++core)
        for (int accum = 0; accum <= 256; accum = accum ? accum * 4 : 1) {
            CGLPixelFormatAttribute at[] = {kCGLPFAAccelerated, kCGLPFAAccumSize, accum, kCGLPFAOpenGLProfile,
                                            (CGLPixelFormatAttribute)(core ? kCGLOGLPVersion_GL4_Core : kCGLOGLPVersion_Legacy), 0};
            CGLPixelFormatObj pf = NULL;
            GLint n = 0, v = -1;
            CGLError e = pCGLChoosePixelFormat(at, &pf, &n);
            if (pf) pCGLDescribePixelFormat(pf, 0, kCGLPFAAccumSize, &v);
            printf("choose core=%d accum=%d: error %d accum %d\n", core, accum, e, v);
            if (pf) pCGLDestroyPixelFormat(pf);
        }

    int status = 0;
    for (int db = 0; db < 2; ++db) {
        /* The double-buffered format also has aux depth/stencil buffers
           (as Call of Duty 2 asks). */
        CGLPixelFormatAttribute at[] = {kCGLPFAAccelerated, kCGLPFAAuxBuffers, 2, kCGLPFADepthSize, 24, kCGLPFAColorSize, 32,
                                        db ? kCGLPFADoubleBuffer : kCGLPFAAccelerated, kCGLPFANoRecovery,
                                        db ? kCGLPFAAuxDepthStencil : kCGLPFAAccelerated, 0};
        CGLPixelFormatObj pf;
        GLint n, v;
        CGLContextObj c;
        pCGLChoosePixelFormat(at, &pf, &n);
        pCGLCreateContext(pf, NULL, &c);
        pCGLSetCurrentContext(c);
        pglGetIntegerv(GL_AUX_BUFFERS, &v);
        printf("double=%d GL_AUX_BUFFERS %d\n", db, v);
        const GLenum aux[] = {GL_AUX0, GL_AUX1, GL_AUX2, GL_AUX3};
        for (int m = 0; m < 4; ++m) {
            GLint d, r, type = -1, red = -1, encoding = -1, component = -1;
            pglDrawBuffer(aux[m]); GLenum e1 = pglGetError(); pglGetIntegerv(GL_DRAW_BUFFER, &d);
            pglDrawBuffers(1, &aux[m]); GLenum e2 = pglGetError();
            pglReadBuffer(aux[m]); GLenum e3 = pglGetError(); pglGetIntegerv(GL_READ_BUFFER, &r);
            pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, aux[m], GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, aux[m], GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &red);
            pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, aux[m], GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
            pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, aux[m], GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component);
            printf("  aux%d: glDrawBuffer %#x (%#x) glDrawBuffers %#x glReadBuffer %#x (%#x) type %#x red %d encoding %#x component %#x error %#x\n",
                   m, e1, d, e2, e3, r, type, red, encoding, component, pglGetError());
        }
        const GLenum pairs[3][2] = {{GL_AUX0, GL_AUX1}, {GL_BACK_LEFT, GL_AUX0}, {GL_FRONT_LEFT, GL_AUX1}};
        for (int i = 0; i < 3; ++i) {
            pglDrawBuffers(2, pairs[i]);
            printf("  glDrawBuffers %#x,%#x: %#x\n", pairs[i][0], pairs[i][1], pglGetError());
        }
        if (!set_view) continue;

        /* GLMetal only: render into a windowless 8x8 view. */
        NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 8, 8)];
        set_view(c, (__bridge void *)view);
        GLint vp[4];
        pglGetIntegerv(GL_VIEWPORT, vp);
        printf("glmetal: double=%d viewport %d %d %d %d\n", db, vp[0], vp[1], vp[2], vp[3]);
        if (vp[2] != 8 || vp[3] != 8) { printf("glmetal: no drawable\n"); status = 1; continue; }
        GLenum base = db ? GL_BACK : GL_FRONT;
        unsigned char p[4];
        pglDrawBuffer(base); pglClearColor(1, 0, 0, 1); pglClear(GL_COLOR_BUFFER_BIT);
        pglDrawBuffer(GL_AUX0); pglClearColor(0, 1, 0, 1); pglClear(GL_COLOR_BUFFER_BIT);
        pglDrawBuffer(GL_AUX1); pglClearColor(0, 0, 1, 1); pglClear(GL_COLOR_BUFFER_BIT);
        pglColor4f(1, 1, 1, 1);
        pglBegin(GL_QUADS); pglVertex2f(-1, -1); pglVertex2f(0, -1); pglVertex2f(0, 0); pglVertex2f(-1, 0); pglEnd();
        const GLenum reads[3] = {base, GL_AUX0, GL_AUX1};
        const int far_rgb[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
        for (int i = 0; i < 3; ++i) {
            char what[64];
            pglReadBuffer(reads[i]);
            pglReadPixels(6, 6, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            snprintf(what, sizeof what, "clear read %#x (6,6)", reads[i]);
            expect(what, p, far_rgb[i][0], far_rgb[i][1], far_rgb[i][2]);
            pglReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            snprintf(what, sizeof what, "draw read %#x (1,1)", reads[i]);
            if (i == 2) expect(what, p, 255, 255, 255);
            else expect(what, p, far_rgb[i][0], far_rgb[i][1], far_rgb[i][2]);
        }
        GLuint tex;
        unsigned char img[8 * 8 * 4];
        pglGenTextures(1, &tex);
        pglBindTexture(GL_TEXTURE_2D, tex);
        pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        pglReadBuffer(GL_AUX1); pglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 8, 8);
        pglReadBuffer(GL_AUX0); pglCopyTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 4, 4, 4, 4);
        pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        expect("copytex from aux1 (1,1)", &img[(1 * 8 + 1) * 4], 255, 255, 255);
        expect("copytex from aux1 (6,1)", &img[(1 * 8 + 6) * 4], 0, 0, 255);
        expect("copytex from aux0 (6,6)", &img[(6 * 8 + 6) * 4], 0, 255, 0);
        pglReadBuffer(GL_AUX0); pglDrawBuffer(base); pglRasterPos2f(-1, -1); pglCopyPixels(0, 0, 2, 2, GL_COLOR);
        pglReadBuffer(base); pglReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        expect("copypixels aux0 -> base (0,0)", p, 0, 255, 0);
        pglReadBuffer(GL_AUX1); pglDrawBuffer(GL_AUX0);
        pglBlitFramebuffer(0, 0, 2, 2, 6, 6, 8, 8, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        pglReadBuffer(GL_AUX0); pglReadPixels(7, 7, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        expect("blit aux1 -> aux0 (7,7)", p, 255, 255, 255);
        const GLenum both[2] = {db ? GL_BACK_LEFT : GL_FRONT_LEFT, GL_AUX1};
        pglDrawBuffers(2, both);
        pglClearColor(1, 1, 0, 1); pglClear(GL_COLOR_BUFFER_BIT);
        pglReadBuffer(GL_AUX1); pglReadPixels(6, 6, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        expect("two-buffer clear aux1", p, 255, 255, 0);
        pglReadBuffer(base); pglReadPixels(6, 6, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        expect("two-buffer clear base", p, 255, 255, 0);
        pglReadBuffer(GL_AUX0); pglReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        expect("two-buffer clear leaves aux0 (5,5)", p, 0, 255, 0);
        /* Depth: aux0's is cleared to 1, then the base buffer's to 0 so
           GL_LESS rejects everything there. A quad at z=0 lands in aux0
           only if aux0 has depth of its own. */
        pglDrawBuffer(GL_AUX0); pglClearColor(0, 0, 0, 1); pglClearDepth(1); pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        pglDrawBuffer(base); pglClearDepth(0); pglClear(GL_DEPTH_BUFFER_BIT);
        pglDrawBuffer(GL_AUX0);
        pglEnable(GL_DEPTH_TEST); pglDepthFunc(GL_LESS);
        pglColor4f(1, 0, 1, 1);
        pglBegin(GL_QUADS); pglVertex2f(-1, -1); pglVertex2f(1, -1); pglVertex2f(1, 1); pglVertex2f(-1, 1); pglEnd();
        pglDisable(GL_DEPTH_TEST);
        pglReadBuffer(GL_AUX0); pglReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        if (db) expect("depth-tested draw into aux0 with its own depth", p, 255, 0, 255);
        else expect("depth-tested draw into aux0 sharing the base depth", p, 0, 0, 0);
        GLenum error = pglGetError();
        printf("glmetal: double=%d error %#x\n", db, error);
        if (error) status = 1;
        set_view(c, NULL);
    }
    return status || failures ? 1 : 0;
    }
}
