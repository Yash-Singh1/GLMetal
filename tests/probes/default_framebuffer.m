/* Window-system framebuffer behaviour, printed so that a run on Apple's
   OpenGL and one on GLMetal (DYLD_INSERT_LIBRARIES=libGLMetalInject.dylib)
   can be diffed (make test-default-framebuffer):

   - GL_FRONT, GL_BACK, GL_FRONT_AND_BACK, GL_LEFT, GL_NONE and GL_AUX<n>
     as draw and read buffers (GL 2.1 4.2.1), including glDrawBuffers lists
   - swaps: the front buffer receives the back one, which keeps its contents
   - gl_FragColor and ARB result.color broadcast to every draw buffer,
     gl_FragData[n] to one (GL 2.1 3.11.2)
   - glClear, glCopyPixels and glBlitFramebuffer on them
   - the accumulation buffer (GL 2.1 4.2.4): every glAccum operation, the
     scissor, colour masks, glClearAccum and the error cases

   Links the headless shim: the window is ordered in fully transparent,
   click-through and behind every other window, and the app never
   activates. */
#define GL_SILENCE_DEPRECATION 1
#import <Cocoa/Cocoa.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>

enum { W = 64, H = 64 };

static void quad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

static void pixel(const char *what, GLenum buffer, int x, int y)
{
    unsigned char p[4] = {0};
    glReadBuffer(buffer);
    GLenum read_error = glGetError();
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    printf("%s: read %#x (%d,%d) = %d %d %d %d error %#x/%#x\n", what, buffer, x, y, p[0], p[1], p[2], p[3], read_error,
           glGetError());
}

static GLuint glsl_program(const char *fragment)
{
    const char *vertex = "void main() { gl_Position = ftransform(); }";
    GLuint program = glCreateProgram();
    const char *sources[2] = {vertex, fragment};
    GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    for (int i = 0; i < 2; ++i) {
        GLuint shader = glCreateShader(types[i]);
        glShaderSource(shader, 1, &sources[i], NULL);
        glCompileShader(shader);
        glAttachShader(program, shader);
    }
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) printf("link failed\n");
    return program;
}

static void reset(void)
{
    GLenum two[2] = {GL_BACK_LEFT, GL_NONE};
    glDrawBuffers(1, two);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glUseProgram(0);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glGetError();
}

