/* ARB programs with the NV options (NV_fragment_program2,
   NV_fragment_program_option, NV_vertex_program2_option, NV_vertex_program3)
   as Apple's OpenGL runs them: for each program of a corpus (stdin; "%% fp
   name" / "%% vp name" lines separate programs; "bits" after the name
   prints the results as IEEE bit patterns, "nodraw" only loads the
   program: Apple's GL accepts some invalid ones and then hangs drawing
   them), whether it loads
   (GL error, error position and string, under native limits) and the values
   it computes (on the next line: the four components of each point, points
   separated by "|"; every program prints two lines). make test-nv-programs diffs a run on Apple's against one on
   GLMetal. "%% glsl name" entries are GLSL fragment shaders (see
   draw_glsl).

   Every program runs on 16 points, one per pixel of a 16x1 RGBA32F
   framebuffer. Point i carries inputs A[i], B[i], C[i] in texture
   coordinates 0, 1 and 2 and (i, i / 16, 0, 1) in texture coordinate 3. A
   fragment program under test gets them through a vertex program that only
   copies them (no matrix arithmetic, so NaN and infinity reach it) and
   writes result.color; a vertex program under test writes result.position
   (vertex.position is the pixel centre) and result.texcoord[0], which a
   fragment program copies to the colour. Program local and env parameters
   0-7 hold the values of P below in both stages, and texture units 0-10 the
   textures of make_textures.

   nv_programs [provider.dylib] < nv_programs.txt */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *lib;
#define D(name) static typeof(&name) p##name

D(glGetError); D(glGetIntegerv); D(glEnable); D(glDisable); D(glClear); D(glClearColor); D(glReadPixels);
D(glViewport); D(glGenTextures); D(glBindTexture); D(glTexImage1D); D(glTexImage2D); D(glTexImage3D);
D(glTexParameteri); D(glActiveTexture); D(glGenFramebuffersEXT); D(glBindFramebufferEXT);
D(glFramebufferTexture2DEXT); D(glCheckFramebufferStatusEXT); D(glGenProgramsARB); D(glBindProgramARB);
D(glProgramStringARB); D(glGetProgramivARB); D(glProgramLocalParameter4fvARB); D(glProgramEnvParameter4fvARB);
D(glDeleteProgramsARB); D(glClientActiveTexture); D(glEnableClientState); D(glDisableClientState);
D(glVertexPointer); D(glTexCoordPointer); D(glDrawArrays); D(glFinish); D(glGetString);
D(glCreateShader); D(glShaderSource); D(glCompileShader); D(glGetShaderiv); D(glGetShaderInfoLog); D(glCreateProgram);
D(glAttachShader); D(glLinkProgram); D(glGetProgramiv); D(glUseProgram); D(glGetUniformLocation); D(glUniform1i);
D(glDeleteProgram);

enum { POINTS = 16 };

static const float A[POINTS][4] = {
    {0.25f, 0.5f, 0.75f, 1}, {-0.5f, 1.5f, -2.25f, 3}, {0, -0.0f, 1, -1}, {INFINITY, -INFINITY, NAN, 2},
    {65504, 65520, 70000, -65504}, {6.103515625e-05f, 5.9604645e-08f, 2.9e-08f, -1e-40f},
    {1e-40f, 2, 0.125f, 100}, {0.3f, 0.7f, 0.1f, 0.9f}, {1 / 3.0f, 2 / 3.0f, -1 / 3.0f, 0.999f},
    {255, 256, 127.5f, -128.5f}, {0.0039215f, 0.5019608f, 1.0039f, -0.0039f}, {1.5f, 2.5f, -1.5f, -2.5f},
    {0.6f, 0.4f, 0.2f, 0.8f}, {-1, 1, 0.5f, -0.5f}, {10, -10, 3.14159f, 0.001f}, {0.125f, 0.875f, 0.0625f, 0.5f},
};
/* Also the x derivatives of TXD tests (in 32nds: texels of the 32x32 2D
   texture's base level). */
