/* The runner inserts production sampling keys and binding decisions. */
#include "glm_internal.h"
#include "programs.h"
#include "sampler_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long NSUInteger;
enum { STAGE_VERTEX = 1, STAGE_FRAGMENT = 2, STAGE_BOTH = 3 };
/* PRODUCTION_KEY */
struct glm_backend_context {
    void *encoder;
    uint64_t encoder_serial;
    struct { uint64_t resource_serial[2]; } cache;
    struct {
        bool valid, quick_only;
        uint64_t encoder, resource_serial, state_serial;
        int units[64];
        struct program_sampling_key key;
    } sampling[2];
    struct { bool valid, live_sampling; } fast;
    bool sampling_cache_enabled, inline_bytes_stats;
    uint64_t sampling_calls, sampling_reused;
    uint32_t vertex_id_base, vertex_id_offset, vertex_id_mapped, border_mask;
    float border_data[64][4];
};
static unsigned calls, last_stages, byte_calls, border_calls;
static bool bind_ok = true, initialize_units, bind_reusable = true;
static void enc_bytes(struct glm_backend_context *b, unsigned stages, const void *bytes,
                      NSUInteger length, NSUInteger slot)
{
    (void)bytes; (void)length;
    ++byte_calls;
    if (slot == GLM_SLOT_BORDER) ++border_calls;
    if (slot == GLM_SLOT_LOD_BIAS || slot == GLM_SLOT_BORDER)
        for (int stage = 0; stage < 2; ++stage)
            if (stages & (1u << stage)) ++b->cache.resource_serial[stage];
}
static bool glm_bind_program_textures(struct glm_context *ctx, void *encoder,
    struct glm_program *p, unsigned stages, uint32_t border_mask, float (*bias)[4], bool *reusable)
{
    (void)encoder; (void)border_mask; (void)bias;
    ++calls; last_stages = stages; *reusable = bind_reusable;
    if (initialize_units) { p->sampler_units[0] = 7; initialize_units = false; }
    struct glm_backend_context *b = ctx->backend;
    for (int stage = 0; stage < 2; ++stage)
        if (stages & (1u << stage)) ++b->cache.resource_serial[stage];
    return bind_ok;
}
#define MIN(a,b) ((a)<(b)?(a):(b))
/* PRODUCTION_FUNCTIONS */

