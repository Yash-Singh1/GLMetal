/* Selection and feedback render modes (GL 2.1 5.2, 5.3).

   In GL_SELECT and GL_FEEDBACK modes primitives are not rasterized: the
   vertex stage (fixed function or a program, run on the GPU without
   rasterization: glm_backend_vertex_outputs) gives each vertex's clip
   position, colour and first texture coordinate; primitives are culled,
   split by polygon mode and clipped here, then counted as hits (with their
   window depth range) or written as feedback tokens. Vertex order follows
   Apple's implementation: polygons start at their last vertex (GL_POLYGON
   at its first), the order line stipple also shows. */
#include "glm_internal.h"
#include "programs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct rm_vertex {
    float clip[4], color[4], tex[4];
    float eye[4]; /* fixed function: for user clip planes */
};

/* ---- buffers ------------------------------------------------------------- */

static void select_write(struct glm_render_modes *r, GLuint value)
{
    if (r->select_used < r->select_size) r->select_buffer[r->select_used] = value;
    else r->select_overflow = true;
    ++r->select_used;
}

static void feedback_write(struct glm_render_modes *r, GLfloat value)
{
    if (r->feedback_used < r->feedback_size) r->feedback_buffer[r->feedback_used] = value;
    else r->feedback_overflow = true;
    ++r->feedback_used;
}

/* Window depths as hit records hold them: [0, 1] onto [0, 2^32 - 1]. */
static GLuint depth_word(float z)
{
    double v = (double)z * 4294967295.0;
    return v <= 0 ? 0 : v >= 4294967295.0 ? 0xffffffffu : (GLuint)v;
}

/* The pending hit (GL 2.1 5.2): name count, depth range, the names. */
static void flush_hit(struct glm_render_modes *r)
{
    if (!r->hit) return;
    select_write(r, (GLuint)r->name_depth);
    select_write(r, depth_word(r->hit_min));
    select_write(r, depth_word(r->hit_max));
    for (int i = 0; i < r->name_depth; ++i) select_write(r, r->names[i]);
    ++r->hits;
    r->hit = false;
    r->hit_min = 1;
    r->hit_max = 0;
}

static void hit_depth(struct glm_render_modes *r, float z)
{
    r->hit = true;
    if (z < r->hit_min) r->hit_min = z;
    if (z > r->hit_max) r->hit_max = z;
}

/* ---- render mode and buffers --------------------------------------------- */

GLM_EXPORT GLint glRenderMode(GLenum mode)
{
    GLM_CONTEXT(ctx, 0);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) {
        glm_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    if (mode != GL_RENDER && mode != GL_SELECT && mode != GL_FEEDBACK) {
        glm_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    if ((mode == GL_SELECT && !r->select_buffer) || (mode == GL_FEEDBACK && !r->feedback_buffer)) {
        glm_error(ctx, GL_INVALID_OPERATION);
        return 0;
    }
    GLint result = 0;
    if (r->mode == GL_SELECT) {
        flush_hit(r);
        result = r->select_overflow ? -1 : r->hits;
    } else if (r->mode == GL_FEEDBACK) {
        result = r->feedback_overflow ? -1 : r->feedback_used;
    }
    r->mode = mode;
    if (mode == GL_SELECT) {
        r->hits = 0;
        r->select_used = 0;
        r->select_overflow = false;
        r->hit = false;
        r->hit_min = 1;
        r->hit_max = 0;
        r->name_depth = 0;
    } else if (mode == GL_FEEDBACK) {
        r->feedback_used = 0;
        r->feedback_overflow = false;
    }
    __atomic_add_fetch(&ctx->state_serial, 1, __ATOMIC_RELAXED);
    return result;
}

GLM_EXPORT void glSelectBuffer(GLsizei size, GLuint *buffer)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate || r->mode == GL_SELECT) return glm_error(ctx, GL_INVALID_OPERATION);
    if (size < 0) return glm_error(ctx, GL_INVALID_VALUE);
    r->select_buffer = buffer;
    r->select_size = size;
}

GLM_EXPORT void glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate || r->mode == GL_FEEDBACK) return glm_error(ctx, GL_INVALID_OPERATION);
    if (size < 0) return glm_error(ctx, GL_INVALID_VALUE);
    if (type != GL_2D && type != GL_3D && type != GL_3D_COLOR && type != GL_3D_COLOR_TEXTURE && type != GL_4D_COLOR_TEXTURE)
        return glm_error(ctx, GL_INVALID_ENUM);
    r->feedback_buffer = buffer;
    r->feedback_size = size;
    r->feedback_type = type;
}

