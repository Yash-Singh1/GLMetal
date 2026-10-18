/* The imaging subset (ARB_imaging: SGI_color_table, EXT_convolution with
 * HP_convolution_border_modes, SGI_color_matrix, EXT_histogram; GL 2.1
 * 3.6.3, 3.6.5): colour tables, convolution filters, the colour matrix,
 * histogram and minmax, run on the CPU over the float RGBA images of every
 * pixel path (glDrawPixels, glCopyPixels, glReadPixels, texture images and
 * copies). Their state, errors and queries answer as Apple's OpenGL does
 * (tests/probes/imaging.c); where Apple's pixel results contradict the
 * extensions, the extensions are followed (docs/LEGACY_GL.md). */
#include "glm_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_TABLE = 8192, MAX_HISTOGRAM = 65536, MAX_FILTER = 11 };

/* Entries are RGBA as glGetColorTable returns them: ALPHA (0, 0, 0, A),
   LUMINANCE (L, L, L, 1), LUMINANCE_ALPHA (L, L, L, A), INTENSITY
   (I, I, I, 1), RGB (R, G, B, 1). Filters keep their pixels the same way. */
struct table {
    GLenum base;
    GLsizei width;
    float *entries;
};
struct filter {
    GLenum internal, base;
    GLsizei width, height;
    float *values; /* width * height pixels; separable: the row, then the column */
};
struct glm_imaging_objects {
    struct table tables[3], proxies[3];
    struct filter filters[3]; /* 1D, 2D, separable */
    struct {
        GLenum internal;
        GLsizei width;
        bool sink;
        uint32_t *counts; /* width RGBA counters */
    } histogram, proxy_histogram;
    struct {
        GLenum internal;
        bool sink;
        float min[4], max[4];
    } minmax;
};

static void reset_minmax(struct glm_imaging_objects *o)
{
    for (int c = 0; c < 4; ++c) o->minmax.min[c] = 1, o->minmax.max[c] = 0;
}

void glm_imaging_init(struct glm_context *ctx)
{
    struct glm_imaging_state *s = &ctx->state.imaging;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 3; ++i) {
        s->border_mode[i] = GL_REDUCE;
        for (int c = 0; c < 4; ++c) s->table_scale[i][c] = s->filter_scale[i][c] = 1;
    }
    for (int c = 0; c < 4; ++c) s->post_convolution_scale[c] = s->post_color_matrix_scale[c] = 1;
    struct glm_imaging_objects *o = calloc(1, sizeof *o);
    if (!o) return;
    for (int i = 0; i < 3; ++i) {
        o->tables[i].base = o->proxies[i].base = GL_RGBA;
        o->filters[i].internal = o->filters[i].base = GL_RGBA;
    }
    o->histogram.internal = o->proxy_histogram.internal = GL_RGBA;
    o->minmax.internal = GL_RGBA;
    reset_minmax(o);
    ctx->imaging = o;
}

void glm_imaging_destroy(struct glm_context *ctx)
{
    struct glm_imaging_objects *o = ctx->imaging;
    if (!o) return;
    for (int i = 0; i < 3; ++i) {
        free(o->tables[i].entries);
        free(o->filters[i].values);
    }
    free(o->histogram.counts);
    free(o);
    ctx->imaging = NULL;
}

/* ---- formats ----------------------------------------------------------------- */

/* The base internal format of `internal` (GL_DEPTH_COMPONENT for depth
   formats, 0 for unknown ones). */
static GLenum base_format(GLenum internal)
{
    switch (internal) {
    case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16: case GL_ALPHA16F_ARB:
    case GL_ALPHA32F_ARB:
        return GL_ALPHA;
    case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12: case GL_LUMINANCE16:
    case GL_LUMINANCE16F_ARB: case GL_LUMINANCE32F_ARB: case GL_SLUMINANCE: case GL_SLUMINANCE8:
        return GL_LUMINANCE;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2: case GL_LUMINANCE8_ALPHA8:
    case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12: case GL_LUMINANCE16_ALPHA16:
    case GL_LUMINANCE_ALPHA16F_ARB: case GL_LUMINANCE_ALPHA32F_ARB: case GL_SLUMINANCE_ALPHA:
    case GL_SLUMINANCE8_ALPHA8:
        return GL_LUMINANCE_ALPHA;
    case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12: case GL_INTENSITY16:
    case GL_INTENSITY16F_ARB: case GL_INTENSITY32F_ARB:
        return GL_INTENSITY;
    case 3: case GL_RGB: case GL_R3_G3_B2: case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_RGB10: case GL_RGB12:
    case GL_RGB16: case GL_RGB16F_ARB: case GL_RGB32F_ARB: case GL_SRGB: case GL_SRGB8:
        return GL_RGB;
    case 4: case GL_RGBA: case GL_RGBA2: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA8: case GL_RGB10_A2:
    case GL_RGBA12: case GL_RGBA16: case GL_RGBA16F_ARB: case GL_RGBA32F_ARB: case GL_SRGB_ALPHA:
    case GL_SRGB8_ALPHA8:
        return GL_RGBA;
    case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT16: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32:
    case GL_DEPTH_COMPONENT32F: case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8: case GL_DEPTH32F_STENCIL8:
        return GL_DEPTH_COMPONENT;
    }
    return 0;
}

/* Which of R, G, B, A a base format holds (luminance and intensity as R). */
static void format_components(GLenum base, bool c[4])
{
    c[0] = base != GL_ALPHA;
    c[1] = c[2] = base == GL_RGB || base == GL_RGBA;
    c[3] = base == GL_ALPHA || base == GL_LUMINANCE_ALPHA || base == GL_RGBA;
}

/* An RGBA pixel as a base format keeps it (the layout of struct table). */
static void to_internal(GLenum base, float *p)
{
    switch (base) {
    case GL_ALPHA: p[0] = p[1] = p[2] = 0; break;
    case GL_LUMINANCE: case GL_INTENSITY: p[1] = p[2] = p[0]; p[3] = 1; break;
    case GL_LUMINANCE_ALPHA: p[1] = p[2] = p[0]; break;
    case GL_RGB: p[3] = 1; break;
    }
}

/* The format and type of client pixel data these commands read. */
static GLenum unpack_error(GLenum format, GLenum type)
{
    if (format == GL_COLOR_INDEX || format == GL_STENCIL_INDEX) return GL_INVALID_ENUM;
    return glm_tex_image_error(GL_RGBA8, format, type, false);
}

/* ... and of the data glGetColorTable, glGetHistogram, etc. write. */
static int pack_components(GLenum format)
{
    switch (format) {
    case GL_RED: case GL_GREEN: case GL_BLUE: case GL_ALPHA: case GL_LUMINANCE: return 1;
    case GL_LUMINANCE_ALPHA: return 2;
    case GL_RGB: case GL_BGR: return 3;
    case GL_RGBA: case GL_BGRA: case GL_ABGR_EXT: return 4;
    }
    return 0;
}
static int pack_type_bytes(GLenum type)
{
    switch (type) {
    case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
    case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: return 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return 4;
    }
    return 0;
}
static GLenum pack_error(GLenum format, GLenum type)
{
    return pack_components(format) && pack_type_bytes(type) ? GL_NO_ERROR : GL_INVALID_ENUM;
}

