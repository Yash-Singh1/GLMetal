/* Fixed-function state: lighting, materials, fog, texture environment,
 * texture coordinate generation and clip planes. Positions, directions and
 * planes are transformed by the modelview matrix current at specification,
 * as GL 2.1 specifies. */
#include "glm_internal.h"

#include <math.h>
#include <string.h>

static void transform_point(const float *m, const float *in, float *out)
{
    float r[4];
    for (int i = 0; i < 4; ++i) r[i] = m[i] * in[0] + m[4 + i] * in[1] + m[8 + i] * in[2] + m[12 + i] * in[3];
    memcpy(out, r, sizeof r);
}

static const float *modelview(struct glm_context *ctx)
{
    return ctx->state.modelview.m[ctx->state.modelview.depth];
}

/* Plane p in object space to eye space: p * inverse(modelview). */
static void transform_plane(struct glm_context *ctx, const double *in, double *out)
{
    float inverse[16];
    glm_mat4_invert(inverse, modelview(ctx));
    for (int c = 0; c < 4; ++c)
        out[c] = in[0] * inverse[c * 4] + in[1] * inverse[c * 4 + 1] + in[2] * inverse[c * 4 + 2] + in[3] * inverse[c * 4 + 3];
}

/* ---- lights -------------------------------------------------------------- */

GLM_EXPORT void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + GLM_MAX_LIGHTS) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_light *l = &ctx->state.lights[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT: memcpy(l->ambient, params, 16); break;
    case GL_DIFFUSE: memcpy(l->diffuse, params, 16); break;
    case GL_SPECULAR: memcpy(l->specular, params, 16); break;
    case GL_POSITION: transform_point(modelview(ctx), params, l->position); break;
    case GL_SPOT_DIRECTION: {
        const float *m = modelview(ctx);
        for (int i = 0; i < 3; ++i) l->spot_direction[i] = m[i] * params[0] + m[4 + i] * params[1] + m[8 + i] * params[2];
        break;
    }
    case GL_SPOT_EXPONENT: /* up to GL_MAX_SPOT_EXPONENT_NV, 128 */
        if (!(params[0] >= 0 && params[0] <= 128)) return glm_error(ctx, GL_INVALID_VALUE);
        l->spot_exponent = params[0];
        break;
    case GL_SPOT_CUTOFF:
        if (!((params[0] >= 0 && params[0] <= 90) || params[0] == 180)) return glm_error(ctx, GL_INVALID_VALUE);
        l->spot_cutoff = params[0];
        break;
    case GL_CONSTANT_ATTENUATION: l->attenuation[0] = params[0]; break;
    case GL_LINEAR_ATTENUATION: l->attenuation[1] = params[0]; break;
    case GL_QUADRATIC_ATTENUATION: l->attenuation[2] = params[0]; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
/* The scalar forms take the scalar parameters only (Apple's:
   GL_INVALID_ENUM for the vector ones). */
static bool light_vector(GLenum pname)
{
    return pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION ||
           pname == GL_SPOT_DIRECTION;
}
GLM_EXPORT void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    if (light_vector(pname)) {
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    glLightfv(light, pname, &param);
}
GLM_EXPORT void glLighti(GLenum light, GLenum pname, GLint param) { glLightf(light, pname, (GLfloat)param); }
GLM_EXPORT void glLightiv(GLenum light, GLenum pname, const GLint *params)
{
    GLfloat f[4];
    int n = pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION ? 4 : pname == GL_SPOT_DIRECTION ? 3 : 1;
    for (int i = 0; i < n; ++i) f[i] = (GLfloat)params[i];
    glLightfv(light, pname, f);
}

GLM_EXPORT void glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + GLM_MAX_LIGHTS) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_light *l = &ctx->state.lights[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT: memcpy(params, l->ambient, 16); break;
    case GL_DIFFUSE: memcpy(params, l->diffuse, 16); break;
    case GL_SPECULAR: memcpy(params, l->specular, 16); break;
    case GL_POSITION: memcpy(params, l->position, 16); break;
    case GL_SPOT_DIRECTION: memcpy(params, l->spot_direction, 12); break;
    case GL_SPOT_EXPONENT: *params = l->spot_exponent; break;
    case GL_SPOT_CUTOFF: *params = l->spot_cutoff; break;
    case GL_CONSTANT_ATTENUATION: *params = l->attenuation[0]; break;
    case GL_LINEAR_ATTENUATION: *params = l->attenuation[1]; break;
    case GL_QUADRATIC_ATTENUATION: *params = l->attenuation[2]; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}