/* ---- the name stack (selection only; ignored otherwise) ------------------- */

GLM_EXPORT void glInitNames(void)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (r->mode != GL_SELECT) return;
    flush_hit(r);
    r->name_depth = 0;
}

GLM_EXPORT void glPushName(GLuint name)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (r->mode != GL_SELECT) return;
    flush_hit(r);
    if (r->name_depth == GLM_MAX_NAME_STACK) return glm_error(ctx, GL_STACK_OVERFLOW);
    r->names[r->name_depth++] = name;
}

GLM_EXPORT void glPopName(void)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (r->mode != GL_SELECT) return;
    flush_hit(r);
    if (!r->name_depth) return glm_error(ctx, GL_STACK_UNDERFLOW);
    --r->name_depth;
}

GLM_EXPORT void glLoadName(GLuint name)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (r->mode != GL_SELECT) return;
    if (!r->name_depth) return glm_error(ctx, GL_INVALID_OPERATION);
    flush_hit(r);
    r->names[r->name_depth - 1] = name;
}

GLM_EXPORT void glPassThrough(GLfloat token)
{
    GLM_CONTEXT(ctx);
    struct glm_render_modes *r = &ctx->render_modes;
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (r->mode != GL_FEEDBACK) return;
    feedback_write(r, (GLfloat)GL_PASS_THROUGH_TOKEN);
    feedback_write(r, token);
}

/* ---- primitives ---------------------------------------------------------- */

static void window_of(const struct glm_context *ctx, const float clip[4], float out[4])
{
    const struct glm_state *s = &ctx->state;
    float w = clip[3] != 0 ? clip[3] : 1e-30f;
    out[0] = (float)s->viewport[0] + (clip[0] / w + 1) * 0.5f * (float)s->viewport[2];
    out[1] = (float)s->viewport[1] + (clip[1] / w + 1) * 0.5f * (float)s->viewport[3];
    float n = (float)s->depth_range[0], f = (float)s->depth_range[1];
    out[2] = n + (f - n) * (clip[2] / w + 1) * 0.5f;
    out[3] = clip[3];
}

static void feedback_vertex(struct glm_context *ctx, const struct rm_vertex *v)
{
    struct glm_render_modes *r = &ctx->render_modes;
    float window[4];
    window_of(ctx, v->clip, window);
    GLenum type = r->feedback_type;
    feedback_write(r, window[0]);
    feedback_write(r, window[1]);
    if (type != GL_2D) feedback_write(r, window[2]);
    if (type == GL_4D_COLOR_TEXTURE) feedback_write(r, window[3]);
    if (type == GL_3D_COLOR || type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE)
        for (int i = 0; i < 4; ++i) feedback_write(r, v->color[i]);
    if (type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE)
        for (int i = 0; i < 4; ++i) feedback_write(r, v->tex[i]);
}

static void select_vertex(struct glm_context *ctx, const struct rm_vertex *v)
{
    float window[4];
    window_of(ctx, v->clip, window);
    hit_depth(&ctx->render_modes, window[2]);
}

static void lerp_vertex(const struct rm_vertex *a, const struct rm_vertex *b, float t, struct rm_vertex *out)
{
    for (int i = 0; i < 4; ++i) {
        out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
        out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i]) * t;
        out->eye[i] = a->eye[i] + (b->eye[i] - a->eye[i]) * t;
    }
}

/* Signed distance to clip plane `p`: the six view-volume planes, then the
   enabled user planes (fixed function: eye space). */
static float plane_distance(const struct glm_context *ctx, const struct rm_vertex *v, int p)
{
    const float *c = v->clip;
    switch (p) {
    case 0: return c[3] + c[0];
    case 1: return c[3] - c[0];
    case 2: return c[3] + c[1];
    case 3: return c[3] - c[1];
    case 4: return c[3] + c[2];
    case 5: return c[3] - c[2];
    }
    const float *plane = ctx->render_modes.eye_planes[p - 6];
    return plane[0] * v->eye[0] + plane[1] * v->eye[1] + plane[2] * v->eye[2] + plane[3] * v->eye[3];
}

static int plane_count(const struct glm_context *ctx) { return 6 + ctx->render_modes.eye_plane_count; }

static bool point_inside(const struct glm_context *ctx, const struct rm_vertex *v)
{
    for (int p = 0; p < plane_count(ctx); ++p)
        if (plane_distance(ctx, v, p) < 0) return false;
    return true;
}