static struct glm_context ctx;
static struct glm_share_group share;
static struct glm_backend_context backend;
static struct glm_program program;
static struct glm_uniform_info uniform;
static unsigned checks;
static void reset(void)
{
    memset(&ctx, 0, sizeof ctx); memset(&share, 0, sizeof share);
    memset(&backend, 0, sizeof backend); memset(&program, 0, sizeof program);
    uniform = (struct glm_uniform_info){.name = "image", .type = GL_SAMPLER_2D, .offset = -1, .sampler_slot = 0, .array_size = 1};
    ctx.share = &share; ctx.backend = &backend;
    backend.encoder = &backend; backend.encoder_serial = 1; backend.sampling_cache_enabled = true;
    program.link_serial = ++checks;
    program.result.sampler_count = 1; program.result.uniform_count = 1; program.result.uniforms = &uniform;
    calls = byte_calls = border_calls = 0; bind_ok = true; initialize_units = false; bind_reusable = true;
}
static void expect(unsigned stages, unsigned expected)
{
    unsigned before = calls;
    assert(bind_program_state(&ctx, &program, stages) == bind_ok);
    assert(calls == before + (expected != 0));
    if (expected) assert(last_stages == expected);
    ++checks;
}
#define CHANGE(statement) do { statement; expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, 0); } while (0)
int main(void)
{
    reset();
    expect(STAGE_BOTH, STAGE_BOTH);
    ++ctx.state_serial;
    expect(STAGE_BOTH, STAGE_BOTH); /* The first raster edit builds the full dependency key. */
    for (int i = 0; i < 1000; ++i) expect(STAGE_BOTH, 0);
    assert(byte_calls >= 1001); /* Point data remains live on every draw. */
    ctx.state.point_size = 9; ctx.state.program_point_size = true;
    ctx.point_sprite_origin = GL_UPPER_LEFT;
    ctx.state.viewport[2] = 640; ctx.state.scissor[0] = 12;
    ctx.state.depth_test = true; ctx.state.cull_face = true;
    ++ctx.state_serial;
    ctx.state.blend_any = true;
    ++program.uniform_serial;
    expect(STAGE_BOTH, 0);
    ctx.state.units[15].bound[GLM_TEX_CUBE] = 1234; ++ctx.state_serial;
    expect(STAGE_BOTH, 0); /* An unused target/unit does not affect sampling. */
    CHANGE(glm_sampling_resource_changed(&share));
    CHANGE(program.sampler_units[0] = 3);
    CHANGE(ctx.state.units[3].bound[GLM_TEX_2D] = 17; ++ctx.state_serial);
    CHANGE(ctx.state.units[3].sampler = 21; ++ctx.state_serial);
    CHANGE(ctx.state.units[3].env.lod_bias = 0.5f; ++ctx.state_serial);
    CHANGE(ctx.state.texture_cube_seamless = true; ++ctx.state_serial);
    CHANGE(++program.link_serial);
    struct glm_program other = program;
    unsigned before = calls;
    assert(bind_program_state(&ctx, &other, STAGE_BOTH));
    assert(calls == before + 1);
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, 0);
    /* The same address with a fresh link serial represents program reuse. */
    CHANGE(program.link_serial += 100);
    CHANGE(++backend.encoder_serial);
    ++backend.cache.resource_serial[0];
    expect(STAGE_BOTH, STAGE_VERTEX); expect(STAGE_BOTH, 0);
    ++backend.cache.resource_serial[1];
    expect(STAGE_BOTH, STAGE_FRAGMENT); expect(STAGE_BOTH, 0);
    CHANGE(backend.border_mask = 1);
    assert(border_calls);
    reset();
    expect(STAGE_VERTEX, STAGE_VERTEX);
    expect(STAGE_BOTH, STAGE_FRAGMENT);
    expect(STAGE_BOTH, 0);
    reset();
    initialize_units = true;
    expect(STAGE_BOTH, STAGE_BOTH);
    assert(program.sampler_units[0] == 7);
    expect(STAGE_BOTH, 0);
    reset(); bind_ok = false;
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, STAGE_BOTH);
    bind_ok = true; expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, 0);
    reset(); bind_reusable = false;
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, STAGE_BOTH);
    assert(backend.fast.live_sampling);
    bind_reusable = true; expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, 0);
    reset(); backend.encoder = NULL;
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, STAGE_BOTH);
    reset(); backend.sampling_cache_enabled = false;
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, STAGE_BOTH);
    reset(); uniform.type = GL_SAMPLER_BUFFER;
    expect(STAGE_BOTH, STAGE_BOTH); expect(STAGE_BOTH, STAGE_BOTH);
    assert(backend.fast.live_sampling);
    reset(); program.result.sampler_count = 0;
    expect(STAGE_BOTH, 0); expect(STAGE_BOTH, 0);
    reset();
    struct glm_program target = program;
    uniform.array_size = 2;
    program.sampler_units[0] = 2; program.sampler_units[1] = 4;
    remap_uniforms(&program, &target.result, NULL, target.sampler_units, &target.sampler_serial);
    assert(target.sampler_serial == 1 && target.sampler_units[0] == 2 && target.sampler_units[1] == 4);
    remap_uniforms(&program, &target.result, NULL, target.sampler_units, &target.sampler_serial);
    assert(target.sampler_serial == 1);
    program.sampler_units[1] = 5;
    remap_uniforms(&program, &target.result, NULL, target.sampler_units, &target.sampler_serial);
    assert(target.sampler_serial == 2 && target.sampler_units[1] == 5);
    int local_units[64]; /* Compute maps do not carry persistent program state. */
    remap_uniforms(&program, &target.result, NULL, local_units, NULL);
    assert(local_units[0] == 2 && local_units[1] == 5 && target.sampler_serial == 2);
    uniform.sampler_slot = 63;
    program.sampler_units[63] = 11;
    remap_uniforms(&program, &target.result, NULL, target.sampler_units, &target.sampler_serial);
    assert(target.sampler_serial == 3 && target.sampler_units[63] == 11);
    uniform.sampler_slot = -1; uniform.offset = 0; uniform.type = GL_FLOAT; uniform.array_size = 1;
    float globals[4] = {0}, source_globals[4] = {0.25f, 0, 0, 0};
    program.globals = (uint8_t *)source_globals; program.result.global_size = sizeof source_globals;
    target.result.global_size = sizeof globals;
    remap_uniforms(&program, &target.result, (uint8_t *)globals, target.sampler_units, &target.sampler_serial);
    assert(globals[0] == 0.25f && target.sampler_serial == 3);
    puts("sampler remap revisions: changed/unchanged arrays, compute maps, bounds, ordinary uniforms passed");
    printf("resource binding CPU checks passed: %u\n", checks);
}