GLM_EXPORT void glLightModelfv(GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT: memcpy(ctx->state.light_model_ambient, params, 16); break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: ctx->state.light_model_local_viewer = params[0] != 0; break;
    case GL_LIGHT_MODEL_TWO_SIDE: ctx->state.light_model_two_side = params[0] != 0; break;
    case GL_LIGHT_MODEL_COLOR_CONTROL:
        if ((GLenum)params[0] != GL_SINGLE_COLOR && (GLenum)params[0] != GL_SEPARATE_SPECULAR_COLOR)
            return glm_error(ctx, GL_INVALID_ENUM);
        ctx->state.light_model_separate_specular = params[0] == GL_SEPARATE_SPECULAR_COLOR;
        break;
    /* APPLE_specular_vector: Phong's reflection vector for specular terms. */
    case GL_LIGHT_MODEL_SPECULAR_VECTOR_APPLE: ctx->state.light_model_specular_vector = params[0] != 0; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glLightModelf(GLenum pname, GLfloat param)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT) {
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    glLightModelfv(pname, &param);
}
GLM_EXPORT void glLightModeli(GLenum pname, GLint param) { glLightModelf(pname, (GLfloat)param); }
GLM_EXPORT void glLightModeliv(GLenum pname, const GLint *params)
{
    GLfloat f[4] = {0};
    int n = pname == GL_LIGHT_MODEL_AMBIENT ? 4 : 1;
    for (int i = 0; i < n; ++i) f[i] = pname == GL_LIGHT_MODEL_AMBIENT ? (GLfloat)((2.0 * params[i] + 1) / 4294967295.0) : (GLfloat)params[i];
    glLightModelfv(pname, f);
}

/* ---- materials ----------------------------------------------------------- */

/* With GL_COLOR_MATERIAL the tracked material parameters follow the
   current colour; brought up to date whenever they are observed or
   tracking changes (draws read the colour itself). */
GLM_HIDDEN void glm_color_material_sync(struct glm_context *ctx)
{
    struct glm_state *s = &ctx->state;
    if (!s->color_material) return;
    const float *color = s->current[GLM_ATTR_COLOR];
    for (int f = 0; f < 2; ++f) {
        if ((f == 0 && s->color_material_face == GL_BACK) || (f == 1 && s->color_material_face == GL_FRONT)) continue;
        struct glm_material *m = &s->material[f];
        switch (s->color_material_mode) {
        case GL_AMBIENT: memcpy(m->ambient, color, 16); break;
        case GL_DIFFUSE: memcpy(m->diffuse, color, 16); break;
        case GL_AMBIENT_AND_DIFFUSE: memcpy(m->ambient, color, 16); memcpy(m->diffuse, color, 16); break;
        case GL_SPECULAR: memcpy(m->specular, color, 16); break;
        case GL_EMISSION: memcpy(m->emission, color, 16); break;
        }
    }
}

GLM_EXPORT void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    /* GL_MAX_SHININESS_NV (NV_light_max_exponent) is 128. */
    if (pname == GL_SHININESS && !(params[0] >= 0 && params[0] <= 128)) return glm_error(ctx, GL_INVALID_VALUE);
    if (ctx->immediate) glm_immediate_material(ctx);
    for (int f = 0; f < 2; ++f) {
        if (f == 0 && face == GL_BACK) continue;
        if (f == 1 && face == GL_FRONT) continue;
        /* Parameters the current colour tracks ignore glMaterial. */
        const struct glm_state *s = &ctx->state;
        bool face_tracked = s->color_material && !((f == 0 && s->color_material_face == GL_BACK) ||
                                                   (f == 1 && s->color_material_face == GL_FRONT));
        GLenum tracked = face_tracked ? s->color_material_mode : 0;
        if (tracked && (pname == tracked || (tracked == GL_AMBIENT_AND_DIFFUSE && (pname == GL_AMBIENT || pname == GL_DIFFUSE)) ||
                        (pname == GL_AMBIENT_AND_DIFFUSE && (tracked == GL_AMBIENT || tracked == GL_DIFFUSE))))
            continue;
        struct glm_material *m = &ctx->state.material[f];
        switch (pname) {
        case GL_AMBIENT: memcpy(m->ambient, params, 16); break;
        case GL_DIFFUSE: memcpy(m->diffuse, params, 16); break;
        case GL_AMBIENT_AND_DIFFUSE: memcpy(m->ambient, params, 16); memcpy(m->diffuse, params, 16); break;
        case GL_SPECULAR: memcpy(m->specular, params, 16); break;
        case GL_EMISSION: memcpy(m->emission, params, 16); break;
        case GL_SHININESS: m->shininess = params[0]; break;
        case GL_COLOR_INDEXES: break;
        default: return glm_error(ctx, GL_INVALID_ENUM);
        }
    }
}
GLM_EXPORT void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    if (pname != GL_SHININESS) { /* the only scalar material parameter */
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    glMaterialfv(face, pname, &param);
}
GLM_EXPORT void glMateriali(GLenum face, GLenum pname, GLint param) { glMaterialf(face, pname, (GLfloat)param); }
GLM_EXPORT void glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    GLfloat f[4];
    int n = pname == GL_SHININESS ? 1 : 4;
    for (int i = 0; i < n; ++i) f[i] = pname == GL_SHININESS ? (GLfloat)params[i] : (GLfloat)((2.0 * params[i] + 1) / 4294967295.0);
    glMaterialfv(face, pname, f);
}