static uint16_t half(float f)
{
    _Float16 h = (_Float16)f;
    uint16_t bits;
    memcpy(&bits, &h, 2);
    return bits;
}

/* An RGBA image to client memory (or the pack buffer) in `format`/`type`,
   with the pack state: luminance is R, absent components 0 (`present`).
   Counts (histograms) convert to integers as they are. */
static void pack_image(struct glm_context *ctx, const float *rgba, GLsizei width, GLsizei height, const bool present[4],
                       GLenum format, GLenum type, void *out, bool counts)
{
    const struct glm_state *s = &ctx->state;
    uint8_t *base = out;
    if (ctx->pixel_pack_buffer) {
        struct glm_buffer *buffer = glm_buffer_get(ctx, ctx->pixel_pack_buffer);
        uint8_t *contents = buffer ? glm_backend_buffer_contents(ctx, buffer, true) : NULL;
        if (!contents) return glm_error(ctx, GL_INVALID_OPERATION);
        base = contents + (uintptr_t)out;
    }
    if (!base) return;
    int n = pack_components(format), size = pack_type_bytes(type);
    size_t pixel = (size_t)n * (size_t)size;
    size_t row_pixels = s->pack_row_length > 0 ? (size_t)s->pack_row_length : (size_t)width;
    size_t alignment = (size_t)(s->pack_alignment > 0 ? s->pack_alignment : 4);
    size_t row = s->pack_row_bytes > 0 ? (size_t)s->pack_row_bytes : (row_pixels * pixel + alignment - 1) / alignment * alignment;
    base += (size_t)s->pack_skip_rows * row + (size_t)s->pack_skip_pixels * pixel;
    static const int order[][4] = {{0}, {1}, {2}, {3}, {0, 3}, {0, 1, 2}, {2, 1, 0}, {0, 1, 2, 3}, {2, 1, 0, 3}, {3, 2, 1, 0}};
    int o;
    switch (format) {
    case GL_RED: case GL_LUMINANCE: o = 0; break;
    case GL_GREEN: o = 1; break;
    case GL_BLUE: o = 2; break;
    case GL_ALPHA: o = 3; break;
    case GL_LUMINANCE_ALPHA: o = 4; break;
    case GL_RGB: o = 5; break;
    case GL_BGR: o = 6; break;
    case GL_RGBA: o = 7; break;
    case GL_BGRA: o = 8; break;
    default: o = 9; break;
    }
    for (GLsizei y = 0; y < height; ++y)
        for (GLsizei x = 0; x < width; ++x) {
            const float *p = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
            uint8_t *d = base + (size_t)y * row + (size_t)x * pixel;
            for (int i = 0; i < n; ++i) {
                int c = order[o][i];
                double v = present[c] ? p[c] : 0;
                uint8_t *e = d + i * size;
                union { uint8_t u8; int8_t s8; uint16_t u16; int16_t s16; uint32_t u32; int32_t s32; float f; } u;
                switch (type) {
                case GL_UNSIGNED_BYTE: u.u8 = counts ? (uint8_t)v : (uint8_t)lround(fmin(fmax(v, 0), 1) * 255); break;
                case GL_BYTE: u.s8 = counts ? (int8_t)v : (int8_t)lround(fmin(fmax(v, -1), 1) * 127); break;
                case GL_UNSIGNED_SHORT: u.u16 = counts ? (uint16_t)v : (uint16_t)lround(fmin(fmax(v, 0), 1) * 65535); break;
                case GL_SHORT: u.s16 = counts ? (int16_t)v : (int16_t)lround(fmin(fmax(v, -1), 1) * 32767); break;
                case GL_UNSIGNED_INT: u.u32 = counts ? (uint32_t)v : (uint32_t)llround(fmin(fmax(v, 0), 1) * 4294967295.0); break;
                case GL_INT: u.s32 = counts ? (int32_t)v : (int32_t)llround(fmin(fmax(v, -1), 1) * 2147483647.0); break;
                case GL_HALF_FLOAT: u.u16 = half((float)v); break;
                default: u.f = (float)v; break;
                }
                memcpy(e, &u, (size_t)size);
                if (s->pack_swap_bytes)
                    for (int k = 0; k < size / 2; ++k) {
                        uint8_t t = e[k];
                        e[k] = e[size - 1 - k];
                        e[size - 1 - k] = t;
                    }
            }
        }
}

/* ---- targets ----------------------------------------------------------------- */

/* GL_COLOR_TABLE .. 0, 1, 2; proxies the same with `proxy` set; -1 others. */
static int table_index(GLenum target, bool *proxy)
{
    *proxy = false;
    switch (target) {
    case GL_COLOR_TABLE: return 0;
    case GL_POST_CONVOLUTION_COLOR_TABLE: return 1;
    case GL_POST_COLOR_MATRIX_COLOR_TABLE: return 2;
    case GL_PROXY_COLOR_TABLE: *proxy = true; return 0;
    case GL_PROXY_POST_CONVOLUTION_COLOR_TABLE: *proxy = true; return 1;
    case GL_PROXY_POST_COLOR_MATRIX_COLOR_TABLE: *proxy = true; return 2;
    }
    return -1;
}

static int filter_index(GLenum target)
{
    switch (target) {
    case GL_CONVOLUTION_1D: return 0;
    case GL_CONVOLUTION_2D: return 1;
    case GL_SEPARABLE_2D: return 2;
    }
    return -1;
}

bool *glm_imaging_capability(struct glm_context *ctx, GLenum cap)
{
    bool *e = ctx->state.imaging.enabled;
    switch (cap) {
    case GL_COLOR_TABLE: return &e[GLM_IMAGING_COLOR_TABLE];
    case GL_POST_CONVOLUTION_COLOR_TABLE: return &e[GLM_IMAGING_POST_CONVOLUTION_TABLE];
    case GL_POST_COLOR_MATRIX_COLOR_TABLE: return &e[GLM_IMAGING_POST_COLOR_MATRIX_TABLE];
    case GL_CONVOLUTION_1D: return &e[GLM_IMAGING_CONVOLUTION_1D];
    case GL_CONVOLUTION_2D: return &e[GLM_IMAGING_CONVOLUTION_2D];
    case GL_SEPARABLE_2D: return &e[GLM_IMAGING_SEPARABLE_2D];
    case GL_HISTOGRAM: return &e[GLM_IMAGING_HISTOGRAM];
    case GL_MINMAX: return &e[GLM_IMAGING_MINMAX];
    }
    return NULL;
}

/* glPixelTransfer's post-convolution and post-colour-matrix scales and biases. */
static float *transfer_value(struct glm_imaging_state *s, GLenum pname)
{
    if (pname >= GL_POST_CONVOLUTION_RED_SCALE && pname <= GL_POST_CONVOLUTION_ALPHA_SCALE)
        return &s->post_convolution_scale[pname - GL_POST_CONVOLUTION_RED_SCALE];
    if (pname >= GL_POST_CONVOLUTION_RED_BIAS && pname <= GL_POST_CONVOLUTION_ALPHA_BIAS)
        return &s->post_convolution_bias[pname - GL_POST_CONVOLUTION_RED_BIAS];
    if (pname >= GL_POST_COLOR_MATRIX_RED_SCALE && pname <= GL_POST_COLOR_MATRIX_ALPHA_SCALE)
        return &s->post_color_matrix_scale[pname - GL_POST_COLOR_MATRIX_RED_SCALE];
    if (pname >= GL_POST_COLOR_MATRIX_RED_BIAS && pname <= GL_POST_COLOR_MATRIX_ALPHA_BIAS)
        return &s->post_color_matrix_bias[pname - GL_POST_COLOR_MATRIX_RED_BIAS];
    return NULL;
}

