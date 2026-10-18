/* APPLE_element_array as Apple's OpenGL answers it: the element pointer's
   types, GL_ELEMENT_ARRAY_APPLE's enable and queries, and the pixels the
   element array draws cover. make test-element-array diffs a run on
   Apple's against one on GLMetal.

   element_array [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#define F(name) typeof(&name) p##name = dlsym(lib, #name)
int main(int argc, char **argv) {
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    bool (*init)(void) = dlsym(lib, "glmetal_initialize"); if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(glGetError); F(glGenFramebuffersEXT);
    F(glBindFramebufferEXT); F(glGenRenderbuffersEXT); F(glBindRenderbufferEXT); F(glRenderbufferStorageEXT);
    F(glFramebufferRenderbufferEXT); F(glViewport); F(glEnableClientState); F(glDisableClientState); F(glVertexPointer);
    F(glElementPointerAPPLE); F(glDrawElementArrayAPPLE); F(glDrawRangeElementArrayAPPLE); F(glMultiDrawElementArrayAPPLE);
    F(glClear); F(glReadPixels); F(glIsEnabled); F(glGetIntegerv); F(glGetPointerv); F(glColor4f);
    CGLPixelFormatAttribute a[] = {kCGLPFAAccelerated, 0}; CGLPixelFormatObj f; GLint n; CGLContextObj c;
    pCGLChoosePixelFormat(a, &f, &n); pCGLCreateContext(f, NULL, &c); pCGLSetCurrentContext(c);
    GLuint fbo, rb; pglGenFramebuffersEXT(1, &fbo); pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    pglGenRenderbuffersEXT(1, &rb); pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 8, 8);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 8, 8);
    GLushort idx[6] = {0, 1, 2, 2, 3, 0}; GLfloat quad[8] = {-1,-1, 1,-1, 1,1, -1,1};
    pglEnableClientState(GL_VERTEX_ARRAY); pglVertexPointer(2, GL_FLOAT, 0, quad);
    pglElementPointerAPPLE(GL_UNSIGNED_SHORT, idx); printf("ptr err %x\n", pglGetError());
    pglElementPointerAPPLE(GL_FLOAT, idx); printf("ptr float err %x\n", pglGetError());
    pglElementPointerAPPLE(GL_UNSIGNED_SHORT, idx);
    pglEnableClientState(GL_ELEMENT_ARRAY_APPLE); printf("enable err %x is %d\n", pglGetError(), pglIsEnabled(GL_ELEMENT_ARRAY_APPLE));
    GLint t = -7; pglGetIntegerv(GL_ELEMENT_ARRAY_TYPE_APPLE, &t); printf("type %#x err %x\n", t, pglGetError());
    void *p = NULL; pglGetPointerv(GL_ELEMENT_ARRAY_POINTER_APPLE, &p); printf("pointer %d err %x\n", p == idx, pglGetError());
    pglColor4f(1, 1, 1, 1);
    for (int k = 0; k < 4; ++k) {
        pglClear(GL_COLOR_BUFFER_BIT);
        if (k == 0) pglDrawElementArrayAPPLE(GL_TRIANGLES, 0, 3);
        if (k == 1) pglDrawElementArrayAPPLE(GL_TRIANGLES, 3, 3);
        if (k == 2) pglDrawRangeElementArrayAPPLE(GL_TRIANGLES, 0, 3, 0, 6);
        if (k == 3) { GLint first[2] = {0, 3}; GLsizei count[2] = {3, 3}; pglMultiDrawElementArrayAPPLE(GL_TRIANGLES, first, count, 2); }
        printf("draw %d err %x:", k, pglGetError());
        GLubyte px[8 * 8 * 4]; pglReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, px);
        int lit = 0; for (int i = 0; i < 64; ++i) lit += px[i * 4] != 0; printf(" %d lit\n", lit);
    }
    pglDisableClientState(GL_ELEMENT_ARRAY_APPLE);
    pglDrawElementArrayAPPLE(GL_TRIANGLES, 0, 3); printf("disabled draw err %x\n", pglGetError());
    return 0;
}