/* Clips a line; false when nothing is left. `*start_clipped` reports a
   moved first end. */
static bool clip_line(const struct glm_context *ctx, const struct rm_vertex *a, const struct rm_vertex *b,
                      struct rm_vertex *out_a, struct rm_vertex *out_b, bool *start_clipped, bool *end_clipped)
{
    float t0 = 0, t1 = 1;
    for (int p = 0; p < plane_count(ctx); ++p) {
        float da = plane_distance(ctx, a, p), db = plane_distance(ctx, b, p);
        if (da < 0 && db < 0) return false;
        if (da < 0) t0 = fmaxf(t0, da / (da - db));
        else if (db < 0) t1 = fminf(t1, da / (da - db));
    }
    if (t0 > t1) return false;
    *start_clipped = t0 > 0;
    *end_clipped = t1 < 1;
    if (t0 > 0) lerp_vertex(a, b, t0, out_a); else *out_a = *a;
    if (t1 < 1) lerp_vertex(a, b, t1, out_b); else *out_b = *b;
    return true;
}

/* Sutherland-Hodgman against every plane in turn; returns the count. */
static int clip_polygon(const struct glm_context *ctx, struct rm_vertex *poly, int count, int capacity)
{
    struct rm_vertex *scratch = malloc((size_t)capacity * sizeof *scratch);
    if (!scratch) return 0;
    for (int p = 0; p < plane_count(ctx) && count; ++p) {
        int n = 0;
        for (int i = 0; i < count; ++i) {
            const struct rm_vertex *a = &poly[i], *b = &poly[(i + 1) % count];
            float da = plane_distance(ctx, a, p), db = plane_distance(ctx, b, p);
            if (da >= 0 && n < capacity) scratch[n++] = *a;
            if ((da >= 0) != (db >= 0) && n < capacity) lerp_vertex(a, b, da / (da - db), &scratch[n++]);
        }
        memcpy(poly, scratch, (size_t)n * sizeof *poly);
        count = n;
    }
    free(scratch);
    return count;
}

static void emit_point(struct glm_context *ctx, const struct rm_vertex *v)
{
    if (!point_inside(ctx, v)) return;
    if (ctx->render_modes.mode == GL_SELECT) return select_vertex(ctx, v);
    feedback_write(&ctx->render_modes, (GLfloat)GL_POINT_TOKEN);
    feedback_vertex(ctx, v);
}

/* A line segment; `*reset` asks for GL_LINE_RESET_TOKEN and is set for the
   next segment when this one ends clipped (a break in the line). */
static void emit_line(struct glm_context *ctx, const struct rm_vertex *a, const struct rm_vertex *b, bool *reset)
{
    struct rm_vertex ca, cb;
    bool start_clipped, end_clipped;
    if (!clip_line(ctx, a, b, &ca, &cb, &start_clipped, &end_clipped)) {
        *reset = true;
        return;
    }
    if (ctx->render_modes.mode == GL_SELECT) {
        select_vertex(ctx, &ca);
        select_vertex(ctx, &cb);
    } else {
        feedback_write(&ctx->render_modes, (GLfloat)(*reset || start_clipped ? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN));
        feedback_vertex(ctx, &ca);
        feedback_vertex(ctx, &cb);
    }
    *reset = end_clipped;
}

/* One polygon given in Apple's order (`cycle`), with its GL-order facing. */
static void emit_polygon(struct glm_context *ctx, const struct rm_vertex *vertices, const uint32_t *cycle, int corners,
                         const uint8_t *edge_flags)
{
    const struct glm_state *s = &ctx->state;
    /* Facing from the projected area (orientation is the same in any
       rotation of the cycle). */
    double area = 0;
    for (int i = 0; i < corners; ++i) {
        const float *a = vertices[cycle[i]].clip, *b = vertices[cycle[(i + 1) % corners]].clip;
        double aw = a[3] != 0 ? a[3] : 1e-30, bw = b[3] != 0 ? b[3] : 1e-30;
        area += (a[0] / aw) * (b[1] / bw) - (b[0] / bw) * (a[1] / aw);
    }
    bool front = (area > 0) == (s->front_face == GL_CCW);
    if (s->cull_face && (s->cull_mode == GL_FRONT_AND_BACK || (front ? s->cull_mode == GL_FRONT : s->cull_mode == GL_BACK)))
        return;
    GLenum mode = s->polygon_mode[front ? 0 : 1];
    if (mode == GL_POINT) {
        for (int i = 0; i < corners; ++i)
            if (!edge_flags || edge_flags[cycle[i]]) emit_point(ctx, &vertices[cycle[i]]);
        return;
    }
    if (mode == GL_LINE) {
        bool reset = true;
        for (int i = 0; i < corners; ++i) {
            if (edge_flags && !edge_flags[cycle[i]]) continue;
            emit_line(ctx, &vertices[cycle[i]], &vertices[cycle[(i + 1) % corners]], &reset);
        }
        return;
    }
    int capacity = corners + plane_count(ctx) + 2;
    struct rm_vertex *poly = malloc((size_t)capacity * sizeof *poly);
    if (!poly) return;
    for (int i = 0; i < corners; ++i) poly[i] = vertices[cycle[i]];
    int n = clip_polygon(ctx, poly, corners, capacity);
    if (n >= 3 || (n && ctx->render_modes.mode == GL_SELECT)) {
        if (ctx->render_modes.mode == GL_SELECT) {
            for (int i = 0; i < n; ++i) select_vertex(ctx, &poly[i]);
        } else {
            feedback_write(&ctx->render_modes, (GLfloat)GL_POLYGON_TOKEN);
            feedback_write(&ctx->render_modes, (GLfloat)n);
            for (int i = 0; i < n; ++i) feedback_vertex(ctx, &poly[i]);
        }
    }
    free(poly);
}

