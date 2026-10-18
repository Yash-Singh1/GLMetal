/* ARB program queries as Apple's OpenGL answers them (make test-arb-queries
   diffs a run on Apple's against one on GLMetal):

   - every glGetProgramivARB pname for both targets, with no program and
     with a loaded one: limits, binding, format, native-limit answers
   - the per-program counters (instructions, temporaries, parameters,
     attributes, address registers, ALU/TEX instructions, indirections) of
     each program in arb_programs.txt (read from stdin; "%% vp" / "%% fp"
     lines separate programs), and whether it loads

   arb_program_queries [provider.dylib] < arb_programs.txt */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)

int main(int argc, char **argv)
{
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(glGetProgramivARB); F(glGetError);
    F(glGenProgramsARB); F(glBindProgramARB); F(glProgramStringARB);
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);

    const GLenum targets[2] = {GL_VERTEX_PROGRAM_ARB, GL_FRAGMENT_PROGRAM_ARB};
    const char *sample[2] = {
        "!!ARBvp1.0\nTEMP a, b;\nADDRESS A0;\nPARAM c[4] = { program.local[0..3] };\nARL A0.x, vertex.attrib[1].x;\n"
        "ADD a, c[A0.x], vertex.position;\nMUL b, a, program.env[3];\nDP4 result.position.x, b, state.matrix.mvp.row[0];\n"
        "MOV result.color, vertex.color;\nEND\n",
        "!!ARBfp1.0\nTEMP a, b;\nTEX a, fragment.texcoord[0], texture[0], 2D;\nMUL b, a, fragment.color;\n"
        "TEX a, b, texture[1], 2D;\nADD result.color, a, program.local[2];\nEND\n"};
    for (int loaded = 0; loaded < 2; ++loaded)
        for (int t = 0; t < 2; ++t) {
            if (loaded) {
                GLuint program;
                pglGenProgramsARB(1, &program);
                pglBindProgramARB(targets[t], program);
                pglProgramStringARB(targets[t], GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(sample[t]), sample[t]);
                printf("load sample %s: error %#x\n", t ? "fp" : "vp", pglGetError());
            }
            for (GLenum pname = 0x8600; pname <= 0x88B6; ++pname) {
                if (pname > 0x8700 && pname < 0x8800) continue;
                GLint value = -12345;
                pglGetProgramivARB(targets[t], pname, &value);
                if (!pglGetError()) printf("%s %s %#x = %d\n", loaded ? "loaded" : "none", t ? "fp" : "vp", pname, value);
            }
        }

    static char all[1 << 20];
    size_t length = fread(all, 1, sizeof all - 1, stdin);
    all[length] = 0;
    const GLenum counters[] = {0x88a0, 0x88a2, 0x88a4, 0x88a6, 0x88a8, 0x88aa, 0x88ac, 0x88ae, 0x88b0, 0x88b2,
                               0x8805, 0x8806, 0x8807, 0x8808, 0x8809, 0x880a, 0x88b6};
    printf("program: load error, then instructions native temporaries native parameters native attribs native "
           "address native alu tex indirections native alu tex indirections under-native-limits\n");
    int index = 0;
    for (char *p = strstr(all, "%% "); p;) {
        bool fragment = p[3] == 'f';
        char *body = strchr(p, '\n');
        if (!body) break;
        ++body;
        char *next = strstr(body, "%% ");
        size_t size = next ? (size_t)(next - body) : strlen(body);
        GLenum target = fragment ? GL_FRAGMENT_PROGRAM_ARB : GL_VERTEX_PROGRAM_ARB;
        GLuint program;
        pglGenProgramsARB(1, &program);
        pglBindProgramARB(target, program);
        pglProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)size, body);
        printf("%s %d: %#x", fragment ? "fp" : "vp", index++, pglGetError());
        for (unsigned k = 0; k < sizeof counters / sizeof *counters; ++k) {
            GLint value = -1;
            pglGetProgramivARB(target, counters[k], &value);
            printf(" %d", value);
        }
        printf("\n");
        p = next;
    }
    return 0;
}
