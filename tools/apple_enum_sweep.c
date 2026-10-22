/* Which enums Apple's OpenGL accepts where: every GL enum value of the SDK
 * headers (build/gen/sweep_enums.h, tools/gen_sweep_enums.py) as the pname
 * of each enum-taking setter and getter, and as the value of each
 * enum-valued parameter and single-enum command, in a legacy and a core
 * context. The diff against GLMetal (make check-enums) lists the enums
 * GLMetal is missing or accepts where Apple does not.
 *
 * For a setter pname the current value (read with the matching getter) is
 * set back, so the error says whether the pname is known; then every enum
 * value is tried as the parameter and the accepted ones are listed (the
 * original value restored after each). Output only names what is
 * accepted, or fails with something other than GL_INVALID_ENUM.
 *
 *   apple_enum_sweep [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "sweep_enums.h"

enum { ENUM_COUNT = sizeof sweep_enums / sizeof *sweep_enums };

static void *lib;
#define D(name) static typeof(&name) p##name
#define L(name) p##name = dlsym(lib, #name)
D(glGetError); D(glGetIntegerv); D(glGetFloatv); D(glGetBooleanv); D(glGetDoublev); D(glGetString);
D(glTexParameteri); D(glGetTexParameteriv); D(glTexEnvi); D(glGetTexEnviv); D(glTexGeni); D(glGetTexGeniv);
D(glFogi); D(glLightModeli); D(glLighti); D(glGetLightiv); D(glMateriali); D(glGetMaterialiv); D(glPixelStorei);
D(glPixelTransferi); D(glHint); D(glPointParameteri); D(glGetTexLevelParameteriv); D(glGetBufferParameteriv);
D(glGetRenderbufferParameteriv); D(glGetFramebufferAttachmentParameteriv); D(glGetQueryiv); D(glGetPointerv);
D(glShadeModel); D(glDepthFunc); D(glAlphaFunc); D(glBlendFunc); D(glBlendEquation); D(glLogicOp); D(glCullFace);
D(glFrontFace); D(glPolygonMode); D(glStencilFunc); D(glStencilOp); D(glColorMaterial); D(glMatrixMode);
D(glDrawBuffer); D(glReadBuffer); D(glBindTexture); D(glBindBuffer); D(glEnableClientState);
D(glDisableClientState); D(glIsEnabled); D(glEnable); D(glDisable); D(glClampColorARB); D(glProvokingVertexEXT);
D(glGenTextures); D(glGenBuffers); D(glBufferData); D(glGenRenderbuffers); D(glBindRenderbuffer);
D(glRenderbufferStorage); D(glGenFramebuffers); D(glBindFramebuffer); D(glFramebufferRenderbuffer);
D(glTexImage2D); D(glBlendEquationSeparate); D(glBlendFuncSeparate); D(glStencilOpSeparate);
D(glStencilFuncSeparate); D(glActiveTexture); D(glClientActiveTexture); D(glGetShaderiv); D(glGetProgramiv);
D(glCreateShader); D(glCreateProgram); D(glDeleteShader); D(glGenQueries); D(glBeginQuery); D(glEndQuery);
D(glGetQueryObjectiv); D(glColorTableParameteriv); D(glConvolutionParameteri); D(glGetMinmaxParameteriv);
D(glGetTexParameterfv); D(glTexParameterf);
static void (*pglSamplerParameteri)(GLuint, GLenum, GLint);
static void (*pglGetSamplerParameteriv)(GLuint, GLenum, GLint *);
static void (*pglGenSamplers)(GLsizei, GLuint *);

static GLenum error(void)
{
    GLenum first = pglGetError(), e;
    while ((e = pglGetError())) {}
    return first;
}

static const char *name_of(unsigned v)
{
    for (int i = 0; i < ENUM_COUNT; ++i)
        if (sweep_enums[i].value == v) return sweep_enums[i].name;
    static char buffer[16];
    snprintf(buffer, sizeof buffer, "%#x", v);
    return buffer;
}

/* ---- pname sweeps over a setter and its getter --------------------------- */

static GLenum target, aux; /* the current setter's target (texture target, light, coordinate...) */

typedef void (*setter)(GLenum pname, GLint value);
typedef void (*getter)(GLenum pname, GLint *values);

