/* Evaluators (GL 2.1 5.1): one- and two-dimensional Bézier maps evaluated
 * on the CPU into the immediate-mode vertices they stand for. glEvalCoord
 * evaluates every enabled map of its dimension and issues the vertex with
 * those values in place of the current ones, which it leaves unchanged;
 * without a vertex map it does nothing. glEvalMesh and glEvalPoint walk the
 * glMapGrid grid. Automatic normals are the normalized cross product of the
 * surface's derivatives in the unit parameter square (as Apple's are). */
#include "glm_internal.h"
#include "programs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

GLM_HIDDEN void glm_attrib(int slot, float x, float y, float z, float w);

/* Map index from the target: GL_MAP1_COLOR_4 ... GL_MAP1_VERTEX_4 in
   enum order, likewise for GL_MAP2_*. */
enum { MAP_COLOR, MAP_INDEX, MAP_NORMAL, MAP_TEX1, MAP_TEX2, MAP_TEX3, MAP_TEX4, MAP_VERTEX3, MAP_VERTEX4 };
static const int components[GLM_EVAL_MAPS] = {4, 1, 3, 1, 2, 3, 4, 3, 4};

/* Dimension (0: 1D, 1: 2D) and index of a map target; false if none. */
static bool map_target(GLenum target, int *dim, int *index)
{
    if (target >= GL_MAP1_COLOR_4 && target <= GL_MAP1_VERTEX_4) {
        *dim = 0;
        *index = (int)(target - GL_MAP1_COLOR_4);
        return true;
    }
    if (target >= GL_MAP2_COLOR_4 && target <= GL_MAP2_VERTEX_4) {
        *dim = 1;
        *index = (int)(target - GL_MAP2_COLOR_4);
        return true;
    }
    return false;
}

int glm_eval_components(GLenum target)
{
    int dim, index;
    return map_target(target, &dim, &index) ? components[index] : 0;
}

void glm_eval_init(struct glm_context *ctx)
{
    /* The default maps: order 1 over [0, 1], the initial current value. */
    static const float defaults[GLM_EVAL_MAPS][4] = {
        {1, 1, 1, 1}, {1}, {0, 0, 1}, {0}, {0, 0}, {0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0, 1}};
    for (int d = 0; d < 2; ++d)
        for (int i = 0; i < GLM_EVAL_MAPS; ++i) {
            struct glm_eval_map *m = &ctx->eval_maps[d][i];
            m->size = components[i];
            m->order[0] = m->order[1] = 1;
            m->domain[0] = m->domain[2] = 0;
            m->domain[1] = m->domain[3] = 1;
            m->points = malloc(sizeof defaults[i]);
            memcpy(m->points, defaults[i], sizeof defaults[i]);
        }
    static const float attrib_default[4] = {0, 0, 0, 1};
    for (int d = 0; d < 2; ++d)
        for (int i = 0; i < GLM_MAX_ATTRIBS; ++i) {
            struct glm_eval_map *m = &ctx->attrib_maps[d][i];
            m->size = 4;
            m->order[0] = m->order[1] = 1;
            m->domain[0] = m->domain[2] = 0;
            m->domain[1] = m->domain[3] = 1;
            m->points = malloc(sizeof attrib_default);
            memcpy(m->points, attrib_default, sizeof attrib_default);
        }
    struct glm_state *s = &ctx->state;
    s->eval_grid_segments[0][0] = 1;
    s->eval_grid_segments[1][0] = s->eval_grid_segments[1][1] = 1;
    s->eval_grid_domain[0][0] = 0;
    s->eval_grid_domain[0][1] = 1;
    s->eval_grid_domain[1][0] = s->eval_grid_domain[1][2] = 0;
    s->eval_grid_domain[1][1] = s->eval_grid_domain[1][3] = 1;
}

void glm_eval_destroy(struct glm_context *ctx)
{
    for (int d = 0; d < 2; ++d) {
        for (int i = 0; i < GLM_EVAL_MAPS; ++i) free(ctx->eval_maps[d][i].points);
        for (int i = 0; i < GLM_MAX_ATTRIBS; ++i) free(ctx->attrib_maps[d][i].points);
    }
}