/* glDrawArrays, glDrawElements and glBegin/glEnd in GL_SELECT or
   GL_FEEDBACK mode (vertex.c submit). */
void glm_render_mode_submit(struct glm_context *ctx, GLenum mode, const float *vertices, uint32_t vertex_count,
                            const uint32_t *indices, uint32_t index_count, uint32_t instance, uint32_t vertex_id_base)
{
    struct glm_render_modes *r = &ctx->render_modes;
    const struct glm_state *s = &ctx->state;
    if (!vertex_count || !index_count) return;
    float *outputs = malloc((size_t)vertex_count * 12 * sizeof(float));
    struct rm_vertex *v = calloc(vertex_count, sizeof *v);
    if (!outputs || !v) {
        free(outputs);
        free(v);
        return glm_error(ctx, GL_OUT_OF_MEMORY);
    }
    struct glm_draw draw = {.mode = mode, .vertices = vertices, .vertex_count = vertex_count, .instance_count = 1,
                            .base_instance = instance, .vertex_id_base = vertex_id_base,
                            .attrib_count = GLM_MAX_ATTRIBS, .attrib_mask = (1u << GLM_MAX_ATTRIBS) - 1,
                            .index_count = vertex_count};
    if (!glm_backend_vertex_outputs(ctx, &draw, outputs)) {
        free(outputs);
        free(v);
        return;
    }
    bool fixed_function = !glm_current_program(ctx) && !s->vertex_program;
    const float *modelview = s->modelview.m[s->modelview.depth];
    const struct glm_texture_unit *unit0 = &s->units[0];
    bool texture_output = !fixed_function || unit0->enabled_1d || unit0->enabled_2d || unit0->enabled_3d ||
                          unit0->enabled_cube || unit0->enabled_rect;
    const float *texture_matrix = s->texture[0].m[s->texture[0].depth];
    for (uint32_t i = 0; i < vertex_count; ++i) {
        const float *o = outputs + (size_t)i * 12;
        /* Back from Metal's conventions to GL clip space. */
        v[i].clip[0] = o[0];
        v[i].clip[1] = -o[1];
        v[i].clip[2] = 2 * o[2] - o[3];
        v[i].clip[3] = o[3];
        memcpy(v[i].color, o + 4, sizeof v[i].color);
        /* Program colours are clamped (GL_CLAMP_VERTEX_COLOR's default). */
        if (!fixed_function)
            for (int c = 0; c < 4; ++c) v[i].color[c] = v[i].color[c] < 0 ? 0 : v[i].color[c] > 1 ? 1 : v[i].color[c];
        const float *attrib = vertices + (size_t)i * GLM_MAX_ATTRIBS * 4;
        if (texture_output) {
            memcpy(v[i].tex, o + 8, sizeof v[i].tex);
        } else {
            /* Fixed function without texturing has no texture coordinate
               output: the current one through the texture matrix. */
            const float *t = attrib + GLM_ATTR_TEX0 * 4;
            for (int c = 0; c < 4; ++c)
                v[i].tex[c] = texture_matrix[c] * t[0] + texture_matrix[4 + c] * t[1] + texture_matrix[8 + c] * t[2] +
                              texture_matrix[12 + c] * t[3];
        }
        if (fixed_function) {
            const float *p = attrib + GLM_ATTR_POSITION * 4;
            for (int c = 0; c < 4; ++c)
                v[i].eye[c] = modelview[c] * p[0] + modelview[4 + c] * p[1] + modelview[8 + c] * p[2] + modelview[12 + c] * p[3];
        }
    }
    free(outputs);
    /* User clip planes (fixed function: compared in eye space). */
    r->eye_plane_count = 0;
    if (fixed_function)
        for (int p = 0; p < GLM_MAX_CLIP_PLANES; ++p)
            if (s->clip_plane_enabled[p]) memcpy(r->eye_planes[r->eye_plane_count++], s->clip_planes[p], 4 * sizeof(float));
    /* Edge flags mark boundary edges of separate triangles, quads and
       polygons only. */
    const uint8_t *flags = mode == GL_TRIANGLES || mode == GL_QUADS || mode == GL_POLYGON ? ctx->edge_flags : NULL;
    const uint32_t *q = indices;
    uint32_t n = index_count;
    switch (mode) {
    case GL_POINTS:
        for (uint32_t i = 0; i < n; ++i) emit_point(ctx, &v[q[i]]);
        break;
    case GL_LINES:
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            bool reset = true;
            emit_line(ctx, &v[q[i]], &v[q[i + 1]], &reset);
        }
        break;
    case GL_LINE_STRIP: case GL_LINE_LOOP: {
        bool reset = true;
        for (uint32_t i = 0; i + 1 < n; ++i) emit_line(ctx, &v[q[i]], &v[q[i + 1]], &reset);
        if (mode == GL_LINE_LOOP && n > 2) emit_line(ctx, &v[q[n - 1]], &v[q[0]], &reset);
        break;
    }
    case GL_TRIANGLES:
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            uint32_t cycle[3] = {q[i + 2], q[i], q[i + 1]};
            emit_polygon(ctx, v, cycle, 3, flags);
        }
        break;
    case GL_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 2 < n; ++i) {
            uint32_t a = i & 1 ? q[i + 1] : q[i], b = i & 1 ? q[i] : q[i + 1], c = q[i + 2];
            uint32_t cycle[3] = {c, a, b};
            emit_polygon(ctx, v, cycle, 3, NULL);
        }
        break;
    case GL_TRIANGLE_FAN:
        for (uint32_t i = 1; i + 1 < n; ++i) {
            uint32_t cycle[3] = {q[i + 1], q[0], q[i]};
            emit_polygon(ctx, v, cycle, 3, NULL);
        }
        break;
    case GL_QUADS:
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            uint32_t cycle[4] = {q[i + 3], q[i], q[i + 1], q[i + 2]};
            emit_polygon(ctx, v, cycle, 4, flags);
        }
        break;
    case GL_QUAD_STRIP:
        for (uint32_t i = 0; i + 3 < n; i += 2) {
            uint32_t cycle[4] = {q[i + 3], q[i + 2], q[i], q[i + 1]};
            emit_polygon(ctx, v, cycle, 4, NULL);
        }
        break;
    case GL_POLYGON:
        if (n >= 3) emit_polygon(ctx, v, q, (int)n, flags);
        break;
    default:
        break;
    }
    free(v);
}