bool glm_imaging_transfer(struct glm_context *ctx, GLenum pname, GLfloat value)
{
    float *v = transfer_value(&ctx->state.imaging, pname);
    if (v) *v = value;
    return v != NULL;
}

int glm_imaging_query(struct glm_context *ctx, GLenum pname, double *v)
{
    float *value = transfer_value(&ctx->state.imaging, pname);
    if (value) {
        v[0] = *value;
        return 1;
    }
    bool *enabled = glm_imaging_capability(ctx, pname);
    if (enabled) {
        v[0] = *enabled;
        return 1;
    }
    return 0;
}

/* ---- colour tables ------------------------------------------------------------- */

/* Table entries from RGBA pixels: scaled, biased and clamped (when the
   scale and bias change them: Apple keeps the values of float tables
   otherwise), then in the table's format. */
static void table_entries(const struct glm_imaging_state *s, int t, GLenum base, float *rgba, GLsizei count)
{
    bool identity = true;
    for (int c = 0; c < 4; ++c) identity &= s->table_scale[t][c] == 1 && s->table_bias[t][c] == 0;
    for (GLsizei i = 0; i < count; ++i) {
        float *p = rgba + (size_t)i * 4;
        if (!identity)
            for (int c = 0; c < 4; ++c) {
                float v = p[c] * s->table_scale[t][c] + s->table_bias[t][c];
                p[c] = v < 0 ? 0 : v > 1 ? 1 : v;
            }
        to_internal(base, p);
    }
}

static bool power_of_two(GLsizei n) { return n > 0 && !(n & (n - 1)); }

/* Defines table `t` (or its proxy) from RGBA pixels (NULL: undefined contents). */
static void define_table(struct glm_context *ctx, int t, bool proxy, GLenum internal, GLsizei width, float *rgba)
{
    struct glm_imaging_objects *o = ctx->imaging;
    GLenum base = base_format(internal);
    if (proxy) {
        /* Apple keeps a proxy's format at GL_RGBA. */
        o->proxies[t].width = width <= MAX_TABLE ? width : 0;
        return;
    }
    if (width > MAX_TABLE) return glm_error(ctx, GL_TABLE_TOO_LARGE);
    struct table *table = &o->tables[t];
    float *entries = malloc((size_t)(width ? width : 1) * 16);
    if (!entries) return glm_error(ctx, GL_OUT_OF_MEMORY);
    if (rgba) {
        memcpy(entries, rgba, (size_t)width * 16);
        table_entries(&ctx->state.imaging, t, base, entries, width);
    } else {
        memset(entries, 0, (size_t)(width ? width : 1) * 16);
    }
    free(table->entries);
    table->entries = entries;
    table->width = width;
    table->base = base;
}

static void color_table(GLenum target, GLenum internal, GLsizei width, GLenum format, GLenum type, const GLvoid *data)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    bool proxy;
    int t = table_index(target, &proxy);
    if (t < 0) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum base = base_format(internal);
    if (!base) return glm_error(ctx, GL_INVALID_ENUM);
    if (base == GL_DEPTH_COMPONENT) return glm_error(ctx, GL_INVALID_OPERATION);
    if (width < 0 || (width && !power_of_two(width))) return glm_error(ctx, GL_INVALID_VALUE);
    GLenum error = unpack_error(format, type);
    if (error) return glm_error(ctx, error);
    if (proxy || width > MAX_TABLE || (!data && !ctx->pixel_unpack_buffer))
        return define_table(ctx, t, proxy, internal, width, NULL);
    float *rgba = width ? glm_unpack_rgba(ctx, width, 1, 1, format, type, data, false) : NULL;
    if (width && !rgba) return glm_error(ctx, GL_INVALID_OPERATION);
    define_table(ctx, t, false, internal, width, rgba);
    free(rgba);
}
GLM_EXPORT void glColorTable(GLenum target, GLenum internal, GLsizei width, GLenum format, GLenum type, const GLvoid *table)
{
    color_table(target, internal, width, format, type, table);
}
GLM_EXPORT void glColorTableEXT(GLenum target, GLenum internal, GLsizei width, GLenum format, GLenum type,
                                const GLvoid *table)
{
    color_table(target, internal, width, format, type, table);
}

/* Replaces entries start .. start + count - 1 with RGBA pixels. */
static void sub_table(struct glm_context *ctx, int t, GLsizei start, GLsizei count, float *rgba)
{
    struct table *table = &ctx->imaging->tables[t];
    table_entries(&ctx->state.imaging, t, table->base, rgba, count);
    memcpy(table->entries + (size_t)start * 4, rgba, (size_t)count * 16);
}

static GLenum sub_table_error(struct glm_context *ctx, GLenum target, GLsizei start, GLsizei count, int *t)
{
    bool proxy;
    *t = table_index(target, &proxy);
    if (*t < 0 || proxy) return GL_INVALID_ENUM;
    const struct table *table = &ctx->imaging->tables[*t];
    if (!table->width) return GL_INVALID_OPERATION;
    if (start < 0 || count < 0 || start + count > table->width) return GL_INVALID_VALUE;
    return GL_NO_ERROR;
}

static void color_sub_table(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    int t;
    GLenum error = sub_table_error(ctx, target, start, count, &t);
    if (!error) error = unpack_error(format, type);
    if (error) return glm_error(ctx, error);
    if (!count || (!data && !ctx->pixel_unpack_buffer)) return;
    float *rgba = glm_unpack_rgba(ctx, count, 1, 1, format, type, data, false);
    if (!rgba) return glm_error(ctx, GL_INVALID_OPERATION);
    sub_table(ctx, t, start, count, rgba);
    free(rgba);
}
GLM_EXPORT void glColorSubTable(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type,
                                const GLvoid *data)
{
    color_sub_table(target, start, count, format, type, data);
}
GLM_EXPORT void glColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type,
                                   const GLvoid *data)
{
    color_sub_table(target, start, count, format, type, data);
}

/* The read buffer's pixels through glPixelTransfer (as Apple copies them
   into tables and filters). */
static float *read_transferred(struct glm_context *ctx, GLint x, GLint y, GLsizei width, GLsizei height)
{
    float *rgba = glm_read_rgba(ctx, x, y, width, height);
    if (rgba && !glm_transfer_identity(ctx)) glm_transfer_rgba(ctx, rgba, (size_t)width * (size_t)height);
    return rgba;
}