bool *glm_eval_capability(struct glm_context *ctx, GLenum cap)
{
    int dim, index;
    if (cap == GL_AUTO_NORMAL) return &ctx->state.auto_normal;
    return map_target(cap, &dim, &index) ? &ctx->state.map_enabled[dim][index] : NULL;
}

/* ---- maps ------------------------------------------------------------------ */

/* Stores `points` (k components each, strided) in `m`. */
static void store_map(struct glm_context *ctx, struct glm_eval_map *m, int k, int dim, const float u[2], int ustride,
                      int uorder, const float v[2], int vstride, int vorder, const void *points, bool is_double)
{
    if (!dim) vorder = 1;
    float *packed = malloc((size_t)uorder * (size_t)vorder * (size_t)k * sizeof *packed);
    if (!packed) return glm_error(ctx, GL_OUT_OF_MEMORY);
    for (int i = 0; i < uorder; ++i)
        for (int j = 0; j < vorder; ++j)
            for (int c = 0; c < k; ++c) {
                size_t at = (size_t)i * (size_t)ustride + (size_t)j * (size_t)(dim ? vstride : 0) + (size_t)c;
                packed[((size_t)i * (size_t)vorder + (size_t)j) * (size_t)k + (size_t)c] =
                    is_double ? (float)((const double *)points)[at] : ((const float *)points)[at];
            }
    free(m->points);
    m->points = packed;
    m->size = k;
    m->order[0] = uorder;
    m->order[1] = vorder;
    m->domain[0] = u[0];
    m->domain[1] = u[1];
    m->domain[2] = dim ? v[0] : 0;
    m->domain[3] = dim ? v[1] : 1;
}

/* Checks in Apple's order: the domain and orders, the target, the strides. */
static void map(struct glm_context *ctx, GLenum target, int dim, const float u[2], int ustride, int uorder,
                const float v[2], int vstride, int vorder, const void *points, bool is_double)
{
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (u[0] == u[1] || uorder < 1 || uorder > GLM_MAX_EVAL_ORDER) return glm_error(ctx, GL_INVALID_VALUE);
    if (dim && (v[0] == v[1] || vorder < 1 || vorder > GLM_MAX_EVAL_ORDER)) return glm_error(ctx, GL_INVALID_VALUE);
    int target_dim, index;
    if (!map_target(target, &target_dim, &index) || target_dim != dim) return glm_error(ctx, GL_INVALID_ENUM);
    int k = components[index];
    if (ustride < k || (dim && vstride < k)) return glm_error(ctx, GL_INVALID_VALUE);
    if (!points) return glm_error(ctx, GL_INVALID_VALUE);
    store_map(ctx, &ctx->eval_maps[dim][index], k, dim, u, ustride, uorder, v, vstride, vorder, points, is_double);
}

GLM_EXPORT void glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {u1, u2};
    map(ctx, target, 0, u, stride, order, NULL, 0, 0, points, false);
}
GLM_EXPORT void glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {(float)u1, (float)u2};
    map(ctx, target, 0, u, stride, order, NULL, 0, 0, points, true);
}
GLM_EXPORT void glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2,
                        GLint vstride, GLint vorder, const GLfloat *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {u1, u2}, v[2] = {v1, v2};
    map(ctx, target, 1, u, ustride, uorder, v, vstride, vorder, points, false);
}
GLM_EXPORT void glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2,
                        GLint vstride, GLint vorder, const GLdouble *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {(float)u1, (float)u2}, v[2] = {(float)v1, (float)v2};
    map(ctx, target, 1, u, ustride, uorder, v, vstride, vorder, points, true);
}

/* ---- attribute maps (APPLE_vertex_program_evaluators) ----------------------- */

/* One map per attribute and dimension, of 1-4 components; evaluated (with a
   vertex program or GLSL program in use) into the generic attributes, the
   vertex from attribute 0's map ahead of the conventional vertex maps. */