static const float B[POINTS][4] = {
    {1 / 32.f, 0, 0, 0}, {2 / 32.f, 0, 0, 0}, {4 / 32.f, 1 / 32.f, 0, 0}, {1 / 32.f, 8 / 32.f, 0, 0},
    {0, 0, 0, 0}, {0.5f, 0.5f, 0, 0}, {-3 / 32.f, 0, 0, 0}, {1 / 64.f, 1 / 64.f, 0, 0},
    {3 / 32.f, 3 / 32.f, 0.1f, 0}, {0, 16 / 32.f, 0, 0}, {6 / 32.f, 0, 0, 0}, {1 / 32.f, 1 / 32.f, 1 / 32.f, 0},
    {2, 0, 0, 0}, {0.01f, 0.2f, 0.3f, 0}, {5 / 32.f, 7 / 32.f, 0, 0}, {12 / 32.f, 1 / 32.f, 0, 0},
};
/* The y derivatives. */
static const float C[POINTS][4] = {
    {0, 1 / 32.f, 0, 0}, {0, 2 / 32.f, 0, 0}, {0, 1 / 32.f, 0, 0}, {8 / 32.f, 1 / 32.f, 0, 0},
    {0, 0, 0, 0}, {0.5f, -0.5f, 0, 0}, {0, 3 / 32.f, 0, 0}, {1 / 64.f, -1 / 64.f, 0, 0},
    {3 / 32.f, -3 / 32.f, 0, 0.1f}, {16 / 32.f, 0, 0, 0}, {0, 1 / 32.f, 0, 0}, {1 / 32.f, 1 / 32.f, 1 / 32.f, 0},
    {0, 2, 0, 0}, {0.3f, 0.01f, 0.2f, 0}, {7 / 32.f, -5 / 32.f, 0, 0}, {1 / 32.f, 12 / 32.f, 0, 0},
};
static const float P[8][4] = {
    {0.5f, 2, -1, 4}, {INFINITY, NAN, -0.0f, 1e-40f}, {3, 2, 1, 0}, {0.25f, 0.75f, -0.25f, 1.5f},
    {1, 0, 0, 0}, {0, 1, 0, 0}, {-2, -3, 7, 9}, {65504, 1e-5f, -6e-8f, 0.0001f},
};

/* A distinct colour per mipmap level (and cube face, array layer), so
   sampled values tell the level of detail apart. */
static void level_color(unsigned char *out, int level, int layer)
{
    out[0] = (unsigned char)(255 - level * 40);
    out[1] = (unsigned char)(level * 40);
    out[2] = (unsigned char)(layer * 40 + (level & 1) * 20);
    out[3] = (unsigned char)(255 - layer * 30);
}

