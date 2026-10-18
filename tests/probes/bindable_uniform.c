/* EXT_bindable_uniform as Apple's OpenGL answers it: compiling bindable
   uniforms, their buffer sizes and offsets (by name: locations differ
   between implementations), the errors of glUniformBufferEXT and the
   queries, the limits, and the values draws read from buffer contents
   (read back as pixels), including buffer updates after binding. make
   test-bindable-uniform diffs a run on Apple's against one on GLMetal.

   bindable_uniform [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

static void *lib;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glGetError); F(glGetIntegerv); F(glGenFramebuffersEXT); F(glBindFramebufferEXT); F(glGenRenderbuffersEXT);
    F(glBindRenderbufferEXT); F(glRenderbufferStorageEXT); F(glFramebufferRenderbufferEXT); F(glViewport);
    F(glCreateShader); F(glShaderSource); F(glCompileShader); F(glGetShaderiv); F(glCreateProgram);
    F(glAttachShader); F(glLinkProgram); F(glGetProgramiv); F(glUseProgram); F(glGetUniformLocation);
    F(glUniform4f); F(glUniform1f); F(glGenBuffers); F(glBindBuffer); F(glBufferData); F(glBufferSubData);
    F(glBegin); F(glEnd); F(glVertex2f); F(glReadPixels); F(glClear); F(glDeleteBuffers);
    GLint (*pglGetUniformBufferSizeEXT)(GLuint, GLint) = dlsym(lib, "glGetUniformBufferSizeEXT");
    GLintptr (*pglGetUniformOffsetEXT)(GLuint, GLint) = dlsym(lib, "glGetUniformOffsetEXT");
    void (*pglUniformBufferEXT)(GLuint, GLint, GLuint) = dlsym(lib, "glUniformBufferEXT");

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
    pglRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, 4, 4);
    pglFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
    pglViewport(0, 0, 4, 4);
    while (pglGetError()) {}
#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())
    GLint v = -7;
    static const GLenum limits[] = {GL_MAX_VERTEX_BINDABLE_UNIFORMS_EXT, GL_MAX_FRAGMENT_BINDABLE_UNIFORMS_EXT,
                                    GL_MAX_GEOMETRY_BINDABLE_UNIFORMS_EXT, GL_MAX_BINDABLE_UNIFORM_SIZE_EXT,
                                    GL_UNIFORM_BUFFER_BINDING_EXT};
    for (size_t i = 0; i < sizeof limits / sizeof *limits; ++i) {
        v = -7;
        pglGetIntegerv(limits[i], &v);
        printf("limit %#x: %d\n", limits[i], v);
        ERR("limit");
    }

    /* ---- layout ---- */
    const char *vs = "void main() { gl_Position = gl_Vertex; }\n";
    const char *fs =
        "#extension GL_EXT_bindable_uniform : enable\n"
        "struct S { float a; vec3 b; vec2 c; };\n"
        "bindable uniform float f;\n"
        "bindable uniform vec2 v2;\n"
        "bindable uniform vec3 v3;\n"
        "bindable  uniform vec4 v4;\n"
        "bindable uniform mat2 m2;\n"
        "bindable uniform mat3 m3;\n"
        "bindable uniform mat4 m4;\n"
        "bindable uniform float fa[3];\n"
        "bindable uniform vec3 va[2];\n"
        "bindable uniform S s;\n"
        "bindable uniform S sa[2];\n"
        "bindable uniform int i;\n"
        "bindable uniform bool b;\n"
        "bindable uniform ivec3 iv3;\n"
        "uniform vec4 plain;\n"
        "uniform int pick;\n"
        "void main() {\n"
        "  vec4 c = vec4(0.0);\n"
        "  if (pick == 0) c = vec4(f, v2.y, v3.z, v4.w);\n"
        "  if (pick == 1) c = vec4(m2[1].x, m3[2].y, m4[3].z, m4[0].w);\n"
        "  if (pick == 2) c = vec4(fa[0], fa[2], va[1].y, va[0].z);\n"
        "  if (pick == 3) c = vec4(s.a, s.b.y, s.c.x, sa[1].c.y);\n"
        "  if (pick == 4) c = vec4(sa[0].b.z, float(i) / 8.0, b ? 1.0 : 0.0, float(iv3.z) / 8.0);\n"
        "  if (pick == 5) c = plain;\n"
        "  gl_FragColor = c;\n"
        "}\n";
    GLuint shaders[2] = {pglCreateShader(GL_VERTEX_SHADER), pglCreateShader(GL_FRAGMENT_SHADER)};
    pglShaderSource(shaders[0], 1, &vs, NULL);
    pglShaderSource(shaders[1], 1, &fs, NULL);
    GLuint program = pglCreateProgram();
    for (int s = 0; s < 2; ++s) {
        pglCompileShader(shaders[s]);
        GLint ok = 0;
        pglGetShaderiv(shaders[s], GL_COMPILE_STATUS, &ok);
        printf("compiled %d\n", ok);
        pglAttachShader(program, shaders[s]);
    }
    pglLinkProgram(program);
    GLint linked = 0;
    pglGetProgramiv(program, GL_LINK_STATUS, &linked);
    printf("linked %d\n", linked);
    static const char *const names[] = {"f", "v2", "v3", "v4", "m2", "m3", "m4", "fa", "fa[0]", "fa[1]", "fa[2]",
                                        "va", "va[1]", "s.a", "s.b", "s.c", "sa[0].a", "sa[0].b", "sa[1].a",
                                        "sa[1].b", "sa[1].c", "i", "b", "iv3", "plain", "nothing"};
    for (size_t k = 0; k < sizeof names / sizeof *names; ++k) {
        GLint location = pglGetUniformLocation(program, names[k]);
        GLint size = pglGetUniformBufferSizeEXT(program, location);
        GLenum size_error = pglGetError();
        GLintptr offset = pglGetUniformOffsetEXT(program, location);
        GLenum offset_error = pglGetError();
        printf("%s: located %d size %d (0x%04x) offset %ld (0x%04x)\n", names[k], location >= 0, size, size_error,
               (long)offset, offset_error);
    }
    printf("bad program: %d\n", pglGetUniformBufferSizeEXT(program + 100, 0)); ERR("bad program");
    printf("bad location: %d\n", (int)pglGetUniformOffsetEXT(program, 999)); ERR("bad location");

    /* ---- binding ---- */
    GLuint first;
    pglGenBuffers(1, &first);
    pglBindBuffer(GL_UNIFORM_BUFFER_EXT, first);
    ERR("bind uniform buffer");
    pglGetIntegerv(GL_UNIFORM_BUFFER_BINDING_EXT, &v);
    printf("uniform buffer binding is the first: %d\n", (GLuint)v == first);
    GLint loc_f = pglGetUniformLocation(program, "f"), loc_v4 = pglGetUniformLocation(program, "v4");
    pglUniformBufferEXT(program, pglGetUniformLocation(program, "plain"), first); ERR("plain uniform");
    pglUniformBufferEXT(program, 999, first); ERR("bad location");
    pglUniformBufferEXT(program + 100, loc_f, first); ERR("bad program");
    pglUniformBufferEXT(program, loc_f, 12345); ERR("not a buffer");
    pglUniformBufferEXT(program, loc_f, 0); ERR("buffer 0");
    pglUseProgram(program);

    /* ---- values ---- */
    /* A buffer per bindable uniform, of its size: floats k / 64, integers k. */
    static const char *const bindables[] = {"f", "v2", "v3", "v4", "m2", "m3", "m4", "fa", "va", "s", "sa", "i", "b", "iv3"};
    enum { BINDABLES = sizeof bindables / sizeof *bindables };
    GLuint buffers[BINDABLES + 2];
    pglGenBuffers(BINDABLES + 2, buffers);
    GLint pick = pglGetUniformLocation(program, "pick");
    void (*glUniform1i)(GLint, GLint) = dlsym(lib, "glUniform1i");
    GLubyte pixel[4];