static void attrib_map(struct glm_context *ctx, GLuint index, GLuint size, int dim, const float u[2], int ustride,
                       int uorder, const float v[2], int vstride, int vorder, const void *points, bool is_double)
{
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (index >= GLM_MAX_ATTRIBS || size < 1 || size > 4) return glm_error(ctx, GL_INVALID_VALUE);
    if (u[0] == u[1] || uorder < 1 || uorder > GLM_MAX_EVAL_ORDER) return glm_error(ctx, GL_INVALID_VALUE);
    if (dim && (v[0] == v[1] || vorder < 1 || vorder > GLM_MAX_EVAL_ORDER)) return glm_error(ctx, GL_INVALID_VALUE);
    int k = (int)size;
    if (ustride < k || (dim && vstride < k) || !points) return glm_error(ctx, GL_INVALID_VALUE);
    store_map(ctx, &ctx->attrib_maps[dim][index], k, dim, u, ustride, uorder, v, vstride, vorder, points, is_double);
}

GLM_EXPORT void glMapVertexAttrib1fAPPLE(GLuint index, GLuint size, GLfloat u1, GLfloat u2, GLint stride, GLint order,
                                         const GLfloat *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {u1, u2};
    attrib_map(ctx, index, size, 0, u, stride, order, NULL, 0, 0, points, false);
}
GLM_EXPORT void glMapVertexAttrib1dAPPLE(GLuint index, GLuint size, GLdouble u1, GLdouble u2, GLint stride, GLint order,
                                         const GLdouble *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {(float)u1, (float)u2};
    attrib_map(ctx, index, size, 0, u, stride, order, NULL, 0, 0, points, true);
}
GLM_EXPORT void glMapVertexAttrib2fAPPLE(GLuint index, GLuint size, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
                                         GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {u1, u2}, v[2] = {v1, v2};
    attrib_map(ctx, index, size, 1, u, ustride, uorder, v, vstride, vorder, points, false);
}
GLM_EXPORT void glMapVertexAttrib2dAPPLE(GLuint index, GLuint size, GLdouble u1, GLdouble u2, GLint ustride,
                                         GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder,
                                         const GLdouble *points)
{
    GLM_CONTEXT(ctx);
    const float u[2] = {(float)u1, (float)u2}, v[2] = {(float)v1, (float)v2};
    attrib_map(ctx, index, size, 1, u, ustride, uorder, v, vstride, vorder, points, true);
}

/* The enable bit of attribute `index`'s map `pname`, NULL after an error. */
static uint16_t *attrib_map_enable(struct glm_context *ctx, GLuint index, GLenum pname)
{
    if (pname != GL_VERTEX_ATTRIB_MAP1_APPLE && pname != GL_VERTEX_ATTRIB_MAP2_APPLE)
        return glm_error(ctx, GL_INVALID_ENUM), NULL;
    if (index >= GLM_MAX_ATTRIBS) return glm_error(ctx, GL_INVALID_VALUE), NULL;
    return &ctx->state.attrib_maps_enabled[pname == GL_VERTEX_ATTRIB_MAP2_APPLE];
}
GLM_EXPORT void glEnableVertexAttribAPPLE(GLuint index, GLenum pname)
{
    GLM_CONTEXT(ctx);
    uint16_t *bits = attrib_map_enable(ctx, index, pname);
    if (bits) *bits |= (uint16_t)(1u << index);
}
GLM_EXPORT void glDisableVertexAttribAPPLE(GLuint index, GLenum pname)
{
    GLM_CONTEXT(ctx);
    uint16_t *bits = attrib_map_enable(ctx, index, pname);
    if (bits) *bits &= (uint16_t)~(1u << index);
}
GLM_EXPORT GLboolean glIsVertexAttribEnabledAPPLE(GLuint index, GLenum pname)
{
    GLM_CONTEXT(ctx, GL_FALSE);
    uint16_t *bits = attrib_map_enable(ctx, index, pname);
    return bits && (*bits >> index & 1) ? GL_TRUE : GL_FALSE;
}