static void set_tex(GLenum p, GLint v) { pglTexParameteri(target, p, v); }
static void get_tex(GLenum p, GLint *v) { pglGetTexParameteriv(target, p, v); }
static void set_env(GLenum p, GLint v) { pglTexEnvi(target, p, v); }
static void get_env(GLenum p, GLint *v) { pglGetTexEnviv(target, p, v); }
static void set_gen(GLenum p, GLint v) { pglTexGeni(target, p, v); }
static void get_gen(GLenum p, GLint *v) { pglGetTexGeniv(target, p, v); }
static void set_fog(GLenum p, GLint v) { pglFogi(p, v); }
static void set_light_model(GLenum p, GLint v) { pglLightModeli(p, v); }
static void set_light(GLenum p, GLint v) { pglLighti(GL_LIGHT0, p, v); }
static void get_light(GLenum p, GLint *v) { pglGetLightiv(GL_LIGHT0, p, v); }
static void set_material(GLenum p, GLint v) { pglMateriali(GL_FRONT, p, v); }
static void get_material(GLenum p, GLint *v) { pglGetMaterialiv(GL_FRONT, p, v); }
static void set_pixel_store(GLenum p, GLint v) { pglPixelStorei(p, v); }
static void set_pixel_transfer(GLenum p, GLint v) { pglPixelTransferi(p, v); }
static void set_hint(GLenum p, GLint v) { pglHint(p, (GLenum)v); }
static void set_point(GLenum p, GLint v) { pglPointParameteri(p, v); }
static void set_sampler(GLenum p, GLint v) { pglSamplerParameteri(aux, p, v); }
static void get_sampler(GLenum p, GLint *v) { pglGetSamplerParameteriv(aux, p, v); }
static void get_state(GLenum p, GLint *v) { pglGetIntegerv(p, v); }

/* Prints the pnames the setter or getter knows; for each one the setter
   takes, the enum values it accepts. */
static void pname_sweep(const char *label, setter set, getter get)
{
    error();
    for (int i = 0; i < ENUM_COUNT; ++i) {
        GLenum p = sweep_enums[i].value;
        GLint current[16] = {0};
        get(p, current);
        GLenum got = error();
        set(p, current[0]);
        GLenum put = error();
        if (put == GL_INVALID_ENUM && (got == GL_INVALID_ENUM || get == get_state)) continue;
        printf("%s %s: get %#x set %#x", label, sweep_enums[i].name, got, put);
        if (got || put) {
            printf("\n");
            continue;
        }
        /* The enum values the parameter accepts, and how many others fail
           with which error. */
        int accepted = 0, rejected[3] = {0, 0, 0};
        char list[8192] = "";
        for (int k = 0; k < ENUM_COUNT; ++k) {
            set(p, (GLint)sweep_enums[k].value);
            GLenum e = error();
            set(p, current[0]);
            error();
            if (e >= GL_INVALID_ENUM && e <= GL_INVALID_OPERATION) {
                ++rejected[e - GL_INVALID_ENUM];
                continue;
            }
            ++accepted;
            if (strlen(list) + 64 < sizeof list) {
                strcat(list, " ");
                strcat(list, sweep_enums[k].name);
            }
        }
        if (accepted > 200) printf(" values: %d enums", accepted);
        else printf(" values:%s", list);
        printf(" | rejected enum %d value %d operation %d\n", rejected[0], rejected[1], rejected[2]);
    }
}

/* ---- getter-only pname sweeps -------------------------------------------- */

typedef void (*query)(GLenum pname, GLint *values);
static void q_level(GLenum p, GLint *v) { pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, p, v); }
static void q_buffer(GLenum p, GLint *v) { pglGetBufferParameteriv(GL_ARRAY_BUFFER, p, v); }
static void q_renderbuffer(GLenum p, GLint *v) { pglGetRenderbufferParameteriv(GL_RENDERBUFFER, p, v); }
static void q_attachment(GLenum p, GLint *v) { pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, p, v); }
static void q_query(GLenum p, GLint *v) { pglGetQueryiv(GL_SAMPLES_PASSED, p, v); }
static GLuint shader, program, query_object;
static void q_shader(GLenum p, GLint *v) { pglGetShaderiv(shader, p, v); }
static void q_program(GLenum p, GLint *v) { pglGetProgramiv(program, p, v); }
static void q_query_object(GLenum p, GLint *v) { pglGetQueryObjectiv(query_object, p, v); }
static void q_pointer(GLenum p, GLint *v) { void *ptr = (void *)1; pglGetPointerv(p, &ptr); v[0] = ptr != (void *)1; }

