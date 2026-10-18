/* Line and polygon stipple state (GL 2.1 3.4.2, 3.5.2). The polygon
   stipple is applied by a fragment function variant (metal_backend.m,
   glm_polygon_stipple_msl). */
#include "glm_internal.h"
#include <string.h>

GLM_EXPORT void glLineStipple(GLint factor, GLushort pattern)
{
    GLM_CONTEXT(ctx);
    ctx->state.line_stipple_factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
    ctx->state.line_stipple_pattern = pattern;
    __atomic_add_fetch(&ctx->state_serial, 1, __ATOMIC_RELAXED);
}

/* The 32x32 pattern is a bitmap image in client memory (or the pixel
   unpack buffer), unpacked as glBitmap's are. */
GLM_EXPORT void glPolygonStipple(const GLubyte *mask)
{
    GLM_CONTEXT(ctx);
    struct glm_state *s = &ctx->state;
    const uint8_t *bits = mask;
    if (ctx->pixel_unpack_buffer) {
        struct glm_buffer *buffer = glm_buffer_get(ctx, ctx->pixel_unpack_buffer);
        const uint8_t *contents = buffer ? glm_backend_buffer_contents(ctx, buffer, false) : NULL;
        bits = contents ? contents + (uintptr_t)mask : NULL;
    }
    if (!bits) return;
    size_t row_pixels = s->unpack_row_length > 0 ? (size_t)s->unpack_row_length : 32;
    size_t alignment = (size_t)(s->unpack_alignment > 0 ? s->unpack_alignment : 4);
    size_t row_bytes = s->unpack_row_bytes > 0 ? (size_t)s->unpack_row_bytes
                                               : ((row_pixels + 7) / 8 + alignment - 1) / alignment * alignment;
    uint8_t pattern[128] = {0};
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            size_t bit = (size_t)s->unpack_skip_pixels + (size_t)x;
            uint8_t byte = bits[((size_t)s->unpack_skip_rows + (size_t)y) * row_bytes + bit / 8];
            bool on = s->unpack_lsb_first ? (byte >> (bit % 8)) & 1 : (byte >> (7 - bit % 8)) & 1;
            if (on) pattern[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
    memcpy(s->polygon_stipple_pattern, pattern, sizeof pattern);
    __atomic_add_fetch(&ctx->state_serial, 1, __ATOMIC_RELAXED);
}

/* Packed as a 32x32 bitmap with the pack state. */
GLM_EXPORT void glGetPolygonStipple(GLubyte *mask)
{
    GLM_CONTEXT(ctx);
    struct glm_state *s = &ctx->state;
    uint8_t *bits = mask;
    if (ctx->pixel_pack_buffer) {
        struct glm_buffer *buffer = glm_buffer_get(ctx, ctx->pixel_pack_buffer);
        uint8_t *contents = buffer ? glm_backend_buffer_contents(ctx, buffer, true) : NULL;
        bits = contents ? contents + (uintptr_t)mask : NULL;
    }
    if (!bits) return;
    size_t row_pixels = s->pack_row_length > 0 ? (size_t)s->pack_row_length : 32;
    size_t alignment = (size_t)(s->pack_alignment > 0 ? s->pack_alignment : 4);
    size_t row_bytes = ((row_pixels + 7) / 8 + alignment - 1) / alignment * alignment;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            size_t bit = (size_t)s->pack_skip_pixels + (size_t)x;
            uint8_t *byte = &bits[((size_t)s->pack_skip_rows + (size_t)y) * row_bytes + bit / 8];
            uint8_t flag = (uint8_t)(0x80 >> (bit % 8));
            if (s->polygon_stipple_pattern[y * 4 + x / 8] & (0x80 >> (x % 8))) *byte |= flag;
            else *byte &= (uint8_t)~flag;
        }
}