int glm_eval_attrib_query(struct glm_context *ctx, GLuint index, GLenum pname, double *v)
{
    if (pname < GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE || pname > GL_VERTEX_ATTRIB_MAP2_DOMAIN_APPLE) return -1;
    int dim = pname >= GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE;
    const struct glm_eval_map *m = &ctx->attrib_maps[dim][index];
    switch (pname - (dim ? GL_VERTEX_ATTRIB_MAP2_SIZE_APPLE : GL_VERTEX_ATTRIB_MAP1_SIZE_APPLE)) {
    case 0: v[0] = m->size; return 1;
    case 1: {
        int n = m->order[0] * m->order[1] * m->size;
        for (int i = 0; i < n; ++i) v[i] = m->points[i];
        return n;
    }
    case 2: v[0] = m->order[0]; v[1] = m->order[1]; return dim + 1;
    default: for (int i = 0; i < 4; ++i) v[i] = m->domain[i]; return 2 * (dim + 1);
    }
}

/* Values glGetMap answers (as doubles); 0 after an error. */
static int get_map(struct glm_context *ctx, GLenum target, GLenum query, double *out, size_t room)
{
    int dim, index;
    if (!map_target(target, &dim, &index)) return glm_error(ctx, GL_INVALID_ENUM), 0;
    const struct glm_eval_map *m = &ctx->eval_maps[dim][index];
    switch (query) {
    case GL_COEFF: {
        size_t n = (size_t)m->order[0] * (size_t)m->order[1] * (size_t)components[index];
        if (n > room) n = room;
        for (size_t i = 0; i < n; ++i) out[i] = m->points[i];
        return (int)n;
    }
    case GL_ORDER:
        out[0] = m->order[0];
        out[1] = m->order[1];
        return dim + 1;
    case GL_DOMAIN:
        for (int i = 0; i < 4; ++i) out[i] = m->domain[i];
        return 2 * (dim + 1);
    }
    glm_error(ctx, GL_INVALID_ENUM);
    return 0;
}
enum { MAP_VALUES = GLM_MAX_EVAL_ORDER * GLM_MAX_EVAL_ORDER * 4 };
GLM_EXPORT void glGetMapdv(GLenum target, GLenum query, GLdouble *v)
{
    GLM_CONTEXT(ctx);
    double values[MAP_VALUES];
    int n = get_map(ctx, target, query, values, MAP_VALUES);
    for (int i = 0; i < n; ++i) v[i] = values[i];
}
GLM_EXPORT void glGetMapfv(GLenum target, GLenum query, GLfloat *v)
{
    GLM_CONTEXT(ctx);
    double values[MAP_VALUES];
    int n = get_map(ctx, target, query, values, MAP_VALUES);
    for (int i = 0; i < n; ++i) v[i] = (GLfloat)values[i];
}
GLM_EXPORT void glGetMapiv(GLenum target, GLenum query, GLint *v)
{
    GLM_CONTEXT(ctx);
    double values[MAP_VALUES];
    int n = get_map(ctx, target, query, values, MAP_VALUES);
    for (int i = 0; i < n; ++i) v[i] = (GLint)lround(values[i]);
}

/* ---- grids ----------------------------------------------------------------- */

GLM_EXPORT void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    GLM_CONTEXT(ctx);
    if (un <= 0) return glm_error(ctx, GL_INVALID_VALUE);
    struct glm_state *s = &ctx->state;
    s->eval_grid_segments[0][0] = un;
    s->eval_grid_domain[0][0] = u1;
    s->eval_grid_domain[0][1] = u2;
}
GLM_EXPORT void glMapGrid1d(GLint un, GLdouble u1, GLdouble u2) { glMapGrid1f(un, (GLfloat)u1, (GLfloat)u2); }
GLM_EXPORT void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    GLM_CONTEXT(ctx);
    if (un <= 0 || vn <= 0) return glm_error(ctx, GL_INVALID_VALUE);
    struct glm_state *s = &ctx->state;
    s->eval_grid_segments[1][0] = un;
    s->eval_grid_segments[1][1] = vn;
    s->eval_grid_domain[1][0] = u1;
    s->eval_grid_domain[1][1] = u2;
    s->eval_grid_domain[1][2] = v1;
    s->eval_grid_domain[1][3] = v2;
}
GLM_EXPORT void glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
    glMapGrid2f(un, (GLfloat)u1, (GLfloat)u2, vn, (GLfloat)v1, (GLfloat)v2);
}