/* The raster position in GL_SELECT mode (a hit at its depth when valid) and
   the pixel rectangle primitives in GL_FEEDBACK mode (raster.c). */
void glm_render_mode_raster(struct glm_context *ctx, GLenum token)
{
    struct glm_render_modes *r = &ctx->render_modes;
    const struct glm_state *s = &ctx->state;
    if (!s->raster_valid) return;
    if (r->mode == GL_SELECT) {
        if (!token) hit_depth(r, s->raster_pos[2]);
        return;
    }
    if (r->mode != GL_FEEDBACK || !token) return;
    feedback_write(r, (GLfloat)token);
    GLenum type = r->feedback_type;
    feedback_write(r, s->raster_pos[0]);
    feedback_write(r, s->raster_pos[1]);
    if (type != GL_2D) feedback_write(r, s->raster_pos[2]);
    if (type == GL_4D_COLOR_TEXTURE) feedback_write(r, s->raster_pos[3]);
    if (type == GL_3D_COLOR || type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE)
        for (int i = 0; i < 4; ++i) feedback_write(r, s->raster_color[i]);
    if (type == GL_3D_COLOR_TEXTURE || type == GL_4D_COLOR_TEXTURE)
        for (int i = 0; i < 4; ++i) feedback_write(r, s->raster_texcoord[0][i]);
}
