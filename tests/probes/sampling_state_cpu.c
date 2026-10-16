/* The runner inserts production metadata setters. No GL context or GPU is used. */
#include "glm_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct glm_context contexts[2], *current = contexts;
static struct glm_share_group share;
static struct glm_texture texture;
struct glm_context *glm_current(void) { return current; }
void glm_error(struct glm_context *ctx, GLenum error) { ctx->error = error; }
void glm_log(const char *format, ...) { (void)format; }
struct glm_texture *glm_texture_bound(struct glm_context *ctx, GLenum target)
{ (void)ctx; return target == GL_TEXTURE_2D ? &texture : NULL; }
void *glm_table_get(struct glm_object_table *table, GLuint name)
{ return name < table->capacity ? table->items[name] : NULL; }
void glm_table_set(struct glm_object_table *table, GLuint name, void *item)
{
    if (name >= table->capacity) {
        GLuint old = table->capacity;
        table->capacity = name + 16;
        table->items = realloc(table->items, table->capacity * sizeof *table->items);
        assert(table->items);
        memset(table->items + old, 0, (table->capacity - old) * sizeof *table->items);
    }
    table->items[name] = item;
}
GLuint glm_table_reserve(struct glm_object_table *table) { return ++table->next_name; }

/* PRODUCTION_FUNCTIONS */

static uint64_t generation(void)
{ return __atomic_load_n(&current->share->sampling_generation, __ATOMIC_ACQUIRE); }
#define CHECK_DELTA(delta, statement) do { uint64_t before = generation(); statement; assert(generation() == before + (delta)); } while (0)
int main(void)
{
    contexts[0].share = contexts[1].share = &share;
    contexts[0].profile = contexts[1].profile = GLM_PROFILE_CORE;
    texture.target = GL_TEXTURE_2D;
    texture.max_level = 1000;
    texture.swizzle[1] = 1; texture.swizzle[2] = 2; texture.swizzle[3] = 3;
    CHECK_DELTA(0, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0));
    CHECK_DELTA(1, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1));
    CHECK_DELTA(0, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1));
    CHECK_DELTA(1, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3));
    CHECK_DELTA(1, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    CHECK_DELTA(0, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    CHECK_DELTA(1, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE));
    CHECK_DELTA(0, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE));
    GLint invalid[4] = {GL_RED, GL_GREEN, -1, GL_ALPHA};
    CHECK_DELTA(0, glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, invalid));
    assert(current->error == GL_INVALID_ENUM && texture.swizzle[0] == 2);
    current->error = 0;
    CHECK_DELTA(0, glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SRGB_DECODE_EXT, 0));
    assert(current->error == GL_INVALID_ENUM); current->error = 0;
    GLuint sampler;
    CHECK_DELTA(1, glGenSamplers(1, &sampler));
    CHECK_DELTA(0, glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    uint64_t observed = generation();
    current = contexts + 1;
    CHECK_DELTA(1, glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    current = contexts;
    assert(generation() == observed + 1);
    CHECK_DELTA(0, glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GLfloat border[4] = {0.2f, 0.4f, 0.6f, 1};
    CHECK_DELTA(1, glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, border));
    CHECK_DELTA(0, glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, border));
    CHECK_DELTA(0, glSamplerParameteri(sampler, GL_TEXTURE_SRGB_DECODE_EXT, 0));
    assert(current->error == GL_INVALID_ENUM); current->error = 0;
    CHECK_DELTA(0, glSamplerParameteri(sampler + 1, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    assert(current->error == GL_INVALID_OPERATION); current->error = 0;
    current->profile = GLM_PROFILE_LEGACY;
    CHECK_DELTA(0, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, 0.0f));
    CHECK_DELTA(1, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, -0.0f));
    uint32_t bits;
    memcpy(&bits, &texture.compare_fail_value, sizeof bits);
    assert(bits == UINT32_C(0x80000000));
    CHECK_DELTA(0, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, -0.0f));
    CHECK_DELTA(1, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, 0.0f));
    bits = UINT32_C(0x7fc12345);
    GLfloat nan_value;
    memcpy(&nan_value, &bits, sizeof nan_value);
    CHECK_DELTA(1, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, nan_value));
    CHECK_DELTA(0, glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, nan_value));
    uint32_t stored_bits;
    memcpy(&stored_bits, &texture.compare_fail_value, sizeof stored_bits);
    assert(stored_bits == bits);
    CHECK_DELTA(1, glDeleteSamplers(1, &sampler));
    CHECK_DELTA(0, glDeleteSamplers(1, &sampler));
    free(share.samplers.items);
    puts("sampling state CPU checks passed");
}
