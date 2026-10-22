/* CGL pbuffers as Apple's OpenGL answers them on Apple silicon (make
   test-pbuffer diffs a run on Apple's against one on GLMetal). Apple's
   renderer has no pbuffers, arm64 and x86_64 alike: CGLCreatePBuffer
   fails for every target, format and size, and the other calls answer for
   a NULL pbuffer. A program that falls back to framebuffer objects when
   creation fails must see that failure; one that is told it succeeded
   draws into nothing.

   Apple crashes for a NULL result pointer to CGLCreatePBuffer, so the
   probe passes none.

   pbuffer [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(CGLDestroyContext);
    F(CGLCreatePBuffer); F(CGLDescribePBuffer); F(CGLDestroyPBuffer); F(CGLRetainPBuffer); F(CGLReleasePBuffer);
    F(CGLGetPBufferRetainCount); F(CGLSetPBuffer); F(CGLGetPBuffer); F(CGLTexImagePBuffer); F(glGetError);

    static const GLenum targets[] = {GL_TEXTURE_2D, GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_1D, 0};
    static const GLenum internals[] = {GL_RGBA, GL_RGB, GL_RGBA8, GL_DEPTH_COMPONENT, 0};
    static const struct { int w, h, levels; } sizes[] = {{64, 64, 0}, {64, 32, 6}, {60, 40, 1}, {0, 0, 0}, {-1, 16, -1}};
    for (size_t t = 0; t < sizeof targets / sizeof targets[0]; ++t)
        for (size_t i = 0; i < sizeof internals / sizeof internals[0]; ++i)
            for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
                CGLPBufferObj pb = (CGLPBufferObj)1;
                CGLError e = pCGLCreatePBuffer(sizes[s].w, sizes[s].h, targets[t], internals[i], sizes[s].levels, &pb);
                printf("create %#x %#x %dx%d levels %d: %d %s\n", targets[t], internals[i], sizes[s].w, sizes[s].h,
                       sizes[s].levels, e, pb == (CGLPBufferObj)1 ? "untouched" : pb ? "pbuffer" : "null");
            }
    GLsizei w = -1, h = -1;
    GLenum target = 1, internal = 1;
    GLint mipmap = -1;
    printf("describe null: %d", pCGLDescribePBuffer(NULL, &w, &h, &target, &internal, &mipmap));
    printf(" %d %d %#x %#x %d\n", w, h, target, internal, mipmap);
    printf("destroy null: %d\n", pCGLDestroyPBuffer(NULL));
    printf("retain null: %s\n", pCGLRetainPBuffer(NULL) ? "pbuffer" : "null");
    pCGLReleasePBuffer(NULL);
    printf("count null: %u\n", pCGLGetPBufferRetainCount(NULL));

    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFADoubleBuffer, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);
    for (int null_context = 0; null_context < 2; ++null_context) {
        CGLContextObj c = null_context ? NULL : context;
        const char *which = null_context ? "null context" : "context";
        CGLPBufferObj got = (CGLPBufferObj)1;
        GLenum face = 1;
        GLint level = -5, screen = -5;
        CGLError e = pCGLGetPBuffer(c, &got, &face, &level, &screen);
        printf("%s get: %d %s face %#x level %d screen %d\n", which, e,
               got == (CGLPBufferObj)1 ? "untouched" : got ? "pbuffer" : "null", face, level, screen);
        printf("%s set null: %d error %#x\n", which, pCGLSetPBuffer(c, NULL, 0, 0, 0), pglGetError());
        printf("%s set null face level screen: %d\n", which,
               pCGLSetPBuffer(c, NULL, GL_TEXTURE_CUBE_MAP_POSITIVE_X, 1, 1));
        printf("%s teximage null: %d error %#x\n", which, pCGLTexImagePBuffer(c, NULL, GL_BACK), pglGetError());
        printf("%s teximage null front: %d\n", which, pCGLTexImagePBuffer(c, NULL, GL_FRONT));
    }
    pCGLSetCurrentContext(NULL);
    pCGLDestroyContext(context);
    return 0;
}
