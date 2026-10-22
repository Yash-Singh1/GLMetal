/* GLSL compile status as Apple's OpenGL reports it, for a corpus of shaders
   (stdin; "%% vs name" / "%% fs name" lines, a "core" word after the name
   for a 4.1 core context): one line per shader, "ok" or "error". make
   test-glsl-builtins diffs Apple's and GLMetal's (tests/probes/glsl_builtins.txt,
   from tools/gen_glsl_builtins.py).

   glsl_compile [provider.dylib] < corpus */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    void *lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    typeof(&CGLChoosePixelFormat) choose = dlsym(lib, "CGLChoosePixelFormat");
    typeof(&CGLCreateContext) create = dlsym(lib, "CGLCreateContext");
    typeof(&CGLSetCurrentContext) make_current = dlsym(lib, "CGLSetCurrentContext");
    typeof(&glCreateShader) create_shader = dlsym(lib, "glCreateShader");
    typeof(&glShaderSource) shader_source = dlsym(lib, "glShaderSource");
    typeof(&glCompileShader) compile = dlsym(lib, "glCompileShader");
    typeof(&glGetShaderiv) shader_iv = dlsym(lib, "glGetShaderiv");
    typeof(&glDeleteShader) delete_shader = dlsym(lib, "glDeleteShader");
    CGLContextObj contexts[2];
    for (int core = 0; core < 2; ++core) {
        CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFAOpenGLProfile,
                                                (CGLPixelFormatAttribute)(core ? kCGLOGLPVersion_GL4_Core : kCGLOGLPVersion_Legacy), 0};
        CGLPixelFormatObj format;
        GLint count;
        choose(attributes, &format, &count);
        create(format, NULL, &contexts[core]);
    }
    static char all[1 << 24];
    size_t length = fread(all, 1, sizeof all - 1, stdin);
    all[length] = 0;
    for (char *p = strstr(all, "%% "); p;) {
        bool fragment = p[3] == 'f';
        char *body = strchr(p, '\n');
        if (!body) break;
        char header[512] = "", name[256] = "", option[16] = "";
        snprintf(header, sizeof header, "%.*s", (int)(body - p), p);
        sscanf(header + 6, "%255s %15s", name, option);
        ++body;
        char *next = strstr(body, "%% ");
        GLint size = (GLint)(next ? next - body : (long)strlen(body));
        p = next;
        make_current(contexts[!strcmp(option, "core")]);
        GLuint shader = create_shader(fragment ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        const char *source = body;
        shader_source(shader, 1, &source, &size);
        compile(shader);
        GLint ok = 0;
        shader_iv(shader, GL_COMPILE_STATUS, &ok);
        printf("%s %s%s: %s\n", fragment ? "fs" : "vs", name, option[0] ? " core" : "", ok ? "ok" : "error");
        if (!ok && getenv("GLSL_COMPILE_LOG")) { /* exploration: the log, on one line */
            typeof(&glGetShaderInfoLog) shader_log = dlsym(lib, "glGetShaderInfoLog");
            char log[1024] = "";
            shader_log(shader, sizeof log, NULL, log);
            for (char *c = log; *c; ++c)
                if (*c == '\n') *c = '|';
            printf("  %s\n", log);
        }
        delete_shader(shader);
    }
    return 0;
}