/* Colours read as integers map [-1, 1] onto the full GLint range. */
static GLint glm_float_to_int_color(float v) { return (GLint)(fmax(fmin(v, 1.0), -1.0) * 2147483647.0); }

GLM_EXPORT void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (face != GL_FRONT && face != GL_BACK) return glm_error(ctx, GL_INVALID_ENUM);
    glm_color_material_sync(ctx);
    const struct glm_material *m = &ctx->state.material[face == GL_BACK];
    switch (pname) {
    case GL_AMBIENT: memcpy(params, m->ambient, 16); break;
    case GL_DIFFUSE: memcpy(params, m->diffuse, 16); break;
    case GL_SPECULAR: memcpy(params, m->specular, 16); break;
    case GL_EMISSION: memcpy(params, m->emission, 16); break;
    case GL_SHININESS: *params = m->shininess; break;
    case GL_COLOR_INDEXES: params[0] = 0; params[1] = 1; params[2] = 1; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    GLfloat f[4] = {0};
    glGetMaterialfv(face, pname, f);
    int n = pname == GL_SHININESS ? 1 : pname == GL_COLOR_INDEXES ? 3 : 4;
    for (int i = 0; i < n; ++i) params[i] = n == 4 ? glm_float_to_int_color(f[i]) : (GLint)lroundf(f[i]);
}

GLM_EXPORT void glGetLightiv(GLenum light, GLenum pname, GLint *params)
{
    GLfloat f[4] = {0};
    glGetLightfv(light, pname, f);
    int n = pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION ? 4
          : pname == GL_SPOT_DIRECTION ? 3 : 1;
    bool color = pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR;
    for (int i = 0; i < n; ++i) params[i] = color ? glm_float_to_int_color(f[i]) : (GLint)lroundf(f[i]);
}

GLM_EXPORT void glColorMaterial(GLenum face, GLenum mode)
{
    GLM_CONTEXT(ctx);
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) return glm_error(ctx, GL_INVALID_ENUM);
    if (mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_SPECULAR && mode != GL_EMISSION &&
        mode != GL_AMBIENT_AND_DIFFUSE)
        return glm_error(ctx, GL_INVALID_ENUM);
    glm_color_material_sync(ctx);
    ctx->state.color_material_face = face;
    ctx->state.color_material_mode = mode;
    glm_color_material_sync(ctx);
}

/* ---- fog ----------------------------------------------------------------- */