GLM_EXPORT void glCopyColorTable(GLenum target, GLenum internal, GLint x, GLint y, GLsizei width)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    bool proxy;
    int t = table_index(target, &proxy);
    if (t < 0 || proxy) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum base = base_format(internal);
    if (!base || base == GL_DEPTH_COMPONENT) return glm_error(ctx, GL_INVALID_ENUM);
    if (width < 0 || (width && !power_of_two(width))) return glm_error(ctx, GL_INVALID_VALUE);
    if (width > MAX_TABLE) return glm_error(ctx, GL_TABLE_TOO_LARGE);
    float *rgba = width ? read_transferred(ctx, x, y, width, 1) : NULL;
    if (width && !rgba) return glm_error(ctx, GL_OUT_OF_MEMORY);
    define_table(ctx, t, false, internal, width, rgba);
    free(rgba);
}

GLM_EXPORT void glCopyColorSubTable(GLenum target, GLsizei start, GLint x, GLint y, GLsizei count)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    int t;
    GLenum error = sub_table_error(ctx, target, start, count, &t);
    if (error) return glm_error(ctx, error);
    if (!count) return;
    float *rgba = read_transferred(ctx, x, y, count, 1);
    if (!rgba) return glm_error(ctx, GL_OUT_OF_MEMORY);
    sub_table(ctx, t, start, count, rgba);
    free(rgba);
}

/* Scale and bias of the tables; Apple accepts (and ignores) proxy targets. */
static void table_parameter(GLenum target, GLenum pname, const GLfloat *v)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    bool proxy;
    int t = table_index(target, &proxy);
    if (t < 0) return glm_error(ctx, GL_INVALID_ENUM);
    float *dst;
    if (pname == GL_COLOR_TABLE_SCALE) dst = ctx->state.imaging.table_scale[t];
    else if (pname == GL_COLOR_TABLE_BIAS) dst = ctx->state.imaging.table_bias[t];
    else return glm_error(ctx, GL_INVALID_ENUM);
    if (!proxy) memcpy(dst, v, 16);
}
GLM_EXPORT void glColorTableParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    table_parameter(target, pname, params);
}
GLM_EXPORT void glColorTableParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    const GLfloat v[4] = {(float)params[0], (float)params[1], (float)params[2], (float)params[3]};
    table_parameter(target, pname, v);
}

static void get_color_table(GLenum target, GLenum format, GLenum type, GLvoid *data)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    bool proxy;
    int t = table_index(target, &proxy);
    if (t < 0 || proxy) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = pack_error(format, type);
    if (error) return glm_error(ctx, error);
    const struct table *table = &ctx->imaging->tables[t];
    if (!table->width) return;
    const bool all[4] = {true, true, true, true};
    pack_image(ctx, table->entries, table->width, 1, all, format, type, data, false);
}
GLM_EXPORT void glGetColorTable(GLenum target, GLenum format, GLenum type, GLvoid *table)
{
    get_color_table(target, format, type, table);
}
GLM_EXPORT void glGetColorTableEXT(GLenum target, GLenum format, GLenum type, GLvoid *table)
{
    get_color_table(target, format, type, table);
}

/* Values of a table query: 1 or 4 (0 with an error raised). */
static int table_parameter_values(struct glm_context *ctx, GLenum target, GLenum pname, float *v)
{
    bool proxy;
    int t = table_index(target, &proxy);
    if (t < 0) return glm_error(ctx, GL_INVALID_ENUM), 0;
    const struct table *table = proxy ? &ctx->imaging->proxies[t] : &ctx->imaging->tables[t];
    switch (pname) {
    case GL_COLOR_TABLE_SCALE: case GL_COLOR_TABLE_BIAS:
        if (proxy) return 0; /* accepted, nothing written */
        memcpy(v, pname == GL_COLOR_TABLE_SCALE ? ctx->state.imaging.table_scale[t] : ctx->state.imaging.table_bias[t], 16);
        return 4;
    case GL_COLOR_TABLE_FORMAT: v[0] = (float)table->base; return 1;
    case GL_COLOR_TABLE_WIDTH: v[0] = (float)table->width; return 1;
    /* Apple reports 8 bits of every component once a table has a size. */
    case GL_COLOR_TABLE_RED_SIZE: case GL_COLOR_TABLE_GREEN_SIZE: case GL_COLOR_TABLE_BLUE_SIZE:
    case GL_COLOR_TABLE_ALPHA_SIZE: case GL_COLOR_TABLE_LUMINANCE_SIZE: case GL_COLOR_TABLE_INTENSITY_SIZE:
        v[0] = table->width ? 8 : 0;
        return 1;
    }
    return glm_error(ctx, GL_INVALID_ENUM), 0;
}
static void get_table_parameter(GLenum target, GLenum pname, GLfloat *fv, GLint *iv)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    float v[4];
    int n = table_parameter_values(ctx, target, pname, v);
    for (int i = 0; i < n; ++i) {
        if (fv) fv[i] = v[i];
        else iv[i] = (GLint)lroundf(v[i]);
    }
}
GLM_EXPORT void glGetColorTableParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    get_table_parameter(target, pname, params, NULL);
}
GLM_EXPORT void glGetColorTableParameteriv(GLenum target, GLenum pname, GLint *params)
{
    get_table_parameter(target, pname, NULL, params);
}
GLM_EXPORT void glGetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params)
{
    get_table_parameter(target, pname, params, NULL);
}
GLM_EXPORT void glGetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params)
{
    get_table_parameter(target, pname, NULL, params);
}

/* ---- convolution filters ------------------------------------------------------- */

/* Filter pixels from RGBA: scaled and biased (never clamped), then in the
   filter's format. */
static void filter_values(const struct glm_imaging_state *s, int f, GLenum base, float *rgba, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        float *p = rgba + i * 4;
        for (int c = 0; c < 4; ++c) p[c] = p[c] * s->filter_scale[f][c] + s->filter_bias[f][c];
        to_internal(base, p);
    }
}

static GLenum filter_format_error(GLenum internal)
{
    GLenum base = base_format(internal);
    return base && base != GL_DEPTH_COMPONENT && internal > 4 ? GL_NO_ERROR : GL_INVALID_ENUM;
}

static void define_filter(struct glm_context *ctx, int f, GLenum internal, GLsizei width, GLsizei height, float *rgba)
{
    struct filter *filter = &ctx->imaging->filters[f];
    size_t count = f == 2 ? (size_t)width + (size_t)height : (size_t)width * (size_t)height;
    GLenum base = base_format(internal);
    if (rgba) filter_values(&ctx->state.imaging, f, base, rgba, count);
    free(filter->values);
    filter->values = rgba;
    filter->internal = internal;
    filter->base = base;
    filter->width = width;
    filter->height = height;
}

static void convolution_filter(int dims, GLenum target, GLenum internal, GLsizei width, GLsizei height, GLenum format,
                               GLenum type, const GLvoid *image)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (filter_index(target) != dims - 1) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = filter_format_error(internal);
    if (error) return glm_error(ctx, error);
    if (width < 0 || width > MAX_FILTER || height < 0 || height > MAX_FILTER) return glm_error(ctx, GL_INVALID_VALUE);
    error = unpack_error(format, type);
    if (error) return glm_error(ctx, error);
    float *rgba = NULL;
    if (width && height && (image || ctx->pixel_unpack_buffer)) {
        rgba = glm_unpack_rgba(ctx, width, height, 1, format, type, image, false);
        if (!rgba) return glm_error(ctx, GL_INVALID_OPERATION);
    } else {
        rgba = calloc((size_t)(width * height) + 1, 16);
    }
    define_filter(ctx, dims - 1, internal, width, height, rgba);
}
GLM_EXPORT void glConvolutionFilter1D(GLenum target, GLenum internal, GLsizei width, GLenum format, GLenum type,
                                      const GLvoid *image)
{
    convolution_filter(1, target, internal, width, 1, format, type, image);
}
GLM_EXPORT void glConvolutionFilter2D(GLenum target, GLenum internal, GLsizei width, GLsizei height, GLenum format,
                                      GLenum type, const GLvoid *image)
{
    convolution_filter(2, target, internal, width, height, format, type, image);
}

