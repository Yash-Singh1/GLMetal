/* CGLTexImageIOSurface2D as Apple's OpenGL implements it (make
   test-iosurface diffs a run on Apple's against one on GLMetal):

   - the CGL and GL errors for each target, internal format, format and
     type, and for sizes and planes the surface does not have
   - the level the texture then has (width, height, internal format)
   - texels sampled from the surface, after the CPU rewrites it, after
     glTexSubImage2D and after a framebuffer clear, and the surface bytes
     those writes leave
   - a second surface replacing the first, and glTexImage2D, which Apple
     refuses (GL_INVALID_OPERATION) while a surface is attached
   - the row pitch bounding the width

   Apple's GL accepts only GL_TEXTURE_RECTANGLE_ARB. It fails a Metal
   assertion (aborts) for a zero width or height, and for BGRA with
   UNSIGNED_SHORT_1_5_5_5_REV or 4_4_4_4_REV and YCBCR_422 with GL_RGB,
   so the probe sends none of those.

   Offscreen: no window, a 4x4 framebuffer object for readback.

   iosurface_texture [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <IOSurface/IOSurface.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)
static void *lib;

static IOSurfaceRef surface(int width, int height, int bytes, uint32_t format)
{
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int values[4] = {width, height, bytes, (int)format};
    CFStringRef keys[4] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat};
    for (int i = 0; i < 4; ++i) {
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &values[i]);
        CFDictionarySetValue(d, keys[i], n);
        CFRelease(n);
    }
    IOSurfaceRef s = IOSurfaceCreate(d);
    CFRelease(d);
    return s;
}

/* Byte i of row y: a pattern that differs per texel and channel. */
static void fill(IOSurfaceRef s, int seed)
{
    IOSurfaceLock(s, 0, NULL);
    uint8_t *p = IOSurfaceGetBaseAddress(s);
    size_t row = IOSurfaceGetBytesPerRow(s), w = IOSurfaceGetWidth(s) * IOSurfaceGetBytesPerElement(s);
    for (size_t y = 0; y < IOSurfaceGetHeight(s); ++y)
        for (size_t i = 0; i < w; ++i) {
            uint8_t v = (uint8_t)(seed + y * 37 + i * 11);
            /* Halves and floats: keep the values finite and in [0, 2]. */
            if (IOSurfaceGetBytesPerElement(s) >= 8 || IOSurfaceGetPixelFormat(s) == 'R16h' || IOSurfaceGetPixelFormat(s) == 'R32f')
                v = (uint8_t)(i % (IOSurfaceGetPixelFormat(s) == 'R32f' || IOSurfaceGetBytesPerElement(s) == 16 ? 4 : 2) ==
                                  (IOSurfaceGetPixelFormat(s) == 'R32f' || IOSurfaceGetBytesPerElement(s) == 16 ? 3 : 1)
                              ? 0x3c - (y & 1) : v);
            p[y * row + i] = v;
        }
    IOSurfaceUnlock(s, 0, NULL);
}

static void dump(const char *what, IOSurfaceRef s)
{
    IOSurfaceLock(s, kIOSurfaceLockReadOnly, NULL);
    const uint8_t *p = IOSurfaceGetBaseAddress(s);
    size_t row = IOSurfaceGetBytesPerRow(s), w = IOSurfaceGetWidth(s) * IOSurfaceGetBytesPerElement(s);
    printf("  %s bytes:", what);
    for (size_t y = 0; y < IOSurfaceGetHeight(s) && y < 2; ++y)
        for (size_t i = 0; i < w && i < 16; ++i) printf(" %02x", p[y * row + i]);
    printf("\n");
    IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, NULL);
}

static GLuint fbo, color;

/* The texture drawn over the 4x4 framebuffer: its texels at each pixel. */
static void sample(const char *what, GLenum target, int width, int height)
{
    F(glBindFramebufferEXT); F(glViewport); F(glClearColor); F(glClear); F(glEnable); F(glDisable); F(glBegin); F(glEnd);
    F(glTexCoord2f); F(glVertex2f); F(glReadPixels); F(glTexEnvi); F(glGetError);
    pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    pglViewport(0, 0, 4, 4);
    pglClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglEnable(target);
    pglTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    float s = target == GL_TEXTURE_RECTANGLE_ARB ? (float)width : 1, t = target == GL_TEXTURE_RECTANGLE_ARB ? (float)height : 1;
    pglBegin(GL_QUADS);
    pglTexCoord2f(0, 0); pglVertex2f(-1, -1);
    pglTexCoord2f(s, 0); pglVertex2f(1, -1);
    pglTexCoord2f(s, t); pglVertex2f(1, 1);
    pglTexCoord2f(0, t); pglVertex2f(-1, 1);
    pglEnd();
    pglDisable(target);
    uint8_t pixels[4 * 4 * 4];
    pglReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    printf("  %s texels (error %#x):", what, pglGetError());
    for (int i = 0; i < 16; i += 5) printf(" %02x%02x%02x%02x", pixels[i * 4], pixels[i * 4 + 1], pixels[i * 4 + 2], pixels[i * 4 + 3]);
    printf("\n");
}