GLM_EXPORT void glFogfv(GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    struct glm_state *s = &ctx->state;
    switch (pname) {
    case GL_FOG_MODE:
        if ((GLenum)params[0] != GL_EXP && (GLenum)params[0] != GL_EXP2 && (GLenum)params[0] != GL_LINEAR)
            return glm_error(ctx, GL_INVALID_ENUM);
        s->fog_mode = (GLenum)params[0];
        break;
    case GL_FOG_DENSITY: s->fog_density = params[0]; break;
    case GL_FOG_START: s->fog_start = params[0]; break;
    case GL_FOG_END: s->fog_end = params[0]; break;
    case GL_FOG_COLOR: memcpy(s->fog_color, params, 16); break;
    case GL_FOG_COORD_SRC:
        if ((GLenum)params[0] != GL_FOG_COORD && (GLenum)params[0] != GL_FRAGMENT_DEPTH)
            return glm_error(ctx, GL_INVALID_ENUM);
        s->fog_coord_source = (GLenum)params[0];
        break;
    case GL_FOG_DISTANCE_MODE_NV: {
        GLenum mode = (GLenum)params[0];
        if (mode != GL_EYE_RADIAL_NV && mode != GL_EYE_PLANE && mode != GL_EYE_PLANE_ABSOLUTE_NV)
            return glm_error(ctx, GL_INVALID_ENUM);
        s->fog_distance_mode = mode;
        break;
    }
    case GL_FOG_INDEX: break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glFogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_COLOR) {
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    glFogfv(pname, &param);
}
GLM_EXPORT void glFogi(GLenum pname, GLint param) { glFogf(pname, (GLfloat)param); }
GLM_EXPORT void glFogiv(GLenum pname, const GLint *params)
{
    GLfloat f[4];
    if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; ++i) f[i] = (GLfloat)((2.0 * params[i] + 1) / 4294967295.0);
    else f[0] = (GLfloat)params[0];
    glFogfv(pname, f);
}

/* ---- texture environment ------------------------------------------------- */

static struct glm_texenv *active_env(struct glm_context *ctx) { return &ctx->state.units[ctx->state.active_texture - GL_TEXTURE0].env; }

GLM_EXPORT void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (target == GL_TEXTURE_FILTER_CONTROL) {
        if (pname != GL_TEXTURE_LOD_BIAS) return glm_error(ctx, GL_INVALID_ENUM);
        active_env(ctx)->lod_bias = params[0];
        return;
    }
    if (target == GL_POINT_SPRITE) {
        if (pname != GL_COORD_REPLACE || (params[0] != 0 && params[0] != 1)) return glm_error(ctx, GL_INVALID_ENUM);
        active_env(ctx)->coord_replace = params[0] != 0;
        return;
    }
    if (target != GL_TEXTURE_ENV) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_texenv *e = active_env(ctx);
    GLenum v = (GLenum)params[0];
    /* The values Apple's implementation accepts (ATI_texture_env_combine3
       adds its three functions and GL_ZERO/GL_ONE sources). */
    bool ok = true;
    switch (pname) {
    case GL_TEXTURE_ENV_MODE:
        ok = v == GL_ADD || v == GL_BLEND || v == GL_REPLACE || v == GL_MODULATE || v == GL_DECAL || v == GL_COMBINE;
        break;
    case GL_COMBINE_RGB: case GL_COMBINE_ALPHA:
        ok = v == GL_ADD || v == GL_REPLACE || v == GL_MODULATE || v == GL_SUBTRACT || v == GL_ADD_SIGNED ||
             v == GL_INTERPOLATE || v == GL_MODULATE_ADD_ATI || v == GL_MODULATE_SIGNED_ADD_ATI ||
             v == GL_MODULATE_SUBTRACT_ATI || (pname == GL_COMBINE_RGB && (v == GL_DOT3_RGB || v == GL_DOT3_RGBA));
        break;
    case GL_SOURCE0_RGB: case GL_SOURCE1_RGB: case GL_SOURCE2_RGB:
    case GL_SOURCE0_ALPHA: case GL_SOURCE1_ALPHA: case GL_SOURCE2_ALPHA:
        ok = v == GL_ZERO || v == GL_ONE || v == GL_TEXTURE || (v >= GL_TEXTURE0 && v <= GL_TEXTURE7) || v == GL_CONSTANT ||
             v == GL_PRIMARY_COLOR || v == GL_PREVIOUS;
        break;
    case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB:
        ok = v == GL_SRC_COLOR || v == GL_ONE_MINUS_SRC_COLOR || v == GL_SRC_ALPHA || v == GL_ONE_MINUS_SRC_ALPHA;
        break;
    case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
        ok = v == GL_SRC_ALPHA || v == GL_ONE_MINUS_SRC_ALPHA;
        break;
    case GL_RGB_SCALE: case GL_ALPHA_SCALE:
        if (params[0] != 1 && params[0] != 2 && params[0] != 4) return glm_error(ctx, GL_INVALID_VALUE);
        break;
    }
    if (!ok) return glm_error(ctx, GL_INVALID_ENUM);
    switch (pname) {
    case GL_TEXTURE_ENV_MODE: e->mode = v; break;
    case GL_TEXTURE_ENV_COLOR: memcpy(e->color, params, 16); break;
    case GL_COMBINE_RGB: e->combine_rgb = v; break;
    case GL_COMBINE_ALPHA: e->combine_alpha = v; break;
    case GL_SOURCE0_RGB: case GL_SOURCE1_RGB: case GL_SOURCE2_RGB: e->source_rgb[pname - GL_SOURCE0_RGB] = v; break;
    case GL_SOURCE0_ALPHA: case GL_SOURCE1_ALPHA: case GL_SOURCE2_ALPHA: e->source_alpha[pname - GL_SOURCE0_ALPHA] = v; break;
    case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB: e->operand_rgb[pname - GL_OPERAND0_RGB] = v; break;
    case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA: e->operand_alpha[pname - GL_OPERAND0_ALPHA] = v; break;
    case GL_RGB_SCALE: e->rgb_scale = params[0]; break;
    case GL_ALPHA_SCALE: e->alpha_scale = params[0]; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if (pname == GL_TEXTURE_ENV_COLOR) {
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    glTexEnvfv(target, pname, &param);
}
GLM_EXPORT void glTexEnvi(GLenum target, GLenum pname, GLint param) { glTexEnvf(target, pname, (GLfloat)param); }
GLM_EXPORT void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    GLfloat f[4];
    if (pname == GL_TEXTURE_ENV_COLOR) for (int i = 0; i < 4; ++i) f[i] = (GLfloat)((2.0 * params[i] + 1) / 4294967295.0);
    else f[0] = (GLfloat)params[0];
    glTexEnvfv(target, pname, f);
}