GLM_EXPORT void glSeparableFilter2D(GLenum target, GLenum internal, GLsizei width, GLsizei height, GLenum format,
                                    GLenum type, const GLvoid *row, const GLvoid *column)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_SEPARABLE_2D) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = filter_format_error(internal);
    if (error) return glm_error(ctx, error);
    if (width < 0 || width > MAX_FILTER || height < 0 || height > MAX_FILTER) return glm_error(ctx, GL_INVALID_VALUE);
    error = unpack_error(format, type);
    if (error) return glm_error(ctx, error);
    float *rgba = calloc((size_t)(width + height) + 1, 16);
    if (!rgba) return glm_error(ctx, GL_OUT_OF_MEMORY);
    const GLvoid *images[2] = {row, column};
    const GLsizei sizes[2] = {width, height};
    for (int i = 0; i < 2; ++i) {
        if (!sizes[i] || (!images[i] && !ctx->pixel_unpack_buffer)) continue;
        float *part = glm_unpack_rgba(ctx, sizes[i], 1, 1, format, type, images[i], false);
        if (!part) {
            free(rgba);
            return glm_error(ctx, GL_INVALID_OPERATION);
        }
        memcpy(rgba + (i ? (size_t)width * 4 : 0), part, (size_t)sizes[i] * 16);
        free(part);
    }
    define_filter(ctx, 2, internal, width, height, rgba);
}

static void copy_filter(int dims, GLenum target, GLenum internal, GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (filter_index(target) != dims - 1) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = filter_format_error(internal);
    if (error) return glm_error(ctx, error);
    if (width < 0 || width > MAX_FILTER || height < 0 || height > MAX_FILTER) return glm_error(ctx, GL_INVALID_VALUE);
    float *rgba = width && height ? read_transferred(ctx, x, y, width, height) : calloc(1, 16);
    if (!rgba) return glm_error(ctx, GL_OUT_OF_MEMORY);
    define_filter(ctx, dims - 1, internal, width, height, rgba);
}
GLM_EXPORT void glCopyConvolutionFilter1D(GLenum target, GLenum internal, GLint x, GLint y, GLsizei width)
{
    copy_filter(1, target, internal, x, y, width, 1);
}
GLM_EXPORT void glCopyConvolutionFilter2D(GLenum target, GLenum internal, GLint x, GLint y, GLsizei width,
                                          GLsizei height)
{
    copy_filter(2, target, internal, x, y, width, height);
}

static void convolution_parameter(GLenum target, GLenum pname, const GLfloat *v, bool integer)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    int f = filter_index(target);
    if (f < 0) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_imaging_state *s = &ctx->state.imaging;
    switch (pname) {
    case GL_CONVOLUTION_BORDER_MODE: {
        GLenum mode = (GLenum)v[0];
        /* Apple accepts other values without changing the mode. */
        if (mode == GL_REDUCE || mode == GL_CONSTANT_BORDER || mode == GL_REPLICATE_BORDER) s->border_mode[f] = mode;
        return;
    }
    case GL_CONVOLUTION_BORDER_COLOR:
        /* Clamped to [0, 1]; integers map the most positive value to 1. */
        for (int c = 0; c < 4; ++c) {
            float value = integer ? (float)(v[c] / 2147483647.0) : v[c];
            s->border_color[f][c] = value < 0 ? 0 : value > 1 ? 1 : value;
        }
        return;
    case GL_CONVOLUTION_FILTER_SCALE: memcpy(s->filter_scale[f], v, 16); return;
    case GL_CONVOLUTION_FILTER_BIAS: memcpy(s->filter_bias[f], v, 16); return;
    }
    glm_error(ctx, GL_INVALID_ENUM);
}
GLM_EXPORT void glConvolutionParameterf(GLenum target, GLenum pname, GLfloat param)
{
    if (pname != GL_CONVOLUTION_BORDER_MODE) {
        GLM_CONTEXT(ctx);
        return glm_error(ctx, GL_INVALID_ENUM);
    }
    const GLfloat v[4] = {param, 0, 0, 0};
    convolution_parameter(target, pname, v, false);
}
GLM_EXPORT void glConvolutionParameteri(GLenum target, GLenum pname, GLint param)
{
    glConvolutionParameterf(target, pname, (GLfloat)param);
}
GLM_EXPORT void glConvolutionParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    GLfloat v[4] = {params[0], 0, 0, 0};
    if (pname != GL_CONVOLUTION_BORDER_MODE) memcpy(v, params, sizeof v);
    convolution_parameter(target, pname, v, false);
}
GLM_EXPORT void glConvolutionParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    int n = pname == GL_CONVOLUTION_BORDER_MODE ? 1 : 4;
    GLfloat v[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; ++i) v[i] = (GLfloat)params[i];
    convolution_parameter(target, pname, v, pname == GL_CONVOLUTION_BORDER_COLOR);
}

static void get_convolution_parameter(GLenum target, GLenum pname, GLfloat *fv, GLint *iv)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    int f = filter_index(target);
    if (f < 0) return glm_error(ctx, GL_INVALID_ENUM);
    const struct glm_imaging_state *s = &ctx->state.imaging;
    const struct filter *filter = &ctx->imaging->filters[f];
    float v[4];
    int n = 1;
    switch (pname) {
    case GL_CONVOLUTION_BORDER_MODE: v[0] = (float)s->border_mode[f]; break;
    case GL_CONVOLUTION_BORDER_COLOR:
        memcpy(v, s->border_color[f], 16);
        n = 4;
        if (iv) {
            for (int c = 0; c < 4; ++c) iv[c] = (GLint)llround(v[c] * 2147483647.0);
            return;
        }
        break;
    case GL_CONVOLUTION_FILTER_SCALE: memcpy(v, s->filter_scale[f], 16); n = 4; break;
    case GL_CONVOLUTION_FILTER_BIAS: memcpy(v, s->filter_bias[f], 16); n = 4; break;
    case GL_CONVOLUTION_FORMAT: v[0] = (float)filter->internal; break;
    case GL_CONVOLUTION_WIDTH: v[0] = (float)filter->width; break;
    case GL_MAX_CONVOLUTION_WIDTH: v[0] = MAX_FILTER; break;
    case GL_CONVOLUTION_HEIGHT:
        if (f == 0) return glm_error(ctx, GL_INVALID_ENUM);
        v[0] = (float)filter->height;
        break;
    case GL_MAX_CONVOLUTION_HEIGHT:
        if (f == 0) return glm_error(ctx, GL_INVALID_ENUM);
        v[0] = MAX_FILTER;
        break;
    default: return glm_error(ctx, GL_INVALID_ENUM);
    }
    for (int i = 0; i < n; ++i) {
        if (fv) fv[i] = v[i];
        else iv[i] = (GLint)lroundf(v[i]);
    }
}
GLM_EXPORT void glGetConvolutionParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    get_convolution_parameter(target, pname, params, NULL);
}
GLM_EXPORT void glGetConvolutionParameteriv(GLenum target, GLenum pname, GLint *params)
{
    get_convolution_parameter(target, pname, NULL, params);
}