static void make_textures(void)
{
    static unsigned char texels[32 * 32 * 32 * 4];
    GLuint t[11];
    pglGenTextures(11, t);
    /* unit 0: 2D 32x32, a colour per level, trilinear. */
    pglActiveTexture(GL_TEXTURE0);
    pglBindTexture(GL_TEXTURE_2D, t[0]);
    for (int level = 0, size = 32; size; ++level, size /= 2) {
        for (int i = 0; i < size * size; ++i) level_color(texels + i * 4, level, 0);
        pglTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 1: 2D 8x8 pattern, one level, nearest. */
    pglActiveTexture(GL_TEXTURE1);
    pglBindTexture(GL_TEXTURE_2D, t[1]);
    for (int i = 0; i < 64; ++i) {
        texels[i * 4] = (unsigned char)((i % 8) * 32);
        texels[i * 4 + 1] = (unsigned char)((i / 8) * 32);
        texels[i * 4 + 2] = (unsigned char)(i * 4);
        texels[i * 4 + 3] = 255;
    }
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* unit 2: cube 16x16, a colour per face and level. */
    pglActiveTexture(GL_TEXTURE2);
    pglBindTexture(GL_TEXTURE_CUBE_MAP, t[2]);
    for (int face = 0; face < 6; ++face)
        for (int level = 0, size = 16; size; ++level, size /= 2) {
            for (int i = 0; i < size * size; ++i) level_color(texels + i * 4, level, face);
            pglTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA8, size, size, 0, GL_RGBA,
                          GL_UNSIGNED_BYTE, texels);
        }
    pglTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 3: 3D 16x16x16, a colour per level. */
    pglActiveTexture(GL_TEXTURE3);
    pglBindTexture(GL_TEXTURE_3D, t[3]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size * size * size; ++i) level_color(texels + i * 4, level, 2);
        pglTexImage3D(GL_TEXTURE_3D, level, GL_RGBA8, size, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    pglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 4: rectangle 8x8 pattern, linear. */
    pglActiveTexture(GL_TEXTURE4);
    pglBindTexture(GL_TEXTURE_RECTANGLE_ARB, t[4]);
    for (int i = 0; i < 64; ++i) {
        texels[i * 4] = (unsigned char)((i % 8) * 32);
        texels[i * 4 + 1] = (unsigned char)((i / 8) * 32);
        texels[i * 4 + 2] = 128;
        texels[i * 4 + 3] = 255;
    }
    pglTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    pglTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    /* unit 5: depth 2D 16x16 (depth = s), mipmapped, compare LEQUAL. */
    static float depth[32 * 32];
    pglActiveTexture(GL_TEXTURE5);
    pglBindTexture(GL_TEXTURE_2D, t[5]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size * size; ++i) depth[i] = ((i % size) + 0.5f) / size * (level ? 0.5f : 1.0f);
        pglTexImage2D(GL_TEXTURE_2D, level, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    }
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    /* unit 6: 1D 32, a colour per level. */
    pglActiveTexture(GL_TEXTURE6);
    pglBindTexture(GL_TEXTURE_1D, t[6]);
    for (int level = 0, size = 32; size; ++level, size /= 2) {
        for (int i = 0; i < size; ++i) level_color(texels + i * 4, level, 1);
        pglTexImage1D(GL_TEXTURE_1D, level, GL_RGBA8, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 7: depth 1D 16 (depth = s), compare LEQUAL. */
    pglActiveTexture(GL_TEXTURE7);
    pglBindTexture(GL_TEXTURE_1D, t[7]);
    for (int i = 0; i < 16; ++i) depth[i] = (i + 0.5f) / 16;
    pglTexImage1D(GL_TEXTURE_1D, 0, GL_DEPTH_COMPONENT24, 16, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    /* unit 8: depth rectangle 8x8 (depth = (s + 0.5) / 8), compare GEQUAL. */
    pglActiveTexture(GL_TEXTURE8);
    pglBindTexture(GL_TEXTURE_RECTANGLE_ARB, t[8]);
    for (int i = 0; i < 64; ++i) depth[i] = ((i % 8) + 0.5f) / 8;
    pglTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_DEPTH_COMPONENT24, 8, 8, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    pglTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    pglTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_COMPARE_FUNC, GL_GEQUAL);
    /* unit 9: 2D array 16x16x4, a colour per level and layer. */
    pglActiveTexture(GL_TEXTURE9);
    pglBindTexture(GL_TEXTURE_2D_ARRAY_EXT, t[9]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int layer = 0; layer < 4; ++layer)
            for (int i = 0; i < size * size; ++i) level_color(texels + (layer * size * size + i) * 4, level, layer);
        pglTexImage3D(GL_TEXTURE_2D_ARRAY_EXT, level, GL_RGBA8, size, size, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    pglTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 10: 1D array 16x4, a colour per level and layer. */
    pglActiveTexture(GL_TEXTURE10);
    pglBindTexture(GL_TEXTURE_1D_ARRAY_EXT, t[10]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int layer = 0; layer < 4; ++layer)
            for (int i = 0; i < size; ++i) level_color(texels + (layer * size + i) * 4, level, layer);
        pglTexImage2D(GL_TEXTURE_1D_ARRAY_EXT, level, GL_RGBA8, size, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    pglTexParameteri(GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    pglActiveTexture(GL_TEXTURE0);
}

static const char pass_vp[] = "!!ARBvp1.0\nMOV result.position, vertex.position;\n"
                              "MOV result.texcoord[0], vertex.texcoord[0];\nMOV result.texcoord[1], vertex.texcoord[1];\n"
                              "MOV result.texcoord[2], vertex.texcoord[2];\nMOV result.texcoord[3], vertex.texcoord[3];\n"
                              "END\n";
static const char pass_fp[] = "!!ARBfp1.0\nMOV result.color, fragment.texcoord[0];\nEND\n";

static GLuint load(GLenum target, const char *source, size_t size, GLint *position, GLint *native)
{
    GLuint program;
    pglGenProgramsARB(1, &program);
    pglBindProgramARB(target, program);
    pglProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)size, source);
    *position = -2;
    pglGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, position);
    *native = -1;
    pglGetProgramivARB(target, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB, native);
    return program;
}

static void print_value(float v, bool bits)
{
    if (bits) {
        unsigned u;
        memcpy(&u, &v, 4);
        printf(" %x", u);
    } else if (isnan(v)) printf(" nan");
    else printf(" %.6g", v);
}

static void read_and_print(bool bits, int rows)
{
    static float out[POINTS][4];
    pglReadPixels(0, 0, POINTS, 1, GL_RGBA, GL_FLOAT, out);
    GLenum draw_error = pglGetError();
    printf("  ");
    if (draw_error) printf("draw error %#x:", draw_error);
    for (int i = 0; i < rows; ++i) {
        for (int c = 0; c < 4; ++c) print_value(out[i][c], bits);
        if (i + 1 < rows) printf(" |");
    }
    printf("\n");
}

/* "%% glsl name": a GLSL fragment shader on the same inputs (texture
   coordinates 0-3 copied by the vertex shader, samplers tex0-tex10 on
   units 0-10), the reference for NV instructions Apple's GL only has in
   GLSL. */
static void draw_glsl(const char *name, const char *body, size_t size, bool bits, int rows)
{
    static const char vertex[] = "#version 120\nvoid main() {\n  gl_Position = gl_Vertex;\n"
                                 "  gl_TexCoord[0] = gl_MultiTexCoord0;\n  gl_TexCoord[1] = gl_MultiTexCoord1;\n"
                                 "  gl_TexCoord[2] = gl_MultiTexCoord2;\n  gl_TexCoord[3] = gl_MultiTexCoord3;\n}\n";
    const char *sources[2] = {vertex, body};
    GLint lengths[2] = {-1, (GLint)size};
    GLuint program = pglCreateProgram();
    for (int stage = 0; stage < 2; ++stage) {
        GLuint shader = pglCreateShader(stage ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        pglShaderSource(shader, 1, &sources[stage], &lengths[stage]);
        pglCompileShader(shader);
        GLint ok = 0;
        pglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = "";
            pglGetShaderInfoLog(shader, sizeof log, NULL, log);
            for (char *c = log; *c; ++c)
                if (*c == '\n') *c = ' ';
            printf("glsl %s: compile error %s\n  not loaded\n", name, log);
            return;
        }
        pglAttachShader(program, shader);
    }
    pglLinkProgram(program);
    pglUseProgram(program);
    for (int u = 0; u < 11; ++u) {
        char sampler[8];
        snprintf(sampler, sizeof sampler, "tex%d", u);
        GLint location = pglGetUniformLocation(program, sampler);
        if (location >= 0) pglUniform1i(location, u);
    }
    printf("glsl %s: error %#x\n", name, pglGetError());
    pglClearColor(-7, -7, -7, -7);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglDrawArrays(GL_POINTS, 0, POINTS);
    read_and_print(bits, rows);
    pglUseProgram(0);
    pglDeleteProgram(program);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
#define L(name) p##name = dlsym(lib, #name)
    L(glGetError); L(glGetIntegerv); L(glEnable); L(glDisable); L(glClear); L(glClearColor); L(glReadPixels);
    L(glViewport); L(glGenTextures); L(glBindTexture); L(glTexImage1D); L(glTexImage2D); L(glTexImage3D);
    L(glTexParameteri); L(glActiveTexture); L(glGenFramebuffersEXT); L(glBindFramebufferEXT);
    L(glFramebufferTexture2DEXT); L(glCheckFramebufferStatusEXT); L(glGenProgramsARB); L(glBindProgramARB);
    L(glProgramStringARB); L(glGetProgramivARB); L(glProgramLocalParameter4fvARB); L(glProgramEnvParameter4fvARB);
    L(glDeleteProgramsARB); L(glClientActiveTexture); L(glEnableClientState); L(glDisableClientState);
    L(glVertexPointer); L(glTexCoordPointer); L(glDrawArrays); L(glFinish); L(glGetString);
    L(glCreateShader); L(glShaderSource); L(glCompileShader); L(glGetShaderiv); L(glGetShaderInfoLog); L(glCreateProgram);
    L(glAttachShader); L(glLinkProgram); L(glGetProgramiv); L(glUseProgram); L(glGetUniformLocation); L(glUniform1i);
    L(glDeleteProgram);
    typeof(&CGLChoosePixelFormat) choose = dlsym(lib, "CGLChoosePixelFormat");
    typeof(&CGLCreateContext) create = dlsym(lib, "CGLCreateContext");
    typeof(&CGLSetCurrentContext) set_current = dlsym(lib, "CGLSetCurrentContext");
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    choose(attributes, &format, &count);
    create(format, NULL, &context);
    set_current(context);

    GLuint target, framebuffer;
    pglGenTextures(1, &target);
    pglBindTexture(GL_TEXTURE_2D, target);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, POINTS, 1, 0, GL_RGBA, GL_FLOAT, NULL);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglGenFramebuffersEXT(1, &framebuffer);
    pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
    pglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, target, 0);
    if (pglCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
        return fprintf(stderr, "incomplete framebuffer\n"), 1;
    pglViewport(0, 0, POINTS, 1);
    make_textures();
    for (int i = 0; i < 8; ++i)
        for (int stage = 0; stage < 2; ++stage) {
            GLenum t = stage ? GL_FRAGMENT_PROGRAM_ARB : GL_VERTEX_PROGRAM_ARB;
            pglProgramLocalParameter4fvARB(t, (GLuint)i, P[i]); /* set again per program below */
            pglProgramEnvParameter4fvARB(t, (GLuint)i, P[i]);
        }

    static float positions[POINTS][2], index[POINTS][4];
    for (int i = 0; i < POINTS; ++i) {
        positions[i][0] = (i + 0.5f) / POINTS * 2 - 1;
        positions[i][1] = 0;
        index[i][0] = (float)i;
        index[i][1] = i / 16.0f;
        index[i][2] = 0;
        index[i][3] = 1;
    }
    pglVertexPointer(2, GL_FLOAT, 0, positions);
    pglEnableClientState(GL_VERTEX_ARRAY);
    const float (*inputs[4])[4] = {A, B, C, (const float (*)[4])index};
    for (int u = 0; u < 4; ++u) {
        pglClientActiveTexture(GL_TEXTURE0 + u);
        pglTexCoordPointer(4, GL_FLOAT, 0, inputs[u]);
        pglEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    GLint position, native;
    GLuint pass[2] = {load(GL_VERTEX_PROGRAM_ARB, pass_vp, strlen(pass_vp), &position, &native),
                      load(GL_FRAGMENT_PROGRAM_ARB, pass_fp, strlen(pass_fp), &position, &native)};
    if (pglGetError() || position != -1) return fprintf(stderr, "pass-through programs do not load\n"), 1;

    /* NV_PROGRAMS_ROWS=n prints the first n points only (exploration). */
    int rows = getenv("NV_PROGRAMS_ROWS") ? atoi(getenv("NV_PROGRAMS_ROWS")) : POINTS;
    if (rows < 1 || rows > POINTS) rows = POINTS;
    static char all[1 << 20];
    size_t length = fread(all, 1, sizeof all - 1, stdin);
    all[length] = 0;
    for (char *p = strstr(all, "%% "); p;) {
        const char *p0 = p;
        bool fragment = p[3] == 'f' || p[3] == 'g';
        char name[128] = "", option[2][16] = {"", ""};
        sscanf(p + 3, "%*s %127s %15s %15s", name, option[0], option[1]);
        bool bits = !strcmp(option[0], "bits") || !strcmp(option[1], "bits");
        bool draw = strcmp(option[0], "nodraw") && strcmp(option[1], "nodraw");
        char *body = strchr(p, '\n');
        if (!body) break;
        ++body;
        char *next = strstr(body, "%% ");
        size_t size = next ? (size_t)(next - body) : strlen(body);
        p = next;
        GLenum target_kind = fragment ? GL_FRAGMENT_PROGRAM_ARB : GL_VERTEX_PROGRAM_ARB;
        while (pglGetError()) {}
        if (!strncmp(p0, "%% glsl", 7)) {
            draw_glsl(name, body, size, bits, rows);
            continue;
        }
        GLuint program = load(target_kind, body, size, &position, &native);
        GLenum error = pglGetError();
        printf("%s %s: error %#x", fragment ? "fp" : "vp", name, error);
        if (position != -1) {
            printf(" position %d \"%s\"\n  not loaded\n", position, (const char *)pglGetString(GL_PROGRAM_ERROR_STRING_ARB));
            pglDeleteProgramsARB(1, &program);
            continue;
        }
        printf(" native %d\n", native);
        if (!draw) {
            printf("  not drawn\n");
            pglDeleteProgramsARB(1, &program);
            continue;
        }
        for (int i = 0; i < 8; ++i) pglProgramLocalParameter4fvARB(target_kind, (GLuint)i, P[i]);
        pglBindProgramARB(fragment ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB, pass[fragment ? 0 : 1]);
        pglEnable(GL_VERTEX_PROGRAM_ARB);
        pglEnable(GL_FRAGMENT_PROGRAM_ARB);
        pglClearColor(-7, -7, -7, -7);
        pglClear(GL_COLOR_BUFFER_BIT);
        pglDrawArrays(GL_POINTS, 0, POINTS);
        read_and_print(bits, rows);
        pglDisable(GL_VERTEX_PROGRAM_ARB);
        pglDisable(GL_FRAGMENT_PROGRAM_ARB);
        pglDeleteProgramsARB(1, &program);
    }
    return 0;
}