static void query_sweep(const char *label, query q)
{
    error();
    for (int i = 0; i < ENUM_COUNT; ++i) {
        GLint v[16] = {-77777};
        q(sweep_enums[i].value, v);
        GLenum e = error();
        if (e == GL_INVALID_ENUM) continue;
        if (e) printf("%s %s: error %#x\n", label, sweep_enums[i].name, e);
        else printf("%s %s: %d\n", label, sweep_enums[i].name, v[0]);
    }
}

/* glGetFloatv/Booleanv/Doublev against glGetIntegerv: pnames accepted by
   only some of them, and float values that differ from the integer ones
   by more than the integer conversion explains. */
static void getter_forms(const char *label)
{
    error();
    for (int i = 0; i < ENUM_COUNT; ++i) {
        GLenum p = sweep_enums[i].value;
        GLint iv[32];
        GLfloat fv[32];
        GLboolean bv[32];
        GLdouble dv[32];
        pglGetIntegerv(p, iv);
        GLenum ei = error();
        pglGetFloatv(p, fv);
        GLenum ef = error();
        pglGetBooleanv(p, bv);
        GLenum eb = error();
        pglGetDoublev(p, dv);
        GLenum ed = error();
        if (ei == ef && ef == eb && eb == ed) {
            if (!ei) printf("%s %s: float %.6g double %.6g boolean %d\n", label, sweep_enums[i].name, fv[0], dv[0], bv[0]);
            continue;
        }
        printf("%s %s: integer %#x float %#x boolean %#x double %#x\n", label, sweep_enums[i].name, ei, ef, eb, ed);
    }
    for (int i = 0; i < ENUM_COUNT; ++i) {
        const GLubyte *s = pglGetString(sweep_enums[i].value);
        GLenum e = error();
        if (e != GL_INVALID_ENUM) printf("%s glGetString %s: %s %#x\n", label, sweep_enums[i].name, s ? "string" : "null", e);
    }
}

/* ---- single-enum commands ------------------------------------------------ */

typedef void (*command)(GLenum value);
static void c_shade(GLenum v) { pglShadeModel(v); }
static void c_depth(GLenum v) { pglDepthFunc(v); }
static void c_alpha(GLenum v) { pglAlphaFunc(v, 0.5f); }
static void c_blend_src(GLenum v) { pglBlendFunc(v, GL_ZERO); }
static void c_blend_dst(GLenum v) { pglBlendFunc(GL_ONE, v); }
static void c_blend_separate_alpha_src(GLenum v) { pglBlendFuncSeparate(GL_ONE, GL_ZERO, v, GL_ZERO); }
static void c_blend_equation(GLenum v) { pglBlendEquation(v); }
static void c_blend_equation_alpha(GLenum v) { pglBlendEquationSeparate(GL_FUNC_ADD, v); }
static void c_logic(GLenum v) { pglLogicOp(v); }
static void c_cull(GLenum v) { pglCullFace(v); }
static void c_front(GLenum v) { pglFrontFace(v); }
static void c_polygon_face(GLenum v) { pglPolygonMode(v, GL_FILL); }
static void c_polygon_mode(GLenum v) { pglPolygonMode(GL_FRONT_AND_BACK, v); }
static void c_stencil_func(GLenum v) { pglStencilFunc(v, 0, ~0u); }
static void c_stencil_op(GLenum v) { pglStencilOp(v, GL_KEEP, GL_KEEP); }
static void c_stencil_face(GLenum v) { pglStencilOpSeparate(v, GL_KEEP, GL_KEEP, GL_KEEP); }
static void c_stencil_func_face(GLenum v) { pglStencilFuncSeparate(v, GL_ALWAYS, 0, ~0u); }
static void c_color_material_face(GLenum v) { pglColorMaterial(v, GL_AMBIENT_AND_DIFFUSE); }
static void c_color_material_mode(GLenum v) { pglColorMaterial(GL_FRONT_AND_BACK, v); }
static void c_matrix(GLenum v) { pglMatrixMode(v); }
static void c_draw_buffer(GLenum v) { pglDrawBuffer(v); }
static void c_read_buffer(GLenum v) { pglReadBuffer(v); }
static void c_bind_texture(GLenum v) { pglBindTexture(v, 0); }
static void c_bind_buffer(GLenum v) { pglBindBuffer(v, 0); }
static void c_client_state(GLenum v) { pglEnableClientState(v); pglDisableClientState(v); }
static void c_is_enabled(GLenum v) { pglIsEnabled(v); }
static void c_clamp_target(GLenum v) { pglClampColorARB(v, GL_FIXED_ONLY_ARB); }
static void c_clamp_value(GLenum v) { pglClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB, v); }
static void c_provoking(GLenum v) { pglProvokingVertexEXT(v); }
static void c_active_texture(GLenum v) { pglActiveTexture(v); }
static void c_client_texture(GLenum v) { pglClientActiveTexture(v); }
static void c_create_shader(GLenum v) { GLuint s = pglCreateShader(v); if (s) pglDeleteShader(s); }
static void c_begin_query(GLenum v) { pglBeginQuery(v, query_object); if (!pglGetError()) pglEndQuery(v); }

