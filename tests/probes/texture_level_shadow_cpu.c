/* CPU-only metadata/query regression:
   xcrun clang -fsanitize=address -g -O1 -ffunction-sections -Isrc -Ibuild/gen
     tests/probes/texture_level_shadow_cpu.c -Wl,-dead_strip -o build/texture_level_shadow_cpu
   The implementation query stubs represent images outside the local cache. */
#include "../../src/marshal/marshal_custom.c"
#include <assert.h>
#include <stdio.h>

static struct glm_context context;
static unsigned implementation_queries;
static GLint implementation_width;

struct glm_context *glm_current(void) { return &context; }
bool glm_texture_format_allowed(GLenum internal, bool core) { return internal == GL_RGBA8; }
bool glm_format_lookup(GLenum internal, struct glm_format_info *out)
{
    memset(out, 0, sizeof *out);
    return internal == GL_RGBA8;
}
const struct glm_apple_texture *glm_apple_texture_format(const struct glm_context *ctx, GLenum internal)
{
    return NULL;
}
int glm_thread_pending_zero(struct glm_context *ctx, int which) { return 1; }
void glm_thread_sync_named(struct glm_context *ctx, const char *name) {}
void glm_texture_query_override(struct glm_context *ctx, GLenum target, GLuint name) {}
void glm_texture_query_override_end(void) {}
void glm_impl_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *out)
{
    ++implementation_queries;
    *out = implementation_width;
}

static void expect_width(GLenum target, GLint level, GLint expected, unsigned fallbacks)
{
    GLint width = -1;
    unsigned before = implementation_queries;
    glGetTexLevelParameteriv(target, level, GL_TEXTURE_WIDTH, &width);
    assert(width == expected);
    assert(implementation_queries - before == fallbacks);
}

int main(void)
{
    /* Only a non-null stream marker is needed by the public query wrapper. */
    context.thread = (struct glm_thread *)(uintptr_t)1;
    struct glm_shadow *s = shadow(&context);
    s->textures[0][GLM_TEX_2D] = 1;
    shadow_define_level(&context, GL_TEXTURE_2D, 1, GL_RGBA8, 8, 4, 1);
    shadow_define_level(&context, GL_TEXTURE_2D, 0, GL_RGBA8, 16, 8, 1);
    expect_width(GL_TEXTURE_2D, 1, 8, 0);
    /* A zero in an unseen slot must not hide an implementation image. */
    implementation_width = 4;
    expect_width(GL_TEXTURE_2D, 2, 4, 1);
    for (int level = 3; level >= 0; --level)
        shadow_define_level(&context, GL_TEXTURE_2D, level, GL_RGBA8, 16 >> level, 8 >> level, 1);
    for (int level = 0; level <= 3; ++level)
        expect_width(GL_TEXTURE_2D, level, 16 >> level, 0);
    shadow_define_level(&context, GL_TEXTURE_2D, 4, GL_RGBA8, 0, 0, 1);
    expect_width(GL_TEXTURE_2D, 4, 0, 0);

    s->textures[0][GLM_TEX_CUBE] = 2;
    shadow_define_level(&context, GL_TEXTURE_CUBE_MAP_POSITIVE_X, 1, GL_RGBA8, 8, 8, 1);
    expect_width(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 1, 8, 0);
    expect_width(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 1, 4, 1);
    /* Without a known base, generated metadata must remain unknown. */
    glm_shadow_glGenerateMipmap(&context, GL_TEXTURE_CUBE_MAP);
    implementation_width = 2;
    expect_width(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 1, 2, 1);
    for (int face = 0; face < 6; ++face)
        shadow_define_level(&context, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, 8, 8, 1);
    glm_shadow_glGenerateMipmap(&context, GL_TEXTURE_CUBE_MAP);
    for (int face = 0; face < 6; ++face)
        expect_width(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 2, 2, 0);

    for (size_t i = 0; i < s->texture_count; ++i) free(s->textures_params[i].levels);
    free(s->textures_params);
    free(s->texture_index);
    free(s);
    puts("Reverse mip definitions, known faces and implementation fallback passed");
}