GLM_EXPORT void glGetConvolutionFilter(GLenum target, GLenum format, GLenum type, GLvoid *image)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    int f = filter_index(target);
    if (f < 0 || f == 2) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = pack_error(format, type);
    if (error) return glm_error(ctx, error);
    const struct filter *filter = &ctx->imaging->filters[f];
    const bool all[4] = {true, true, true, true};
    if (filter->width && filter->height)
        pack_image(ctx, filter->values, filter->width, filter->height, all, format, type, image, false);
}

GLM_EXPORT void glGetSeparableFilter(GLenum target, GLenum format, GLenum type, GLvoid *row, GLvoid *column,
                                     GLvoid *span)
{
    (void)span;
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_SEPARABLE_2D) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = pack_error(format, type);
    if (error) return glm_error(ctx, error);
    const struct filter *filter = &ctx->imaging->filters[2];
    const bool all[4] = {true, true, true, true};
    if (filter->width) pack_image(ctx, filter->values, filter->width, 1, all, format, type, row, false);
    if (filter->height)
        pack_image(ctx, filter->values + (size_t)filter->width * 4, filter->height, 1, all, format, type, column, false);
}

/* ---- histogram and minmax --------------------------------------------------------- */

static GLenum statistics_format_error(GLenum internal)
{
    GLenum base = base_format(internal);
    return base && base != GL_DEPTH_COMPONENT && base != GL_INTENSITY && internal > 4 ? GL_NO_ERROR : GL_INVALID_ENUM;
}

GLM_EXPORT void glHistogram(GLenum target, GLsizei width, GLenum internal, GLboolean sink)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_HISTOGRAM && target != GL_PROXY_HISTOGRAM) return glm_error(ctx, GL_INVALID_ENUM);
    if (width < 0 || (width && !power_of_two(width))) return glm_error(ctx, GL_INVALID_VALUE);
    GLenum error = statistics_format_error(internal);
    if (error) return glm_error(ctx, error);
    struct glm_imaging_objects *o = ctx->imaging;
    bool fits = width <= MAX_HISTOGRAM;
    if (target == GL_PROXY_HISTOGRAM) {
        o->proxy_histogram.width = fits ? width : 0;
        o->proxy_histogram.internal = fits ? internal : 0;
        return;
    }
    if (!fits) return glm_error(ctx, GL_TABLE_TOO_LARGE);
    uint32_t *counts = calloc((size_t)width * 4 + 1, sizeof *counts);
    if (!counts) return glm_error(ctx, GL_OUT_OF_MEMORY);
    free(o->histogram.counts);
    o->histogram.counts = counts;
    o->histogram.width = width;
    o->histogram.internal = internal;
    o->histogram.sink = sink != 0;
}

GLM_EXPORT void glResetHistogram(GLenum target)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_HISTOGRAM) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_imaging_objects *o = ctx->imaging;
    if (o->histogram.counts) memset(o->histogram.counts, 0, (size_t)o->histogram.width * 4 * sizeof(uint32_t));
}

/* Which of R, G, B, A `format` returns (luminance as R). */
static void returned_components(GLenum format, bool c[4])
{
    c[0] = format == GL_RED || format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA || pack_components(format) >= 3;
    c[1] = format == GL_GREEN || pack_components(format) >= 3;
    c[2] = format == GL_BLUE || pack_components(format) >= 3;
    c[3] = format == GL_ALPHA || format == GL_LUMINANCE_ALPHA || pack_components(format) == 4;
}

GLM_EXPORT void glGetHistogram(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_HISTOGRAM) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = pack_error(format, type);
    if (error) return glm_error(ctx, error);
    struct glm_imaging_objects *o = ctx->imaging;
    GLsizei width = o->histogram.width;
    if (!width) return;
    bool present[4], returned[4];
    format_components(base_format(o->histogram.internal), present);
    float *rgba = malloc((size_t)width * 16);
    if (!rgba) return glm_error(ctx, GL_OUT_OF_MEMORY);
    for (size_t i = 0; i < (size_t)width * 4; ++i) rgba[i] = (float)o->histogram.counts[i];
    pack_image(ctx, rgba, width, 1, present, format, type, values, true);
    free(rgba);
    /* Only the counters returned are reset. */
    returned_components(format, returned);
    if (reset)
        for (GLsizei i = 0; i < width; ++i)
            for (int c = 0; c < 4; ++c)
                if (present[c] && returned[c]) o->histogram.counts[(size_t)i * 4 + (size_t)c] = 0;
}

static void get_histogram_parameter(GLenum target, GLenum pname, GLfloat *fv, GLint *iv)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_HISTOGRAM && target != GL_PROXY_HISTOGRAM) return glm_error(ctx, GL_INVALID_ENUM);
    struct glm_imaging_objects *o = ctx->imaging;
    bool proxy = target == GL_PROXY_HISTOGRAM;
    GLsizei width = proxy ? o->proxy_histogram.width : o->histogram.width;
    float v;
    switch (pname) {
    case GL_HISTOGRAM_WIDTH: v = (float)width; break;
    case GL_HISTOGRAM_FORMAT: v = (float)(proxy ? o->proxy_histogram.internal : o->histogram.internal); break;
    /* Apple reports 32-bit counters of every component once it has a size. */
    case GL_HISTOGRAM_RED_SIZE: case GL_HISTOGRAM_GREEN_SIZE: case GL_HISTOGRAM_BLUE_SIZE:
    case GL_HISTOGRAM_ALPHA_SIZE: case GL_HISTOGRAM_LUMINANCE_SIZE:
        v = width ? 32 : 0;
        break;
    case GL_HISTOGRAM_SINK:
        if (proxy) return glm_error(ctx, GL_INVALID_ENUM);
        v = o->histogram.sink;
        break;
    default: return glm_error(ctx, GL_INVALID_ENUM);
    }
    if (fv) fv[0] = v;
    else iv[0] = (GLint)v;
}
GLM_EXPORT void glGetHistogramParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    get_histogram_parameter(target, pname, params, NULL);
}
GLM_EXPORT void glGetHistogramParameteriv(GLenum target, GLenum pname, GLint *params)
{
    get_histogram_parameter(target, pname, NULL, params);
}

GLM_EXPORT void glMinmax(GLenum target, GLenum internal, GLboolean sink)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_MINMAX) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = statistics_format_error(internal);
    if (error) return glm_error(ctx, error);
    struct glm_imaging_objects *o = ctx->imaging;
    o->minmax.internal = internal;
    o->minmax.sink = sink != 0;
    reset_minmax(o);
}