GLM_EXPORT void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    GLM_CONTEXT(ctx);
    const struct glm_texenv *e = active_env(ctx);
    if (target == GL_TEXTURE_FILTER_CONTROL) {
        if (pname != GL_TEXTURE_LOD_BIAS) return glm_error(ctx, GL_INVALID_ENUM);
        *params = e->lod_bias;
        return;
    }
    if (target == GL_POINT_SPRITE) {
        if (pname != GL_COORD_REPLACE) return glm_error(ctx, GL_INVALID_ENUM);
        *params = e->coord_replace;
        return;
    }
    if (target != GL_TEXTURE_ENV) return glm_error(ctx, GL_INVALID_ENUM);
    switch (pname) {
    case GL_TEXTURE_ENV_MODE: *params = (GLfloat)e->mode; break;
    case GL_TEXTURE_ENV_COLOR: memcpy(params, e->color, 16); break;
    case GL_COMBINE_RGB: *params = (GLfloat)e->combine_rgb; break;
    case GL_COMBINE_ALPHA: *params = (GLfloat)e->combine_alpha; break;
    case GL_SOURCE0_RGB: case GL_SOURCE1_RGB: case GL_SOURCE2_RGB: *params = (GLfloat)e->source_rgb[pname - GL_SOURCE0_RGB]; break;
    case GL_SOURCE0_ALPHA: case GL_SOURCE1_ALPHA: case GL_SOURCE2_ALPHA:
        *params = (GLfloat)e->source_alpha[pname - GL_SOURCE0_ALPHA];
        break;
    case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB: *params = (GLfloat)e->operand_rgb[pname - GL_OPERAND0_RGB]; break;
    case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
        *params = (GLfloat)e->operand_alpha[pname - GL_OPERAND0_ALPHA];
        break;
    case GL_RGB_SCALE: *params = e->rgb_scale; break;
    case GL_ALPHA_SCALE: *params = e->alpha_scale; break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    GLfloat f[4] = {0};
    glGetTexEnvfv(target, pname, f);
    if (pname == GL_TEXTURE_ENV_COLOR)
        for (int i = 0; i < 4; ++i) params[i] = glm_float_to_int_color(f[i]);
    else
        params[0] = (GLint)lroundf(f[0]);
}

/* ---- texture coordinate generation --------------------------------------- */