/* ---- evaluation ------------------------------------------------------------ */

/* Bernstein evaluation of `order` control points of k components (stride
   between points) at t by de Casteljau; with `derivative`, also dp/dt. */
static void casteljau(const float *points, size_t stride, int order, int k, float t, float *out, float *derivative)
{
    float work[GLM_MAX_EVAL_ORDER][4];
    for (int i = 0; i < order; ++i)
        for (int c = 0; c < k; ++c) work[i][c] = points[(size_t)i * stride + (size_t)c];
    for (int level = order - 1; level > 0; --level) {
        if (level == 1 && derivative)
            for (int c = 0; c < k; ++c) derivative[c] = (float)(order - 1) * (work[1][c] - work[0][c]);
        for (int i = 0; i < level; ++i)
            for (int c = 0; c < k; ++c) work[i][c] = (1 - t) * work[i][c] + t * work[i + 1][c];
    }
    if (derivative && order == 1)
        for (int c = 0; c < k; ++c) derivative[c] = 0;
    for (int c = 0; c < k; ++c) out[c] = work[0][c];
}

static float unit_parameter(float u, float u1, float u2) { return (u - u1) / (u2 - u1); }

static void eval1(const struct glm_eval_map *m, int k, float u, float *out)
{
    casteljau(m->points, (size_t)k, m->order[0], k, unit_parameter(u, m->domain[0], m->domain[1]), out, NULL);
}

/* The surface at (u, v); with du/dv, its derivatives in the unit square. */
static void eval2(const struct glm_eval_map *m, int k, float u, float v, float *out, float *du, float *dv)
{
    float s = unit_parameter(u, m->domain[0], m->domain[1]), t = unit_parameter(v, m->domain[2], m->domain[3]);
    /* Along v for each row of u, then along u over those. */
    float rows[GLM_MAX_EVAL_ORDER][4], row_dv[GLM_MAX_EVAL_ORDER][4];
    for (int i = 0; i < m->order[0]; ++i)
        casteljau(m->points + (size_t)i * (size_t)m->order[1] * (size_t)k, (size_t)k, m->order[1], k, t, rows[i],
                  dv ? row_dv[i] : NULL);
    casteljau(&rows[0][0], 4, m->order[0], k, s, out, du);
    if (dv) casteljau(&row_dv[0][0], 4, m->order[0], k, s, dv, NULL);
}

/* An attribute map's value, unused components (0, 0, 0, 1). */
static void eval_attrib(const struct glm_eval_map *m, int dim, float u, float v, float *out)
{
    out[0] = out[1] = out[2] = 0;
    out[3] = 1;
    if (dim) eval2(m, m->size, u, v, out, NULL, NULL);
    else eval1(m, m->size, u, out);
}

