/* The runner inserts actual objects.c functions. No driver library, GL
   context, Metal device, or GPU work is needed. */
#include "glm_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct glm_context context;
static struct glm_share_group share;
static unsigned invalidations, checks;
struct glm_context *glm_current(void) { return &context; }
void glm_error(struct glm_context *ctx, GLenum error) { ctx->error = error; }
void glm_backend_framebuffer_changed(struct glm_context *ctx) { assert(ctx == &context); ++invalidations; }
void *glm_table_get(struct glm_object_table *table, GLuint name)
{ return name < table->capacity ? table->items[name] : NULL; }
void glm_table_set(struct glm_object_table *table, GLuint name, void *item)
{
    if (name >= table->capacity) {
        GLuint old = table->capacity;
        table->capacity = name + 32;
        table->items = realloc(table->items, table->capacity * sizeof *table->items);
        assert(table->items);
        memset(table->items + old, 0, (table->capacity - old) * sizeof *table->items);
    }
    table->items[name] = item;
}
GLuint glm_table_reserve(struct glm_object_table *table) { return ++table->next_name; }
struct glm_texture *glm_texture_get(struct glm_context *ctx, GLuint name)
{ return glm_table_get(&ctx->share->textures, name); }

/* PRODUCTION_FUNCTIONS */