static void command_sweep(const char *label, command c, command restore, GLenum restore_value)
{
    error();
    char list[16384] = "";
    int accepted = 0;
    for (int i = 0; i < ENUM_COUNT; ++i) {
        c(sweep_enums[i].value);
        GLenum e = error();
        if (restore) restore(restore_value);
        error();
        if (e == GL_INVALID_ENUM) continue;
        ++accepted;
        char item[96];
        snprintf(item, sizeof item, e ? " %s(%#x)" : " %s", sweep_enums[i].name, e);
        if (strlen(list) + strlen(item) < sizeof list) strcat(list, item);
    }
    printf("%s:%s\n", label, list);
}

static void sweep(const char *profile, bool core)
{
    char label[96];
    GLuint names[4];
    /* Objects the queries need. */
    pglGenTextures(1, names);
    pglBindTexture(GL_TEXTURE_2D, names[0]);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    pglGenBuffers(1, names);
    pglBindBuffer(GL_ARRAY_BUFFER, names[0]);
    pglBufferData(GL_ARRAY_BUFFER, 64, NULL, GL_STATIC_DRAW);
    pglGenRenderbuffers(1, names);
    pglBindRenderbuffer(GL_RENDERBUFFER, names[0]);
    pglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
    pglGenFramebuffers(1, names + 1);
    pglBindFramebuffer(GL_FRAMEBUFFER, names[1]);
    pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, names[0]);
    shader = pglCreateShader(GL_VERTEX_SHADER);
    program = pglCreateProgram();
    pglGenQueries(1, &query_object);
    pglBeginQuery(GL_SAMPLES_PASSED, query_object);
    pglEndQuery(GL_SAMPLES_PASSED);
    error();

    static const GLenum tex_targets[] = {GL_TEXTURE_1D, GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_CUBE_MAP,
                                         GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_2D_ARRAY_EXT};
    for (unsigned t = 0; t < sizeof tex_targets / sizeof *tex_targets; ++t) {
        if (core && tex_targets[t] == GL_TEXTURE_1D_ARRAY_EXT) continue;
        target = tex_targets[t];
        snprintf(label, sizeof label, "%s glTexParameteri(%s)", profile, name_of(target));
        pname_sweep(label, set_tex, get_tex);
    }
    pglBindTexture(GL_TEXTURE_2D, 0);
    if (!core) {
        static const GLenum env_targets[] = {GL_TEXTURE_ENV, GL_TEXTURE_FILTER_CONTROL, GL_POINT_SPRITE};
        for (int t = 0; t < 3; ++t) {
            target = env_targets[t];
            snprintf(label, sizeof label, "%s glTexEnvi(%s)", profile, name_of(target));
            pname_sweep(label, set_env, get_env);
        }
        static const GLenum coords[] = {GL_S, GL_T, GL_R, GL_Q};
        for (int t = 0; t < 4; ++t) {
            target = coords[t];
            snprintf(label, sizeof label, "%s glTexGeni(%s)", profile, name_of(target));
            pname_sweep(label, set_gen, get_gen);
        }
        snprintf(label, sizeof label, "%s glFogi", profile);
        pname_sweep(label, set_fog, get_state);
        snprintf(label, sizeof label, "%s glLightModeli", profile);
        pname_sweep(label, set_light_model, get_state);
        snprintf(label, sizeof label, "%s glLighti", profile);
        pname_sweep(label, set_light, get_light);
        snprintf(label, sizeof label, "%s glMateriali", profile);
        pname_sweep(label, set_material, get_material);
        snprintf(label, sizeof label, "%s glPixelTransferi", profile);
        pname_sweep(label, set_pixel_transfer, get_state);
    }
    snprintf(label, sizeof label, "%s glPixelStorei", profile);
    pname_sweep(label, set_pixel_store, get_state);
    snprintf(label, sizeof label, "%s glHint", profile);
    pname_sweep(label, set_hint, get_state);
    snprintf(label, sizeof label, "%s glPointParameteri", profile);
    pname_sweep(label, set_point, get_state);
    if (core && pglGenSamplers) {
        pglGenSamplers(1, &aux);
        snprintf(label, sizeof label, "%s glSamplerParameteri", profile);
        pname_sweep(label, set_sampler, get_sampler);
    }

    struct { const char *name; query q; } queries[] = {
        {"glGetTexLevelParameteriv", q_level}, {"glGetBufferParameteriv", q_buffer},
        {"glGetRenderbufferParameteriv", q_renderbuffer}, {"glGetFramebufferAttachmentParameteriv", q_attachment},
        {"glGetQueryiv", q_query}, {"glGetShaderiv", q_shader}, {"glGetProgramiv", q_program},
        {"glGetQueryObjectiv", q_query_object}, {"glGetPointerv", q_pointer},
    };
    pglBindTexture(GL_TEXTURE_2D, names[0]);
    for (unsigned k = 0; k < sizeof queries / sizeof *queries; ++k) {
        if (core && queries[k].q == q_pointer) continue;
        snprintf(label, sizeof label, "%s %s", profile, queries[k].name);
        query_sweep(label, queries[k].q);
    }
    getter_forms(profile);

    struct { const char *name; command c, restore; GLenum value; bool legacy_only; } commands[] = {
        {"glShadeModel", c_shade, c_shade, GL_SMOOTH, true},
        {"glDepthFunc", c_depth, c_depth, GL_LESS},
        {"glAlphaFunc", c_alpha, c_alpha, GL_ALWAYS, true},
        {"glBlendFunc src", c_blend_src, NULL, 0},
        {"glBlendFunc dst", c_blend_dst, NULL, 0},
        {"glBlendFuncSeparate alpha src", c_blend_separate_alpha_src, NULL, 0},
        {"glBlendEquation", c_blend_equation, c_blend_equation, GL_FUNC_ADD},
        {"glBlendEquationSeparate alpha", c_blend_equation_alpha, c_blend_equation_alpha, GL_FUNC_ADD},
        {"glLogicOp", c_logic, c_logic, GL_COPY},
        {"glCullFace", c_cull, c_cull, GL_BACK},
        {"glFrontFace", c_front, c_front, GL_CCW},
        {"glPolygonMode face", c_polygon_face, NULL, 0},
        {"glPolygonMode mode", c_polygon_mode, c_polygon_mode, GL_FILL},
        {"glStencilFunc", c_stencil_func, c_stencil_func, GL_ALWAYS},
        {"glStencilOp", c_stencil_op, c_stencil_op, GL_KEEP},
        {"glStencilOpSeparate face", c_stencil_face, NULL, 0},
        {"glStencilFuncSeparate face", c_stencil_func_face, NULL, 0},
        {"glColorMaterial face", c_color_material_face, NULL, 0, true},
        {"glColorMaterial mode", c_color_material_mode, NULL, 0, true},
        {"glMatrixMode", c_matrix, c_matrix, GL_MODELVIEW, true},
        {"glDrawBuffer", c_draw_buffer, c_draw_buffer, GL_COLOR_ATTACHMENT0},
        {"glReadBuffer", c_read_buffer, c_read_buffer, GL_COLOR_ATTACHMENT0},
        {"glBindTexture", c_bind_texture, NULL, 0},
        {"glBindBuffer", c_bind_buffer, NULL, 0},
        {"glEnableClientState", c_client_state, NULL, 0, true},
        {"glIsEnabled", c_is_enabled, NULL, 0},
        {"glClampColorARB target", c_clamp_target, NULL, 0},
        {"glClampColorARB value", c_clamp_value, c_clamp_value, GL_FIXED_ONLY_ARB},
        {"glProvokingVertexEXT", c_provoking, c_provoking, GL_LAST_VERTEX_CONVENTION_EXT},
        {"glActiveTexture", c_active_texture, c_active_texture, GL_TEXTURE0},
        {"glClientActiveTexture", c_client_texture, c_client_texture, GL_TEXTURE0, true},
        {"glCreateShader", c_create_shader, NULL, 0},
        {"glBeginQuery", c_begin_query, NULL, 0},
    };
    for (unsigned k = 0; k < sizeof commands / sizeof *commands; ++k) {
        if (core && commands[k].legacy_only) continue;
        snprintf(label, sizeof label, "%s %s", profile, commands[k].name);
        command_sweep(label, commands[k].c, commands[k].restore, commands[k].value);
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    L(glGetError); L(glGetIntegerv); L(glGetFloatv); L(glGetBooleanv); L(glGetDoublev); L(glGetString);
    L(glTexParameteri); L(glGetTexParameteriv); L(glTexEnvi); L(glGetTexEnviv); L(glTexGeni); L(glGetTexGeniv);
    L(glFogi); L(glLightModeli); L(glLighti); L(glGetLightiv); L(glMateriali); L(glGetMaterialiv); L(glPixelStorei);
    L(glPixelTransferi); L(glHint); L(glPointParameteri); L(glGetTexLevelParameteriv); L(glGetBufferParameteriv);
    L(glGetRenderbufferParameteriv); L(glGetFramebufferAttachmentParameteriv); L(glGetQueryiv); L(glGetPointerv);
    L(glShadeModel); L(glDepthFunc); L(glAlphaFunc); L(glBlendFunc); L(glBlendEquation); L(glLogicOp); L(glCullFace);
    L(glFrontFace); L(glPolygonMode); L(glStencilFunc); L(glStencilOp); L(glColorMaterial); L(glMatrixMode);
    L(glDrawBuffer); L(glReadBuffer); L(glBindTexture); L(glBindBuffer); L(glEnableClientState);
    L(glDisableClientState); L(glIsEnabled); L(glEnable); L(glDisable); L(glClampColorARB); L(glProvokingVertexEXT);
    L(glGenTextures); L(glGenBuffers); L(glBufferData); L(glGenRenderbuffers); L(glBindRenderbuffer);
    L(glRenderbufferStorage); L(glGenFramebuffers); L(glBindFramebuffer); L(glFramebufferRenderbuffer);
    L(glTexImage2D); L(glBlendEquationSeparate); L(glBlendFuncSeparate); L(glStencilOpSeparate);
    L(glStencilFuncSeparate); L(glActiveTexture); L(glClientActiveTexture); L(glGetShaderiv); L(glGetProgramiv);
    L(glCreateShader); L(glCreateProgram); L(glDeleteShader); L(glGenQueries); L(glBeginQuery); L(glEndQuery);
    L(glGetQueryObjectiv); L(glColorTableParameteriv); L(glConvolutionParameteri); L(glGetMinmaxParameteriv);
    L(glGetTexParameterfv); L(glTexParameterf);
    L(glSamplerParameteri); L(glGetSamplerParameteriv); L(glGenSamplers);
    typeof(&CGLChoosePixelFormat) choose = dlsym(lib, "CGLChoosePixelFormat");
    typeof(&CGLCreateContext) create = dlsym(lib, "CGLCreateContext");
    typeof(&CGLSetCurrentContext) make_current = dlsym(lib, "CGLSetCurrentContext");
    for (int core = 0; core < 2; ++core) {
        CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFAOpenGLProfile,
                                                (CGLPixelFormatAttribute)(core ? kCGLOGLPVersion_GL4_Core
                                                                               : kCGLOGLPVersion_Legacy), 0};
        CGLPixelFormatObj format;
        GLint count;
        CGLContextObj context;
        choose(attributes, &format, &count);
        create(format, NULL, &context);
        make_current(context);
        sweep(core ? "core" : "legacy", core);
        fflush(stdout);
    }
    return 0;
}