static void eval_coord(struct glm_context *ctx, int dim, float u, float v)
{
    const struct glm_state *s = &ctx->state;
    const bool *enabled = s->map_enabled[dim];
    /* Attribute maps evaluate for vertex programs (ARB or GLSL) only. */
    uint16_t attribs = s->vertex_program || glm_current_program(ctx) ? s->attrib_maps_enabled[dim] : 0;
    int vertex = enabled[MAP_VERTEX4] ? MAP_VERTEX4 : enabled[MAP_VERTEX3] ? MAP_VERTEX3 : -1;
    if (vertex < 0 && !(attribs & 1)) return;
    const struct glm_eval_map *maps = ctx->eval_maps[dim];
    float saved[GLM_MAX_ATTRIBS][4];
    memcpy(saved, s->current, sizeof saved);
    float value[4];
    if (enabled[MAP_COLOR]) {
        if (dim) eval2(&maps[MAP_COLOR], 4, u, v, value, NULL, NULL);
        else eval1(&maps[MAP_COLOR], 4, u, value);
        glm_attrib(GLM_ATTR_COLOR, value[0], value[1], value[2], value[3]);
    }
    for (int tex = MAP_TEX4; tex >= MAP_TEX1; --tex) {
        if (!enabled[tex]) continue;
        float t[4] = {0, 0, 0, 1};
        if (dim) eval2(&maps[tex], components[tex], u, v, t, NULL, NULL);
        else eval1(&maps[tex], components[tex], u, t);
        glm_attrib(GLM_ATTR_TEX0, t[0], t[1], t[2], t[3]);
        break;
    }
    float position[4] = {0, 0, 0, 1}, du[4], dv[4];
    bool auto_normal = dim && s->auto_normal && vertex >= 0;
    if (vertex >= 0) {
        int k = components[vertex];
        if (dim) eval2(&maps[vertex], k, u, v, position, auto_normal ? du : NULL, auto_normal ? dv : NULL);
        else eval1(&maps[vertex], k, u, position);
        if (auto_normal) {
            /* A rational patch's normal is that of the projected surface.
               (Apple's lighting of such patches differs slightly from this.) */
            if (k == 4)
                for (int c = 0; c < 3; ++c) {
                    du[c] = du[c] * position[3] - du[3] * position[c];
                    dv[c] = dv[c] * position[3] - dv[3] * position[c];
                }
            float n[3] = {du[1] * dv[2] - du[2] * dv[1], du[2] * dv[0] - du[0] * dv[2], du[0] * dv[1] - du[1] * dv[0]};
            float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length > 0)
                for (int c = 0; c < 3; ++c) n[c] /= length;
            glm_attrib(GLM_ATTR_NORMAL, n[0], n[1], n[2], 1);
        }
    }
    if (!auto_normal && enabled[MAP_NORMAL]) {
        if (dim) eval2(&maps[MAP_NORMAL], 3, u, v, value, NULL, NULL);
        else eval1(&maps[MAP_NORMAL], 3, u, value);
        glm_attrib(GLM_ATTR_NORMAL, value[0], value[1], value[2], 1);
    }
    for (int i = 1; i < GLM_MAX_ATTRIBS; ++i)
        if (attribs >> i & 1) {
            eval_attrib(&ctx->attrib_maps[dim][i], dim, u, v, value);
            glm_attrib(i, value[0], value[1], value[2], value[3]);
        }
    if (attribs & 1) eval_attrib(&ctx->attrib_maps[dim][0], dim, u, v, position);
    glm_attrib(GLM_ATTR_POSITION, position[0], position[1], position[2], position[3]);
    memcpy(ctx->state.current, saved, sizeof saved);
}

GLM_EXPORT void glEvalCoord1f(GLfloat u) { GLM_CONTEXT(ctx); eval_coord(ctx, 0, u, 0); }
GLM_EXPORT void glEvalCoord1d(GLdouble u) { glEvalCoord1f((GLfloat)u); }
GLM_EXPORT void glEvalCoord1fv(const GLfloat *u) { glEvalCoord1f(u[0]); }
GLM_EXPORT void glEvalCoord1dv(const GLdouble *u) { glEvalCoord1f((GLfloat)u[0]); }
GLM_EXPORT void glEvalCoord2f(GLfloat u, GLfloat v) { GLM_CONTEXT(ctx); eval_coord(ctx, 1, u, v); }
GLM_EXPORT void glEvalCoord2d(GLdouble u, GLdouble v) { glEvalCoord2f((GLfloat)u, (GLfloat)v); }
GLM_EXPORT void glEvalCoord2fv(const GLfloat *u) { glEvalCoord2f(u[0], u[1]); }
GLM_EXPORT void glEvalCoord2dv(const GLdouble *u) { glEvalCoord2f((GLfloat)u[0], (GLfloat)u[1]); }