static void expect_at(unsigned expected, int line)
{
    ++checks;
    if (invalidations != expected) {
        fprintf(stderr, "Fixture line %d: expected %u invalidations, received %u\n", line, expected, invalidations);
        abort();
    }
    invalidations = 0;
}
#define expect(expected) expect_at(expected, __LINE__)
static void expect_error(GLenum error)
{ ++checks; assert(context.error == error); context.error = GL_NO_ERROR; expect(0); }
static void bind_tests(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 1); expect(1);
    for (int i = 0; i < 1000; ++i) glBindFramebuffer(GL_FRAMEBUFFER, 1);
    expect(0);
    assert(context.draw_framebuffer == 1 && context.read_framebuffer == 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 2); expect(0);
    assert(context.draw_framebuffer == 1 && context.read_framebuffer == 2);
    glBindFramebuffer(GL_FRAMEBUFFER, 1); expect(0);
    assert(context.read_framebuffer == 1);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 2); expect(1);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 2); expect(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); expect(1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 2); expect(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 1); expect(1);
    GLuint draw = context.draw_framebuffer, read = context.read_framebuffer;
    glBindFramebuffer(0xFFFF, 1); expect_error(GL_INVALID_ENUM);
    assert(context.draw_framebuffer == draw && context.read_framebuffer == read);
}
static void attachment_tests(void)
{
    struct glm_texture texture = {.name = 3, .target = GL_TEXTURE_2D_ARRAY};
    glm_table_set(&share.textures, 3, &texture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 3, 0); expect(1);
    for (int i = 0; i < 1000; ++i)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 3, 0);
    expect(0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 3, 1); expect(1);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1, 2); expect(1);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1, 2); expect(0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, 3, 1); expect(1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 3, 1); expect(1);
    // Compare final whole-texture state, including the layered bit.
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1); expect(1);
    assert(glm_framebuffer_get(&context, 1)->color[0].layered);
    for (int i = 0; i < 1000; ++i) glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1);
    expect(0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1, 0); expect(1);
    assert(!glm_framebuffer_get(&context, 1)->color[0].layered);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1); expect(1);
    texture.target = GL_TEXTURE_2D;
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1); expect(1);
    assert(!glm_framebuffer_get(&context, 1)->color[0].layered);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 1); expect(0);
    // Equal depth alone cannot hide a different stencil attachment.
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 4); expect(1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 4); expect(0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 5); expect(1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 4); expect(1);
    struct glm_framebuffer *fb = glm_framebuffer_get(&context, 1);
    assert(fb->depth.name == 4 && fb->stencil.name == 4);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 5); expect(1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 4); expect(1);
    // Attachment type is part of equality even when its name is unchanged.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 4, 0); expect(1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 4); expect(1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 2); expect(0);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 0); expect(0);
    assert(glm_framebuffer_get(&context, 2)->color[0].name == 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 1); expect(0);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 0); expect(1);
    glFramebufferTexture(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 0); expect(0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, 0xFFFF, GL_TEXTURE_2D, 3, 0); expect_error(GL_INVALID_ENUM);
    glFramebufferTexture(0xFFFF, GL_COLOR_ATTACHMENT0, 3, 0); expect_error(GL_INVALID_ENUM);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); expect(1);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3, 0); expect_error(GL_INVALID_OPERATION);
    glBindFramebuffer(GL_FRAMEBUFFER, 1); expect(1);
    glm_table_set(&share.textures, 3, NULL);
}
static void draw_buffer_tests(void)
{
    GLenum two[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, two); expect(1);
    glDrawBuffers(2, two); expect(0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); expect(1);
    assert(glm_framebuffer_get(&context, 1)->draw_buffers[1] == GL_NONE);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); expect(0);
    glDrawBuffers(2, two); expect(1);
    glDrawBuffers(1, two); expect(1);
    for (int i = 1; i < GLM_MAX_DRAW_BUFFERS; ++i)
        assert(glm_framebuffer_get(&context, 1)->draw_buffers[i] == GL_NONE);
    glDrawBuffers(1, two); expect(0);
    glDrawBuffers(0, NULL); expect(1);
    glDrawBuffer(GL_NONE); expect(0);
    glDrawBuffers(-1, two); expect_error(GL_INVALID_VALUE);
    glDrawBuffers(GLM_MAX_DRAW_BUFFERS + 1, two); expect_error(GL_INVALID_VALUE);
    GLenum duplicate[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0};
    glDrawBuffers(2, duplicate); expect_error(GL_INVALID_OPERATION);
    assert(glm_framebuffer_get(&context, 1)->draw_buffers[0] == GL_NONE);
    GLenum bad = GL_COLOR_ATTACHMENT0 + GLM_MAX_DRAW_BUFFERS;
    glDrawBuffers(1, &bad); expect_error(GL_INVALID_ENUM);
    for (int i = 0; i < GLM_MAX_DRAW_BUFFERS; ++i)
        assert(glm_framebuffer_get(&context, 1)->draw_buffers[i] == GL_NONE);
    glDrawBuffer(GL_FRONT); expect_error(GL_INVALID_OPERATION);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); expect(1);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); expect(1);
    glDrawBuffer(GL_FRONT); expect(1);
    glDrawBuffer(GL_FRONT); expect(0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); expect_error(GL_INVALID_OPERATION);
    glBindFramebuffer(GL_FRAMEBUFFER, 1); expect(1);
}
static void read_buffer_tests(void)
{
    context.profile = GLM_PROFILE_LEGACY;
    glReadBuffer(GL_NONE); expect(1);
    glReadBuffer(GL_NONE); expect(0);
    glReadBuffer(GL_COLOR_ATTACHMENT1); expect(1);
    glReadBuffer(GL_COLOR_ATTACHMENT1); expect(0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 2); expect(0);
    glReadBuffer(GL_NONE); expect(0);
    glReadBuffer(GL_COLOR_ATTACHMENT1); expect(0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 1); expect(0);
    context.profile = GLM_PROFILE_CORE;
    glReadBuffer(GL_NONE); expect(0);
    glReadBuffer(GL_COLOR_ATTACHMENT0); expect(0);
    glReadBuffer(GL_FRONT); expect_error(GL_INVALID_OPERATION);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + GLM_MAX_DRAW_BUFFERS); expect_error(GL_INVALID_ENUM);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); expect(1);
    context.profile = GLM_PROFILE_LEGACY;
    glReadBuffer(GL_FRONT); expect(0);
    glReadBuffer(GL_NONE); expect(0);
    glReadBuffer(GL_FRONT_AND_BACK); expect_error(GL_INVALID_ENUM);
}
static void deletion_tests(void)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 1); expect(1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 2); expect(0);
    GLuint name = 2;
    glDeleteFramebuffers(1, &name); expect(0);
    assert(context.read_framebuffer == 0 && context.draw_framebuffer == 1);
    glDeleteFramebuffers(1, &name); expect(0);
    glDeleteFramebuffers(-1, &name); expect_error(GL_INVALID_VALUE);
    name = 1;
    glDeleteFramebuffers(1, &name); expect(1);
    assert(context.draw_framebuffer == 0);
    glDeleteFramebuffers(1, &name); expect(0);
}
int main(void)
{
    context.share = &share;
    context.profile = GLM_PROFILE_LEGACY;
    bind_tests(); attachment_tests(); draw_buffer_tests(); read_buffer_tests(); deletion_tests();
    for (GLuint i = 0; i < context.framebuffers.capacity; ++i) free(context.framebuffers.items[i]);
    free(context.framebuffers.items);
    free(share.textures.items);
    printf("Framebuffer CPU state checks passed: %u invalidation/error checks; 3000 redundant bind/attachment calls caused zero pass invalidations.\n", checks);
    return 0;
}
