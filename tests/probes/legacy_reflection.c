/* GLSL 1.10/1.20 programs as Apple's OpenGL links and reflects them: an
   identifier that later GLSL made a built-in function (a sampler named
   `texture`), a varying declared once per preprocessor branch with
   different types, varyings qualified through a precision macro, and
   uniforms and samplers declared but never read (inactive). Fast Five's
   Glitch engine shaders do all of these. Uniforms and attributes are listed
   by name: GL leaves their order to the implementation. make
   test-legacy-reflection diffs a run on Apple's against one on GLMetal.

   legacy_reflection [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

static int compare(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glCreateShader); F(glShaderSource); F(glCompileShader); F(glGetShaderiv); F(glCreateProgram);
    F(glAttachShader); F(glLinkProgram); F(glGetProgramiv); F(glGetActiveUniform); F(glGetActiveAttrib);
    F(glGetUniformLocation); F(glGetAttribLocation); F(glGetError);

    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);

    static const char *const vertex_texture =
        "attribute vec4 Vertex;\nattribute vec2 TexCoord0;\nvarying vec2 vCoord0;\n"
        "void main() { vCoord0 = TexCoord0; gl_Position = Vertex; }\n";
    static const char *const cases[][3] = {
        {"sampler named texture", NULL,
         "uniform sampler2D texture;\nvarying vec2 vCoord0;\n"
         "void main() { gl_FragColor = texture2D(texture, vCoord0); }\n"},
        /* The first branch is the one compiled, as for the engine's cube
           and 3D texture variants. */
        {"varying per branch",
         "#define CUBE 1\nattribute vec4 Vertex;\n#if CUBE\nvarying vec3 vCoord0;\n#else\nvarying vec2 vCoord0;\n#endif\n"
         "varying vec4 vColor0;\n"
         "void main() { vColor0 = Vertex; vCoord0 = Vertex.xyz; gl_Position = Vertex; }\n",
         "#define CUBE 1\n#if CUBE\nuniform samplerCube tex;\nvarying vec3 vCoord0;\n#else\n"
         "uniform sampler2D tex;\nvarying vec2 vCoord0;\n#endif\nvarying vec4 vColor0;\n"
         "#if CUBE\nvoid main() { gl_FragColor = textureCube(tex, vCoord0) * vColor0; }\n"
         "#else\nvoid main() { gl_FragColor = texture2D(tex, vCoord0) * vColor0; }\n#endif\n"},
        {"macro precision",
         "#define lowp\n#define mediump\n#define MAXP mediump\nattribute vec4 Position;\n"
         "attribute mediump vec2 TexCoord0;\nvarying MAXP vec2 vTexCoord0;\nvarying lowp vec4 vColor0;\n"
         "void main() { vTexCoord0 = TexCoord0; vColor0 = Position; gl_Position = Position; }\n",
         "#define lowp\n#define mediump\n#define MAXP mediump\nuniform lowp sampler2D Sampler0;\n"
         "varying lowp vec4 vColor0;\nvarying MAXP vec2 vTexCoord0;\n"
         "void main() { gl_FragColor = vColor0 * texture2D(Sampler0, vTexCoord0); }\n"},
        {"unused uniforms", NULL,
         "#version 120\nuniform sampler2D used;\nuniform sampler2D unusedSampler;\nuniform float bias;\nuniform float AlphaRef;\n"
         "uniform float initialized = 2.0;\n"
         "uniform vec4 tint;\nvarying vec2 vCoord0;\n"
         "void main() { gl_FragColor = texture2D(used, vCoord0) * tint; }\n"},
    };
    for (size_t c = 0; c < sizeof cases / sizeof *cases; ++c) {
        const char *sources[2] = {cases[c][1] ? cases[c][1] : vertex_texture, cases[c][2]};
        GLuint program = pglCreateProgram();
        GLint compiled[2] = {0, 0};
        for (int s = 0; s < 2; ++s) {
            GLuint shader = pglCreateShader(s ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
            pglShaderSource(shader, 1, &sources[s], NULL);
            pglCompileShader(shader);
            pglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled[s]);
            pglAttachShader(program, shader);
        }
        pglLinkProgram(program);
        GLint linked = 0, uniforms = 0, attributes = 0;
        pglGetProgramiv(program, GL_LINK_STATUS, &linked);
        pglGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniforms);
        pglGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &attributes);
        printf("%s: compiled %d %d linked %d uniforms %d attributes %d\n", cases[c][0], compiled[0], compiled[1],
               linked, uniforms, attributes);
        char *lines[64];
        int n = 0;
        for (GLint i = 0; i < uniforms && n < 64; ++i) {
            char name[128] = "", line[256];
            GLint size = 0;
            GLenum type = 0;
            pglGetActiveUniform(program, i, sizeof name, NULL, &size, &type, name);
            snprintf(line, sizeof line, "  uniform %s type %#x size %d located %d", name, type, size,
                     pglGetUniformLocation(program, name) >= 0);
            lines[n++] = strdup(line);
        }
        for (GLint i = 0; i < attributes && n < 64; ++i) {
            char name[128] = "", line[256];
            GLint size = 0;
            GLenum type = 0;
            pglGetActiveAttrib(program, i, sizeof name, NULL, &size, &type, name);
            snprintf(line, sizeof line, "  attribute %s type %#x size %d located %d", name, type, size,
                     pglGetAttribLocation(program, name) >= 0);
            lines[n++] = strdup(line);
        }
        qsort(lines, n, sizeof *lines, compare);
        for (int i = 0; i < n; ++i) printf("%s\n", lines[i]), free(lines[i]);
        static const char *const unused[] = {"unusedSampler", "bias", "AlphaRef"};
        if (strstr(cases[c][0], "unused"))
            for (int i = 0; i < 3; ++i) printf("  location of %s: %d\n", unused[i], pglGetUniformLocation(program, unused[i]));
        printf("  error 0x%04x\n", pglGetError());
    }
    return 0;
}