static float grid_u(const struct glm_state *s, int dim, GLint i)
{
    const float *d = s->eval_grid_domain[dim];
    return d[0] + (float)i * ((d[1] - d[0]) / (float)s->eval_grid_segments[dim][0]);
}
static float grid_v(const struct glm_state *s, GLint j)
{
    const float *d = s->eval_grid_domain[1];
    return d[2] + (float)j * ((d[3] - d[2]) / (float)s->eval_grid_segments[1][1]);
}

GLM_EXPORT void glEvalPoint1(GLint i) { GLM_CONTEXT(ctx); eval_coord(ctx, 0, grid_u(&ctx->state, 0, i), 0); }
GLM_EXPORT void glEvalPoint2(GLint i, GLint j)
{
    GLM_CONTEXT(ctx);
    eval_coord(ctx, 1, grid_u(&ctx->state, 1, i), grid_v(&ctx->state, j));
}

GLM_EXPORT void glEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    GLM_CONTEXT(ctx);
    if (mode != GL_POINT && mode != GL_LINE) return glm_error(ctx, GL_INVALID_ENUM);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    glBegin(mode == GL_POINT ? GL_POINTS : GL_LINE_STRIP);
    for (GLint i = i1; i <= i2; ++i) eval_coord(ctx, 0, grid_u(&ctx->state, 0, i), 0);
    glEnd();
}

GLM_EXPORT void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    GLM_CONTEXT(ctx);
    if (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL) return glm_error(ctx, GL_INVALID_ENUM);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    const struct glm_state *s = &ctx->state;
    switch (mode) {
    case GL_POINT:
        glBegin(GL_POINTS);
        for (GLint j = j1; j <= j2; ++j)
            for (GLint i = i1; i <= i2; ++i) eval_coord(ctx, 1, grid_u(s, 1, i), grid_v(s, j));
        glEnd();
        break;
    case GL_LINE:
        for (GLint j = j1; j <= j2; ++j) {
            glBegin(GL_LINE_STRIP);
            for (GLint i = i1; i <= i2; ++i) eval_coord(ctx, 1, grid_u(s, 1, i), grid_v(s, j));
            glEnd();
        }
        for (GLint i = i1; i <= i2; ++i) {
            glBegin(GL_LINE_STRIP);
            for (GLint j = j1; j <= j2; ++j) eval_coord(ctx, 1, grid_u(s, 1, i), grid_v(s, j));
            glEnd();
        }
        break;
    case GL_FILL:
        for (GLint j = j1; j < j2; ++j) {
            glBegin(GL_TRIANGLE_STRIP);
            for (GLint i = i1; i <= i2; ++i) {
                eval_coord(ctx, 1, grid_u(s, 1, i), grid_v(s, j));
                eval_coord(ctx, 1, grid_u(s, 1, i), grid_v(s, j + 1));
            }
            glEnd();
        }
        break;
    }
}

/* glGet: the grids. */
int glm_eval_query(struct glm_context *ctx, GLenum pname, double *v)
{
    const struct glm_state *s = &ctx->state;
    switch (pname) {
    case GL_MAP1_GRID_DOMAIN: v[0] = s->eval_grid_domain[0][0]; v[1] = s->eval_grid_domain[0][1]; return 2;
    case GL_MAP1_GRID_SEGMENTS: v[0] = s->eval_grid_segments[0][0]; return 1;
    case GL_MAP2_GRID_DOMAIN: for (int i = 0; i < 4; ++i) v[i] = s->eval_grid_domain[1][i]; return 4;
    case GL_MAP2_GRID_SEGMENTS: v[0] = s->eval_grid_segments[1][0]; v[1] = s->eval_grid_segments[1][1]; return 2;
    case GL_MAX_EVAL_ORDER: v[0] = GLM_MAX_EVAL_ORDER; return 1;
    }
    return 0;
}