static void clear_all(float r, float g, float b)
{
    glClearColor(r, g, b, 1);
    const GLenum all[] = {GL_FRONT, GL_BACK, GL_AUX0, GL_AUX1};
    for (int i = 0; i < 4; ++i) {
        glDrawBuffer(all[i]);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDrawBuffer(GL_BACK);
}

static void draw_buffer_modes(void)
{
    const GLenum modes[] = {GL_FRONT_AND_BACK, GL_BACK, GL_FRONT, GL_LEFT, GL_BACK_LEFT, GL_FRONT_LEFT, GL_NONE,
                            GL_AUX0, GL_AUX1, GL_RIGHT, GL_FRONT_RIGHT, GL_BACK_RIGHT};
    for (unsigned m = 0; m < sizeof modes / sizeof *modes; ++m) {
        clear_all(0.2f, 0.2f, 0.2f);
        glDrawBuffer(modes[m]);
        GLenum error = glGetError();
        GLint current = 0;
        glGetIntegerv(GL_DRAW_BUFFER, &current);
        printf("glDrawBuffer(%#x): error %#x, GL_DRAW_BUFFER %#x\n", modes[m], error, current);
        glColor4f(0.25f + 0.05f * (float)m, 0.5f, 0.75f, 1);
        quad(-0.5f, -0.5f, 0.5f, 0.5f);
        glClearColor(0, 1, 0, 1);
        glEnable(GL_SCISSOR_TEST);
        glScissor(2, 2, 4, 4);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        const GLenum reads[] = {GL_BACK, GL_FRONT, GL_AUX0, GL_AUX1};
        char what[64];
        for (int r = 0; r < 4; ++r) {
            snprintf(what, sizeof what, "mode %#x draw", modes[m]);
            pixel(what, reads[r], W / 2, H / 2);
            snprintf(what, sizeof what, "mode %#x clear", modes[m]);
            pixel(what, reads[r], 3, 3);
        }
        reset();
    }
    const GLenum reads[] = {GL_LEFT, GL_FRONT_AND_BACK, GL_NONE, GL_AUX2, GL_RIGHT};
    for (int r = 0; r < 5; ++r) pixel("read-only names", reads[r], 1, 1);
}

static void swaps(CGLContextObj context)
{
    clear_all(0, 0, 0);
    glDrawBuffer(GL_BACK);
    glClearColor(0, 1, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_FRONT);
    glClearColor(1, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    glDrawBuffer(GL_BACK);
    CGLFlushDrawable(context);
    pixel("after first swap", GL_FRONT, 10, 10);
    pixel("after first swap", GL_BACK, 10, 10);
    glDrawBuffer(GL_FRONT);
    glClearColor(0, 0, 1, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    CGLFlushDrawable(context);
    pixel("after second swap", GL_FRONT, 10, 10);
    pixel("after second swap", GL_BACK, 10, 10);
    /* Front rendering survives back rendering (piglit front-invalidate-back). */
    glDrawBuffer(GL_FRONT);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    glDrawBuffer(GL_FRONT);
    glReadBuffer(GL_BACK);
    glDrawBuffer(GL_BACK);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pixel("front kept", GL_FRONT, 20, 20);
    glFlush();
    reset();
}

static void broadcast(void)
{
    struct { const char *name; GLuint program; bool arb; } programs[] = {
        {"gl_FragColor", glsl_program("void main() { gl_FragColor = vec4(0.4, 0.6, 0.8, 1.0); }"), false},
        {"gl_FragData[1]", glsl_program("void main() { gl_FragData[0] = vec4(0.2, 0.0, 0.0, 1.0);"
                                        " gl_FragData[1] = vec4(0.0, 0.8, 0.0, 1.0); }"), false},
        {"ARB result.color", 0, true},
        {"fixed function", 0, false},
    };
    GLuint arb = 0;
    glGenProgramsARB(1, &arb);
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, arb);
    const char *fp = "!!ARBfp1.0\nMOV result.color, { 0.6, 0.2, 0.4, 1.0 };\nEND\n";
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(fp), fp);
    const GLenum lists[][2] = {{GL_BACK_LEFT, GL_AUX0}, {GL_AUX1, GL_FRONT_LEFT}, {GL_AUX0, GL_NONE}, {GL_NONE, GL_AUX1}};
    for (int p = 0; p < 4; ++p)
        for (int l = 0; l < 4; ++l) {
            clear_all(0.1f, 0.1f, 0.1f);
            glDrawBuffers(2, lists[l]);
            if (programs[p].arb) glEnable(GL_FRAGMENT_PROGRAM_ARB);
            else glUseProgram(programs[p].program);
            glColor4f(0.9f, 0.3f, 0.1f, 1);
            quad(-1, -1, 1, 1);
            const GLenum reads[] = {GL_BACK, GL_FRONT, GL_AUX0, GL_AUX1};
            char what[96];
            snprintf(what, sizeof what, "%s into %#x,%#x", programs[p].name, lists[l][0], lists[l][1]);
            for (int r = 0; r < 4; ++r) pixel(what, reads[r], 30, 30);
            reset();
        }
    /* GL_FRONT_AND_BACK with a program and with masks and blending. */
    for (int p = 0; p < 4; ++p) {
        clear_all(0.1f, 0.1f, 0.1f);
        glDrawBuffer(GL_FRONT_AND_BACK);
        if (programs[p].arb) glEnable(GL_FRAGMENT_PROGRAM_ARB);
        else glUseProgram(programs[p].program);
        glColorMask(1, 0, 1, 1);
        glColor4f(0.9f, 0.3f, 0.1f, 1);
        quad(-1, -1, 1, 1);
        char what[96];
        snprintf(what, sizeof what, "%s into front and back, green masked", programs[p].name);
        pixel(what, GL_BACK, 30, 30);
        pixel(what, GL_FRONT, 30, 30);
        reset();
    }
    glDeleteProgramsARB(1, &arb);
}

static void copies(void)
{
    /* glCopyPixels between window buffers. */
    clear_all(0, 0, 0);
    glDrawBuffer(GL_AUX1);
    glColor4f(0.3f, 0.7f, 0.2f, 1);
    quad(-1, -1, 0, 0);
    glReadBuffer(GL_AUX1);
    glDrawBuffer(GL_FRONT_AND_BACK);
    glRasterPos2f(0, 0);
    glCopyPixels(0, 0, 16, 16, GL_COLOR);
    pixel("copypixels aux1 -> front and back", GL_BACK, W / 2 + 4, H / 2 + 4);
    pixel("copypixels aux1 -> front and back", GL_FRONT, W / 2 + 4, H / 2 + 4);
    reset();
    /* glBlitFramebuffer from a framebuffer object into window buffers. */
    GLuint fbo, rb;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    glClearColor(1, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    clear_all(0, 0, 0);
    const GLenum targets[] = {GL_FRONT_AND_BACK, GL_FRONT, GL_AUX0};
    for (int t = 0; t < 3; ++t) {
        clear_all(0, 0, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glDrawBuffer(targets[t]);
        glBlitFramebuffer(0, 0, 8, 8, 0, 0, 8, 8, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        printf("blit into %#x: error %#x\n", targets[t], glGetError());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        pixel("blit", GL_BACK, 4, 4);
        pixel("blit", GL_FRONT, 4, 4);
        pixel("blit", GL_AUX0, 4, 4);
        reset();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
}

static void accum_pixel(const char *what, int x, int y)
{
    float p[4] = {0};
    glReadBuffer(GL_BACK);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, p);
    printf("%s: (%d,%d) = %.3f %.3f %.3f %.3f error %#x\n", what, x, y, p[0], p[1], p[2], p[3], glGetError());
}

static void accumulation(void)
{
    GLint bits[4];
    glGetIntegerv(GL_ACCUM_RED_BITS, &bits[0]);
    glGetIntegerv(GL_ACCUM_GREEN_BITS, &bits[1]);
    glGetIntegerv(GL_ACCUM_BLUE_BITS, &bits[2]);
    glGetIntegerv(GL_ACCUM_ALPHA_BITS, &bits[3]);
    printf("accum bits %d %d %d %d\n", bits[0], bits[1], bits[2], bits[3]);
    glClearAccum(0.25f, -0.5f, 2.0f, 0.75f);
    GLfloat clear[4];
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, clear);
    printf("accum clear value %.3f %.3f %.3f %.3f\n", clear[0], clear[1], clear[2], clear[3]);
    glClear(GL_ACCUM_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    glAccum(GL_RETURN, 1.0f);
    accum_pixel("clear then return", 5, 5);
    /* LOAD, ACCUM, MULT, ADD, RETURN on two colours. */
    glClearColor(0.5f, 0.25f, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_LOAD, 0.5f);
    glClearColor(0.2f, 0.4f, 0.6f, 0.8f);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_ACCUM, 1.0f);
    glAccum(GL_MULT, 0.5f);
    glAccum(GL_ADD, 0.1f);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_RETURN, 2.0f);
    accum_pixel("load accum mult add return", 5, 5);
    /* The read buffer is the source; the scissor limits every operation;
       colour masks apply to RETURN. */
    glDrawBuffer(GL_AUX0);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_AUX0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 8, 8);
    glAccum(GL_LOAD, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    glReadBuffer(GL_BACK);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(1, 1, 0, 1);
    glAccum(GL_RETURN, 1.0f);
    glColorMask(1, 1, 1, 1);
    accum_pixel("scissored load from aux0, masked return", 4, 4);
    accum_pixel("outside the scissor", 20, 20);
    /* RETURN into two draw buffers and glClear(GL_ACCUM_BUFFER_BIT) under
       the scissor. */
    glEnable(GL_SCISSOR_TEST);
    glScissor(30, 30, 4, 4);
    glClearAccum(0, 1, 0, 1);
    glClear(GL_ACCUM_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glDrawBuffer(GL_FRONT_AND_BACK);
    glAccum(GL_RETURN, 1.0f);
    glDrawBuffer(GL_BACK);
    accum_pixel("scissored accum clear", 31, 31);
    accum_pixel("outside scissored accum clear", 4, 4);
    pixel("return into front and back", GL_FRONT, 31, 31);
    /* Values outside [0, 1] stay in the accumulation buffer. */
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_LOAD, 4.0f);
    glAccum(GL_RETURN, 0.125f);
    accum_pixel("unclamped accumulation", 10, 10);
    glAccum(GL_ADD, -10.0f);
    glAccum(GL_RETURN, -0.1f);
    accum_pixel("negative accumulation", 10, 10);
    /* Errors. */
    glGetError();
    glAccum(GL_ADD + 100, 1.0f);
    printf("bad accum op: error %#x\n", glGetError());
    glBegin(GL_POINTS);
    glAccum(GL_LOAD, 1.0f);
    glEnd();
    printf("accum inside begin/end: error %#x\n", glGetError());
    GLuint fbo, rb;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 8, 8);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    glAccum(GL_LOAD, 1.0f);
    printf("accum on a framebuffer object: error %#x\n", glGetError());
    glClear(GL_ACCUM_BUFFER_BIT);
    printf("accum clear on a framebuffer object: error %#x\n", glGetError());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    /* glPushAttrib(GL_ACCUM_BUFFER_BIT) keeps the clear value. */
    glClearAccum(0.1f, 0.2f, 0.3f, 0.4f);
    glPushAttrib(GL_ACCUM_BUFFER_BIT);
    glClearAccum(0.9f, 0.9f, 0.9f, 0.9f);
    glPopAttrib();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, clear);
    printf("accum clear value after pop %.3f %.3f %.3f %.3f\n", clear[0], clear[1], clear[2], clear[3]);
    reset();
}