GLM_EXPORT void glResetMinmax(GLenum target)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_MINMAX) return glm_error(ctx, GL_INVALID_ENUM);
    reset_minmax(ctx->imaging);
}

GLM_EXPORT void glGetMinmax(GLenum target, GLboolean reset, GLenum format, GLenum type, GLvoid *values)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_MINMAX) return glm_error(ctx, GL_INVALID_ENUM);
    GLenum error = pack_error(format, type);
    if (error) return glm_error(ctx, error);
    struct glm_imaging_objects *o = ctx->imaging;
    bool present[4];
    format_components(base_format(o->minmax.internal), present);
    float rgba[8];
    memcpy(rgba, o->minmax.min, 16);
    memcpy(rgba + 4, o->minmax.max, 16);
    pack_image(ctx, rgba, 2, 1, present, format, type, values, false);
    if (reset) reset_minmax(o);
}

static void get_minmax_parameter(GLenum target, GLenum pname, GLfloat *fv, GLint *iv)
{
    GLM_CONTEXT(ctx);
    if (ctx->immediate) return glm_error(ctx, GL_INVALID_OPERATION);
    if (target != GL_MINMAX) return glm_error(ctx, GL_INVALID_ENUM);
    float v;
    switch (pname) {
    case GL_MINMAX_FORMAT: v = (float)ctx->imaging->minmax.internal; break;
    case GL_MINMAX_SINK: v = ctx->imaging->minmax.sink; break;
    default: return glm_error(ctx, GL_INVALID_ENUM);
    }
    if (fv) fv[0] = v;
    else iv[0] = (GLint)v;
}
GLM_EXPORT void glGetMinmaxParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    get_minmax_parameter(target, pname, params, NULL);
}
GLM_EXPORT void glGetMinmaxParameteriv(GLenum target, GLenum pname, GLint *params)
{
    get_minmax_parameter(target, pname, NULL, params);
}

/* ---- the operations --------------------------------------------------------------- */

static const float *color_matrix(const struct glm_context *ctx)
{
    const struct glm_matrix_stack *m = &ctx->state.color;
    return m->m[m->depth];
}

static bool matrix_identity(const float *m)
{
    for (int i = 0; i < 16; ++i)
        if (m[i] != (i % 5 == 0 ? 1.0f : 0.0f)) return false;
    return true;
}

static bool post_matrix_identity(const struct glm_imaging_state *s)
{
    for (int c = 0; c < 4; ++c)
        if (s->post_color_matrix_scale[c] != 1 || s->post_color_matrix_bias[c] != 0) return false;
    return true;
}

/* The convolution filter `dims`-dimensional images go through: 0 (1D), 1
   (2D), 2 (separable) or -1 (none). */
static int active_filter(const struct glm_context *ctx, int dims)
{
    const bool *e = ctx->state.imaging.enabled;
    if (dims == 1) return e[GLM_IMAGING_CONVOLUTION_1D] ? 0 : -1;
    if (dims == 2) return e[GLM_IMAGING_CONVOLUTION_2D] ? 1 : e[GLM_IMAGING_SEPARABLE_2D] ? 2 : -1;
    return -1;
}

bool glm_imaging_active(const struct glm_context *ctx, int dims)
{
    if (ctx->pixels_raw || !ctx->imaging) return false;
    const struct glm_imaging_state *s = &ctx->state.imaging;
    const bool *e = s->enabled;
    if (e[GLM_IMAGING_COLOR_TABLE] || e[GLM_IMAGING_POST_CONVOLUTION_TABLE] || e[GLM_IMAGING_POST_COLOR_MATRIX_TABLE] ||
        e[GLM_IMAGING_HISTOGRAM] || e[GLM_IMAGING_MINMAX] || active_filter(ctx, dims) >= 0)
        return true;
    return !matrix_identity(color_matrix(ctx)) || !post_matrix_identity(s);
}

void glm_imaging_size(const struct glm_context *ctx, int dims, GLsizei *width, GLsizei *height)
{
    if (ctx->pixels_raw || !ctx->imaging) return;
    int f = active_filter(ctx, dims);
    if (f < 0 || ctx->state.imaging.border_mode[f] != GL_REDUCE) return;
    const struct filter *filter = &ctx->imaging->filters[f];
    if (!filter->width) return;
    *width -= filter->width - 1;
    if (dims == 2) *height -= filter->height - 1;
    if (*width < 0) *width = 0;
    if (*height < 0) *height = 0;
}

static int lookup_index(float v, GLsizei width)
{
    float c = v < 0 ? 0 : v > 1 ? 1 : v;
    return (int)lrintf(c * (float)(width - 1));
}

static void table_lookup(const struct table *t, float *rgba, size_t count)
{
    if (!t->width) return;
    for (size_t i = 0; i < count; ++i) {
        float *p = rgba + i * 4;
        const float *e = t->entries;
        int w = t->width;
        switch (t->base) {
        case GL_ALPHA: p[3] = e[lookup_index(p[3], w) * 4 + 3]; break;
        case GL_LUMINANCE: case GL_LUMINANCE_ALPHA: case GL_INTENSITY:
            for (int c = 0; c < 3; ++c) p[c] = e[lookup_index(p[c], w) * 4];
            if (t->base == GL_LUMINANCE_ALPHA) p[3] = e[lookup_index(p[3], w) * 4 + 3];
            if (t->base == GL_INTENSITY) p[3] = e[lookup_index(p[3], w) * 4];
            break;
        case GL_RGB: for (int c = 0; c < 3; ++c) p[c] = e[lookup_index(p[c], w) * 4 + c]; break;
        default: for (int c = 0; c < 4; ++c) p[c] = e[lookup_index(p[c], w) * 4 + c]; break;
        }
    }
}

/* Which components a filter of `base` convolves (the others pass through)
   and which of its components weighs each. */
static void filter_weights(GLenum base, bool convolved[4], int weight[4])
{
    for (int c = 0; c < 4; ++c) convolved[c] = true, weight[c] = c;
    switch (base) {
    case GL_ALPHA: convolved[0] = convolved[1] = convolved[2] = false; break;
    case GL_LUMINANCE: convolved[3] = false; weight[1] = weight[2] = 0; break;
    case GL_LUMINANCE_ALPHA: weight[1] = weight[2] = 0; break;
    case GL_INTENSITY: weight[1] = weight[2] = weight[3] = 0; break;
    case GL_RGB: convolved[3] = false; break;
    }
}

/* The source pixel (x, y) for a convolution, outside the image per the
   border mode (REDUCE never reads outside). */
static const float *source_pixel(const float *rgba, GLsizei width, GLsizei height, int x, int y, GLenum mode,
                                 const float *border)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        if (mode == GL_CONSTANT_BORDER) return border;
        x = x < 0 ? 0 : x >= width ? width - 1 : x;
        y = y < 0 ? 0 : y >= height ? height - 1 : y;
    }
    return rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
}

/* Convolves with filter `f` (EXT_convolution, HP_convolution_border_modes);
   the result replaces the image. */