#define DRAW(what, p) do { glUniform1i(pick, p); pglClear(GL_COLOR_BUFFER_BIT); pglBegin(GL_QUADS); \
        pglVertex2f(-1, -1); pglVertex2f(1, -1); pglVertex2f(1, 1); pglVertex2f(-1, 1); pglEnd(); \
        printf("%s draw: error 0x%04x\n", what, pglGetError()); \
        pglReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel); \
        printf("%s: %02x%02x%02x%02x\n", what, pixel[0], pixel[1], pixel[2], pixel[3]); ERR(what); } while (0)
    for (int k = 0; k < BINDABLES; ++k) {
        GLint location = pglGetUniformLocation(program, bindables[k]);
        if (location < 0) location = pglGetUniformLocation(program, k == 9 ? "s.a" : k == 10 ? "sa[0].a" : "fa[0]");
        GLint size = pglGetUniformBufferSizeEXT(program, location);
        float data[64];
        for (int e = 0; e < 64; ++e) data[e] = (float)((e * 5 + k * 3) % 64) / 64.0f;
        if (k >= 11) {
            GLint ints[16] = {3, 0, 0, 0};
            for (int e = 0; e < 16; ++e) ints[e] = e % 8 + k - 10;
            memcpy(data, ints, sizeof ints);
        }
        pglBindBuffer(GL_UNIFORM_BUFFER_EXT, buffers[k]);
        pglBufferData(GL_UNIFORM_BUFFER_EXT, size, data, GL_DYNAMIC_DRAW);
        if (k == 0) DRAW("some unbound", 5);
        pglUniformBufferEXT(program, location, buffers[k]);
    }
    ERR("bound");
    DRAW("simple", 0);
    DRAW("matrices", 1);
    DRAW("arrays", 2);
    DRAW("struct", 3);
    DRAW("integers", 4);
    pglUniform4f(pglGetUniformLocation(program, "plain"), 0.25f, 0.5f, 0.75f, 1);
    DRAW("plain", 5);
    /* An update after binding is seen by the next draw. */
    pglBindBuffer(GL_UNIFORM_BUFFER_EXT, buffers[3]);
    const float alpha = 0.75f;
    pglBufferSubData(GL_UNIFORM_BUFFER_EXT, 12, 4, &alpha);
    DRAW("updated", 0);
    /* glUniform* loads into the buffer. */
    void (*pglGetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *) = dlsym(lib, "glGetBufferSubData");
    float back[8] = {-7, -7, -7, -7, -7, -7, -7, -7};
    pglUniform4f(loc_v4, 0.5f, 0.25f, 0.125f, 1); ERR("glUniform4f v4");
    pglBindBuffer(GL_UNIFORM_BUFFER_EXT, buffers[3]);
    pglGetBufferSubData(GL_UNIFORM_BUFFER_EXT, 0, 16, back);
    printf("v4 buffer: %g %g %g %g\n", back[0], back[1], back[2], back[3]);
    pglUniform1f(pglGetUniformLocation(program, "fa[1]"), 0.625f); ERR("glUniform1f fa[1]");
    pglBindBuffer(GL_UNIFORM_BUFFER_EXT, buffers[7]);
    pglGetBufferSubData(GL_UNIFORM_BUFFER_EXT, 16, 4, back);
    printf("fa buffer: %g\n", back[0]);
    pglUniform1f(pglGetUniformLocation(program, "s.a"), 0.375f); ERR("glUniform1f s.a");
    DRAW("loaded", 0);
    DRAW("loaded array", 2);
    /* Draw errors: a buffer shared, or too small. */
    pglUniformBufferEXT(program, pglGetUniformLocation(program, "v3"), buffers[3]);
    DRAW("shared buffer", 5);
    pglUniformBufferEXT(program, pglGetUniformLocation(program, "v3"), buffers[2]);
    pglBindBuffer(GL_UNIFORM_BUFFER_EXT, buffers[2]);
    pglBufferData(GL_UNIFORM_BUFFER_EXT, 8, NULL, GL_DYNAMIC_DRAW);
    DRAW("small buffer", 5);
    /* Relinking forgets the bindings. */
    pglLinkProgram(program);
    pglUseProgram(program);
    DRAW("relinked", 5);
    ERR("end");
    return 0;
}