/* A context without an accumulation buffer rejects glAccum. */
static void no_accumulation(NSOpenGLContext *context)
{
    [context makeCurrentContext];
    GLint bits = -1;
    glGetIntegerv(GL_ACCUM_RED_BITS, &bits);
    glGetError();
    glAccum(GL_LOAD, 1.0f);
    printf("no accumulation buffer: bits %d, glAccum error %#x\n", bits, glGetError());
    glClear(GL_ACCUM_BUFFER_BIT);
    printf("no accumulation buffer: glClear error %#x\n", glGetError());
}

int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, W, H)
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, W, H)];
        view.wantsBestResolutionOpenGLSurface = NO;
        window.contentView = view;
        [window orderBack:nil];
        NSOpenGLPixelFormatAttribute attributes[] = {NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, NSOpenGLPFANoRecovery,
                                                     NSOpenGLPFAAuxBuffers, 2, NSOpenGLPFAAccumSize, 64,
                                                     NSOpenGLPFADepthSize, 24, NSOpenGLPFAColorSize, 32, 0};
        NSOpenGLPixelFormat *format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        NSOpenGLContext *context = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
        if (!context) { printf("no context\n"); return 1; }
        for (int i = 0; i < 20; ++i)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        context.view = view;
        [context makeCurrentContext];
        [context update];
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        printf("viewport %d %d %d %d\n", viewport[0], viewport[1], viewport[2], viewport[3]);
        if (viewport[2] != W || viewport[3] != H) return 1;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        draw_buffer_modes();
        swaps(context.CGLContextObj);
        broadcast();
        copies();
        accumulation();
        NSOpenGLPixelFormatAttribute plain[] = {NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated, 0};
        NSOpenGLContext *other = [[NSOpenGLContext alloc]
            initWithFormat:[[NSOpenGLPixelFormat alloc] initWithAttributes:plain] shareContext:nil];
        other.view = view;
        no_accumulation(other);
        printf("done\n");
        [NSOpenGLContext clearCurrentContext];
    }
    return 0;
}
