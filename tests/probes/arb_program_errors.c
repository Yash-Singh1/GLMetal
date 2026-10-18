/* ARB program load errors as Apple's OpenGL reports them: GL error, error
   position and error string for syntax errors, plus NUL bytes and control
   characters in the string (make test-arb-queries diffs Apple and GLMetal).

   arb_program_errors [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#define F(name) typeof(&name) p##name = dlsym(lib, #name)
int main(int argc, char **argv) {
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    bool (*init)(void) = dlsym(lib, "glmetal_initialize"); if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext); F(glGetError); F(glGenProgramsARB); F(glBindProgramARB); F(glProgramStringARB); F(glGetIntegerv);
    CGLPixelFormatAttribute at[] = {kCGLPFAAccelerated, 0}; CGLPixelFormatObj pf; GLint n; CGLContextObj c;
    pCGLChoosePixelFormat(at, &pf, &n); pCGLCreateContext(pf, NULL, &c); pCGLSetCurrentContext(c);
    GLuint p; pglGenProgramsARB(1, &p); pglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, p);
    const char *srcs[] = {
        "!!ARBfp1.0\nMOV result.color, foo;\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color\nEND\n",
        "!!ARBfp1.0\nFOO result.color, fragment.color;\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color;\n",
        "!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color;\x01\nEND\n",
        "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n\0garbage",
        "!!ARBfp1.0\n# caf\xc3\xa9 \xff comment\r\nMOV result.color,\tfragment.color;\r\nEND\r\n# after\n",
    };
    size_t lens[] = {0, 0, 0, 0, 0, 0, 52, 0};
    for (int i = 0; i < 8; ++i) {
        size_t len = lens[i] ? lens[i] : strlen(srcs[i]);
        pglProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)len, srcs[i]);
        GLint pos = -2; GLenum e = pglGetError(); pglGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        const GLubyte *(*gs)(GLenum) = dlsym(lib, "glGetString"); printf("case %d: %#x position %d string \"%s\"\n", i, e, pos, gs(GL_PROGRAM_ERROR_STRING_ARB));
    }
}