int main(int argc, char **argv)
{
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(CGLTexImageIOSurface2D); F(glGetError);
    F(glGenTextures); F(glBindTexture); F(glTexParameteri); F(glGetTexLevelParameteriv); F(glDeleteTextures);
    F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glFramebufferTexture2DEXT); F(glCheckFramebufferStatusEXT);
    F(glTexImage2D); F(glTexSubImage2D); F(glClearColor); F(glClear); F(glFlush); F(glFinish); F(glViewport);
    
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);

    pglGenTextures(1, &color);
    pglBindTexture(GL_TEXTURE_2D, color);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglGenFramebuffersEXT(1, &fbo);
    pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    pglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, color, 0);
    pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    static const struct {
        const char *name;
        GLenum internal, format, type;
        int bytes;
        uint32_t pixel_format;
    } formats[] = {
        {"rgba_bgra_8888rev", GL_RGBA, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 'BGRA'},
        {"rgb_bgra_8888rev", GL_RGB, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 'BGRA'},
        {"rgba8_bgra_8888rev", GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 'BGRA'},
        {"rgba8_bgra_ubyte", GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4, 'BGRA'},
        {"rgba_rgba_ubyte", GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, 4, 'RGBA'},
        {"rgba8_rgba_ubyte", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, 'RGBA'},
        {"srgb8a8_bgra_8888rev", GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 'BGRA'},
        {"rgb10a2_bgra_2101010rev", GL_RGB10_A2, GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV, 4, 'l10r'},
        {"r8_red_ubyte", GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, 'L008'},
        {"red_red_ubyte", GL_RED, GL_RED, GL_UNSIGNED_BYTE, 1, 'L008'},
        {"rg8_rg_ubyte", GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, '2C08'},
        {"luminance_luminance_ubyte", GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE, 1, 'L008'},
        {"alpha_alpha_ubyte", GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, 1, 'L008'},
        {"r16_red_ushort", GL_R16, GL_RED, GL_UNSIGNED_SHORT, 2, 'L016'},
        {"r16f_red_half", GL_R16F, GL_RED, GL_HALF_FLOAT, 2, 'R16h'},
        {"r32f_red_float", GL_R32F, GL_RED, GL_FLOAT, 4, 'R32f'},
        {"rgba16f_rgba_half", GL_RGBA16F_ARB, GL_RGBA, GL_HALF_FLOAT, 8, 'RGhA'},
        {"rgba32f_rgba_float", GL_RGBA32F_ARB, GL_RGBA, GL_FLOAT, 16, 'RGfA'},
        {"rgb_422_8_8", GL_RGB, GL_RGB_422_APPLE, GL_UNSIGNED_SHORT_8_8_APPLE, 2, '2vuy'},
        {"rgb_422_8_8rev", GL_RGB, GL_RGB_422_APPLE, GL_UNSIGNED_SHORT_8_8_REV_APPLE, 2, 'yuvs'},
        {"rgb8_bgra_8888rev", GL_RGB8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 4, 'BGRA'},
        {"rgba_bgra_ushort", GL_RGBA, GL_BGRA, GL_UNSIGNED_SHORT, 4, 'BGRA'},
        {"depth_depth_float", GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_FLOAT, 4, 'R32f'},
    };
    static const GLenum targets[] = {GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_1D};
    static const char *target_names[] = {"rect", "2d", "cube", "1d"};
    const int width = 8, height = 4;
    for (size_t f = 0; f < sizeof formats / sizeof formats[0]; ++f) {
        IOSurfaceRef s = surface(width, height, formats[f].bytes, formats[f].pixel_format);
        if (!s) { printf("%s: no surface\n", formats[f].name); continue; }
        for (size_t t = 0; t < sizeof targets / sizeof targets[0]; ++t) {
            /* Only the 8-bit BGRA surface goes through every target. */
            if (t > 1 && f) continue;
            GLuint texture;
            pglGenTextures(1, &texture);
            pglBindTexture(targets[t], texture);
            pglTexParameteri(targets[t], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            pglTexParameteri(targets[t], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            fill(s, 0);
            CGLError e = pCGLTexImageIOSurface2D(context, targets[t], formats[f].internal, width, height, formats[f].format,
                                                 formats[f].type, s, 0);
            GLenum gl = pglGetError();
            GLint w = -1, h = -1, internal = -1;
            GLenum query = targets[t] == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : targets[t];
            pglGetTexLevelParameteriv(query, 0, GL_TEXTURE_WIDTH, &w);
            pglGetTexLevelParameteriv(query, 0, GL_TEXTURE_HEIGHT, &h);
            pglGetTexLevelParameteriv(query, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);
            pglGetError();
            printf("%s %s: cgl %d gl %#x level %dx%d internal %#x\n", formats[f].name, target_names[t], e, gl, w, h, internal);
            if (!e && t < 2) {
                sample("initial", targets[t], width, height);
                fill(s, 100);
                sample("cpu rewrite", targets[t], width, height);
                if (formats[f].bytes == 4 && formats[f].type != GL_UNSIGNED_SHORT) {
                    static const uint8_t sub[2 * 2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
                    pglTexSubImage2D(targets[t], 0, 1, 0, 2, 2, formats[f].format, formats[f].type, sub);
                    printf("  subimage error %#x\n", pglGetError());
                    pglFinish();
                    dump("subimage", s);
                    sample("subimage", targets[t], width, height);
                }
                GLuint target_fbo;
                pglGenFramebuffersEXT(1, &target_fbo);
                pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, target_fbo);
                pglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, targets[t], texture, 0);
                GLenum status = pglCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
                printf("  framebuffer status %#x error %#x\n", status, pglGetError());
                if (status == GL_FRAMEBUFFER_COMPLETE_EXT) {
                    pglViewport(0, 0, width, height);
                    pglClearColor(0.25f, 0.5f, 0.75f, 1.0f);
                    pglClear(GL_COLOR_BUFFER_BIT);
                    pglFlush();
                    pglFinish();
                    dump("cleared", s);
                }
                pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
                if (!f) {
                    /* A second surface replaces the first; a new image
                       through glTexImage2D. */
                    IOSurfaceRef other = surface(width, height, 4, 'BGRA');
                    fill(other, 50);
                    e = pCGLTexImageIOSurface2D(context, targets[t], GL_RGBA, width, height, GL_BGRA,
                                                GL_UNSIGNED_INT_8_8_8_8_REV, other, 0);
                    printf("  second surface: cgl %d gl %#x\n", e, pglGetError());
                    sample("second surface", targets[t], width, height);
                    e = pCGLTexImageIOSurface2D(context, targets[t], GL_RGBA, width, height, GL_BGRA,
                                                GL_UNSIGNED_INT_8_8_8_8_REV, s, 0);
                    CFRelease(other);
                    pglTexImage2D(targets[t], 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                    printf("  teximage error %#x\n", pglGetError());
                    fill(s, 200);
                    pglTexSubImage2D(targets[t], 0, 0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, "\x11\x22\x33\x44");
                    pglFinish();
                    dump("detached", s);
                    pglGetTexLevelParameteriv(targets[t], 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);
                    printf("  detached internal %#x error %#x\n", internal, pglGetError());
                }
            }
            pglDeleteTextures(1, &texture);
        }
        CFRelease(s);
    }

    /* Sizes, planes and contexts the call cannot use. */
    IOSurfaceRef s = surface(width, height, 4, 'BGRA');
    GLuint texture;
    pglGenTextures(1, &texture);
    pglBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture);
    static const struct { const char *name; int w, h, plane; } bad[] = {
        {"wider", width * 2, height, 0}, {"taller", width, height * 2, 0}, {"smaller", width / 2, height / 2, 0},
        {"negative", -1, height, 0}, {"plane1", width, height, 1},
    };
    /* The row pitch bounds the width, the surface's height the height. */
    int pitch = (int)(IOSurfaceGetBytesPerRow(s) / 4);
    for (int i = 0; i < 2; ++i) {
        CGLError e = pCGLTexImageIOSurface2D(context, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA, pitch + i, height, GL_BGRA,
                                             GL_UNSIGNED_INT_8_8_8_8_REV, s, 0);
        printf("pitch%s: cgl %d gl %#x\n", i ? "+1" : "", e, pglGetError());
    }
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        CGLError e = pCGLTexImageIOSurface2D(context, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA, bad[i].w, bad[i].h, GL_BGRA,
                                             GL_UNSIGNED_INT_8_8_8_8_REV, s, bad[i].plane);
        GLint w = -1;
        pglGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB, 0, GL_TEXTURE_WIDTH, &w);
        printf("%s: cgl %d gl %#x width %d\n", bad[i].name, e, pglGetError(), w);
    }
    printf("null surface: cgl %d gl %#x\n",
           pCGLTexImageIOSurface2D(context, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL, 0),
           pglGetError());
    printf("null context: cgl %d\n",
           pCGLTexImageIOSurface2D(NULL, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, s, 0));
    pglBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    printf("texture 0: cgl %d gl %#x\n",
           pCGLTexImageIOSurface2D(context, GL_TEXTURE_RECTANGLE_ARB, GL_RGBA, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, s, 0),
           pglGetError());
    CFRelease(s);
    return 0;
}