static bool convolve(struct glm_context *ctx, int f, float **rgba, GLsizei *width, GLsizei *height)
{
    const struct filter *filter = &ctx->imaging->filters[f];
    const struct glm_imaging_state *s = &ctx->state.imaging;
    GLenum mode = s->border_mode[f];
    GLsizei fw = filter->width, fh = f == 0 ? 1 : filter->height;
    GLsizei w = *width, h = *height;
    GLsizei ow = w, oh = h;
    int cx = fw / 2, cy = fh / 2;
    if (mode == GL_REDUCE) {
        ow = w - (fw ? fw - 1 : 0);
        oh = h - (fh ? fh - 1 : 0);
        cx = cy = 0;
        if (ow <= 0 || oh <= 0) {
            free(*rgba);
            *rgba = NULL;
            *width = *height = 0;
            return true;
        }
    }
    float *out = calloc((size_t)ow * (size_t)oh + 1, 16);
    if (!out) return false;
    bool convolved[4];
    int weight[4];
    filter_weights(filter->base, convolved, weight);
    const float *values = filter->values;
    for (GLsizei y = 0; y < oh; ++y)
        for (GLsizei x = 0; x < ow; ++x) {
            float sum[4] = {0, 0, 0, 0};
            if (fw && fh)
                for (GLsizei m = 0; m < fh; ++m)
                    for (GLsizei n = 0; n < fw; ++n) {
                        const float *p = source_pixel(*rgba, w, h, (int)(x + n - cx), (int)(y + m - cy), mode,
                                                      s->border_color[f]);
                        for (int c = 0; c < 4; ++c) {
                            float k = f == 2 ? values[(size_t)n * 4 + (size_t)weight[c]] *
                                                   values[((size_t)fw + (size_t)m) * 4 + (size_t)weight[c]]
                                             : values[((size_t)m * (size_t)fw + (size_t)n) * 4 + (size_t)weight[c]];
                            sum[c] += p[c] * k;
                        }
                    }
            /* Components the filter leaves: the source pixel under the filter's centre. */
            int sx = mode == GL_REDUCE ? (int)x + fw / 2 : (int)x, sy = mode == GL_REDUCE ? (int)y + fh / 2 : (int)y;
            const float *centre = *rgba + ((size_t)sy * (size_t)w + (size_t)sx) * 4;
            float *o = out + ((size_t)y * (size_t)ow + (size_t)x) * 4;
            for (int c = 0; c < 4; ++c) {
                float v = convolved[c] ? sum[c] : centre[c];
                o[c] = v * s->post_convolution_scale[c] + s->post_convolution_bias[c];
            }
        }
    free(*rgba);
    *rgba = out;
    *width = ow;
    *height = oh;
    return true;
}

bool glm_imaging_apply(struct glm_context *ctx, int dims, float **rgba, GLsizei *width, GLsizei *height)
{
    if (!glm_imaging_active(ctx, dims)) return true;
    struct glm_imaging_objects *o = ctx->imaging;
    const struct glm_imaging_state *s = &ctx->state.imaging;
    const bool *e = s->enabled;
    size_t count = (size_t)*width * (size_t)*height;
    if (e[GLM_IMAGING_COLOR_TABLE]) table_lookup(&o->tables[0], *rgba, count);
    int f = active_filter(ctx, dims);
    if (f >= 0) {
        if (!convolve(ctx, f, rgba, width, height)) return glm_error(ctx, GL_OUT_OF_MEMORY), false;
        if (!*rgba) return false;
        count = (size_t)*width * (size_t)*height;
    }
    if (e[GLM_IMAGING_POST_CONVOLUTION_TABLE]) table_lookup(&o->tables[1], *rgba, count);
    const float *m = color_matrix(ctx);
    if (!matrix_identity(m) || !post_matrix_identity(s))
        for (size_t i = 0; i < count; ++i) {
            float *p = *rgba + i * 4, in[4] = {p[0], p[1], p[2], p[3]};
            for (int r = 0; r < 4; ++r) {
                float v = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r] * in[3];
                p[r] = v * s->post_color_matrix_scale[r] + s->post_color_matrix_bias[r];
            }
        }
    if (e[GLM_IMAGING_POST_COLOR_MATRIX_TABLE]) table_lookup(&o->tables[2], *rgba, count);
    if (e[GLM_IMAGING_HISTOGRAM]) {
        GLsizei w = o->histogram.width;
        bool present[4];
        format_components(base_format(o->histogram.internal), present);
        if (w)
            for (size_t i = 0; i < count; ++i)
                for (int c = 0; c < 4; ++c)
                    if (present[c]) ++o->histogram.counts[(size_t)lookup_index((*rgba)[i * 4 + (size_t)c], w) * 4 + (size_t)c];
        if (o->histogram.sink) return false;
    }
    if (e[GLM_IMAGING_MINMAX]) {
        bool present[4];
        format_components(base_format(o->minmax.internal), present);
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < 4; ++c) {
                float v = (*rgba)[i * 4 + (size_t)c];
                if (!present[c]) continue;
                if (v < o->minmax.min[c]) o->minmax.min[c] = v;
                if (v > o->minmax.max[c]) o->minmax.max[c] = v;
            }
        if (o->minmax.sink) return false;
    }
    return true;
}

bool glm_imaging_read(struct glm_context *ctx, float **rgba, GLsizei *width, GLsizei *height)
{
    glm_transfer_rgba(ctx, *rgba, (size_t)*width * (size_t)*height);
    return glm_imaging_apply(ctx, 2, rgba, width, height);
}

/* ---- raw pixel data ----------------------------------------------------------- */

void glm_pixels_raw_begin(struct glm_context *ctx, struct glm_pixels_raw *saved)
{
    struct glm_state *s = &ctx->state;
    GLint *u[8] = {&s->unpack_alignment, &s->unpack_row_length, &s->unpack_skip_rows, &s->unpack_skip_pixels,
                   &s->unpack_image_height, &s->unpack_skip_images, &s->unpack_row_bytes, &s->unpack_image_bytes};
    for (int i = 0; i < 8; ++i) {
        saved->unpack[i] = *u[i];
        *u[i] = i == 0 ? 1 : 0;
    }
    saved->swap_bytes = s->unpack_swap_bytes;
    saved->lsb_first = s->unpack_lsb_first;
    saved->client_storage = s->unpack_client_storage;
    saved->raw = ctx->pixels_raw;
    saved->unpack_buffer = ctx->pixel_unpack_buffer;
    s->unpack_swap_bytes = s->unpack_lsb_first = s->unpack_client_storage = false;
    ctx->pixels_raw = true;
    ctx->pixel_unpack_buffer = 0;
}

void glm_pixels_raw_end(struct glm_context *ctx, const struct glm_pixels_raw *saved)
{
    struct glm_state *s = &ctx->state;
    GLint *u[8] = {&s->unpack_alignment, &s->unpack_row_length, &s->unpack_skip_rows, &s->unpack_skip_pixels,
                   &s->unpack_image_height, &s->unpack_skip_images, &s->unpack_row_bytes, &s->unpack_image_bytes};
    for (int i = 0; i < 8; ++i) *u[i] = saved->unpack[i];
    s->unpack_swap_bytes = saved->swap_bytes;
    s->unpack_lsb_first = saved->lsb_first;
    s->unpack_client_storage = saved->client_storage;
    ctx->pixels_raw = saved->raw;
    ctx->pixel_unpack_buffer = saved->unpack_buffer;
}