GLM_EXPORT void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (coord < GL_S || coord > GL_Q) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_texgen *g = &ctx->state.units[ctx->state.active_texture - GL_TEXTURE0].gen[coord - GL_S];
    switch (pname) {
    case GL_TEXTURE_GEN_MODE: {
        /* Sphere maps for s and t, normal and reflection maps for s, t and r. */
        GLenum m = (GLenum)params[0];
        bool ok = m == GL_EYE_LINEAR || m == GL_OBJECT_LINEAR || (m == GL_SPHERE_MAP && coord <= GL_T) ||
                  ((m == GL_NORMAL_MAP || m == GL_REFLECTION_MAP) && coord <= GL_R);
        if (!ok) return glm_error(ctx, GL_INVALID_ENUM);
        g->mode = m;
        break;
    }
    case GL_OBJECT_PLANE: memcpy(g->object_plane, params, 16); break;
    case GL_EYE_PLANE: {
        double in[4] = {params[0], params[1], params[2], params[3]}, out[4];
        transform_plane(ctx, in, out);
        for (int i = 0; i < 4; ++i) g->eye_plane[i] = (float)out[i];
        break;
    }
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
/* Apple's scalar forms take the planes too; their other components are 0. */
GLM_EXPORT void glTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    GLfloat f[4] = {param, 0, 0, 0};
    glTexGenfv(coord, pname, f);
}
GLM_EXPORT void glTexGeni(GLenum coord, GLenum pname, GLint param) { glTexGenf(coord, pname, (GLfloat)param); }
GLM_EXPORT void glTexGend(GLenum coord, GLenum pname, GLdouble param) { glTexGenf(coord, pname, (GLfloat)param); }
GLM_EXPORT void glTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    GLfloat f[4];
    int n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
    for (int i = 0; i < n; ++i) f[i] = (GLfloat)params[i];
    glTexGenfv(coord, pname, f);
}
GLM_EXPORT void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    GLfloat f[4];
    int n = pname == GL_TEXTURE_GEN_MODE ? 1 : 4;
    for (int i = 0; i < n; ++i) f[i] = (GLfloat)params[i];
    glTexGenfv(coord, pname, f);
}

/* Eye planes are returned in eye coordinates, as stored. */
GLM_EXPORT void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
    GLM_CONTEXT(ctx);
    if (coord < GL_S || coord > GL_Q) return glm_error(ctx, GL_INVALID_ENUM);
    const struct glm_texgen *g = &ctx->state.units[ctx->state.active_texture - GL_TEXTURE0].gen[coord - GL_S];
    switch (pname) {
    case GL_TEXTURE_GEN_MODE: *params = (GLfloat)g->mode; break;
    case GL_OBJECT_PLANE: memcpy(params, g->object_plane, 16); break;
    case GL_EYE_PLANE: memcpy(params, g->eye_plane, 16); break;
    default: glm_error(ctx, GL_INVALID_ENUM);
    }
}
GLM_EXPORT void glGetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
    GLfloat f[4] = {0};
    glGetTexGenfv(coord, pname, f);
    for (int i = 0; i < (pname == GL_TEXTURE_GEN_MODE ? 1 : 4); ++i) params[i] = (GLint)lroundf(f[i]);
}
GLM_EXPORT void glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
    GLfloat f[4] = {0};
    glGetTexGenfv(coord, pname, f);
    for (int i = 0; i < (pname == GL_TEXTURE_GEN_MODE ? 1 : 4); ++i) params[i] = f[i];
}

/* ---- clip planes --------------------------------------------------------- */

GLM_EXPORT void glClipPlane(GLenum plane, const GLdouble *equation)
{
    GLM_CONTEXT(ctx);
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + GLM_MAX_CLIP_PLANES) return glm_error(ctx, GL_INVALID_ENUM);
    transform_plane(ctx, equation, ctx->state.clip_planes[plane - GL_CLIP_PLANE0]);
}

GLM_EXPORT void glGetClipPlane(GLenum plane, GLdouble *equation)
{
    GLM_CONTEXT(ctx);
    if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + GLM_MAX_CLIP_PLANES) return glm_error(ctx, GL_INVALID_ENUM);
    memcpy(equation, ctx->state.clip_planes[plane - GL_CLIP_PLANE0], 4 * sizeof(double));
}

/* ---- texture units ------------------------------------------------------- */

GLM_EXPORT void glActiveTexture(GLenum texture)
{
    GLM_CONTEXT(ctx);
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + GLM_MAX_TEXTURE_UNITS) return glm_error(ctx, GL_INVALID_ENUM);
    /* This selector routes subsequent setters; draws use explicit units.
       Its pending-state accounting remains in the marshal layer. */
    ctx->state.active_texture = texture;
}
GLM_EXPORT void glActiveTextureARB(GLenum texture) { glActiveTexture(texture); }
