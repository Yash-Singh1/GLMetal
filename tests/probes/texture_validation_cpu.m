/* No Metal device, GL context, or GPU work. The runner inserts the real
   production functions, so mutation tests cover the actual cache branches. */
#import <Metal/Metal.h>
#include "glm_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned properties, lookups, allocations, views, copies, invalidations, pending_flushes;
static bool allocation_failure, view_failure;
@interface CPUTexture : NSObject {
@public
    MTLTextureDescriptor *descriptor;
    MTLTextureSwizzleChannels channels;
}
@end
@implementation CPUTexture
- (NSUInteger)width { ++properties; return descriptor.width; }
- (NSUInteger)height { ++properties; return descriptor.height; }
- (NSUInteger)depth { ++properties; return descriptor.depth; }
- (NSUInteger)mipmapLevelCount { ++properties; return descriptor.mipmapLevelCount; }
- (NSUInteger)arrayLength { ++properties; return descriptor.arrayLength; }
- (NSUInteger)sampleCount { ++properties; return descriptor.sampleCount; }
- (MTLPixelFormat)pixelFormat { ++properties; return descriptor.pixelFormat; }
- (MTLTextureType)textureType { ++properties; return descriptor.textureType; }
- (MTLTextureSwizzleChannels)swizzle { ++properties; return channels; }
- (id)newTextureViewWithPixelFormat:(MTLPixelFormat)format textureType:(MTLTextureType)type
                            levels:(NSRange)levels slices:(NSRange)slices swizzle:(MTLTextureSwizzleChannels)swizzle
{
    ++views;
    if (view_failure) return nil;
    CPUTexture *view = [CPUTexture new];
    view->descriptor = [descriptor copy];
    view->descriptor.pixelFormat = format;
    view->descriptor.textureType = type;
    view->descriptor.width = MAX(descriptor.width >> levels.location, 1);
    view->descriptor.height = MAX(descriptor.height >> levels.location, 1);
    view->descriptor.mipmapLevelCount = levels.length;
    view->descriptor.arrayLength = slices.length;
    view->channels = swizzle;
    return view;
}
@end
@interface CPUDevice : NSObject
@end
@implementation CPUDevice
- (id)newTextureWithDescriptor:(MTLTextureDescriptor *)descriptor
{
    ++allocations;
    if (allocation_failure) return nil;
    CPUTexture *texture = [CPUTexture new];
    texture->descriptor = [descriptor copy];
    texture->channels = MTLTextureSwizzleChannelsMake(0, 1, 2, 3);
    return texture;
}
@end
@interface CPUBlit : NSObject
@end
@implementation CPUBlit
- (void)copyFromTexture:(id)source sourceSlice:(NSUInteger)a sourceLevel:(NSUInteger)b toTexture:(id)target
      destinationSlice:(NSUInteger)c destinationLevel:(NSUInteger)d sliceCount:(NSUInteger)e levelCount:(NSUInteger)f
{ ++copies; }
- (void)endEncoding {}
- (void)generateMipmapsForTexture:(id)texture {}
@end
@interface CPUCommand : NSObject
@end
@implementation CPUCommand
- (id)blitCommandEncoder { return [CPUBlit new]; }
@end
static id<MTLDevice> metal_device(void) { static CPUDevice *device; if (!device) device = [CPUDevice new]; return (id)device; }
void *glm_backend_command_buffer(struct glm_context *ctx)
{ static CPUCommand *command; if (!command) command = [CPUCommand new]; return (__bridge void *)command; }
void glm_backend_release(void *value) { if (value) CFBridgingRelease(value); }
void glm_backend_framebuffer_changed(struct glm_context *ctx) { ++invalidations; }
static void flush_pending(struct glm_context *ctx, struct glm_texture *t) { ++pending_flushes; }
static void generate_compressed_mipmaps(struct glm_context *ctx, struct glm_texture *t, id<MTLTexture> storage) {}
static void generate_cpu_mipmaps(struct glm_context *ctx, struct glm_texture *t, id<MTLTexture> storage) {}
void glm_backend_upload_encoded(struct glm_context *ctx) {}
void glm_log(const char *format, ...) {}
void glm_error(struct glm_context *ctx, GLenum error) { ctx->error = error; }
bool glm_texture_format_allowed(GLenum format, bool core) { return true; }
bool glm_format_lookup(GLenum format, struct glm_format_info *info)
{
    ++lookups;
    *info = (struct glm_format_info){0};
    info->metal_format = format == GL_SRGB8_ALPHA8 ? MTLPixelFormatRGBA8Unorm_sRGB :
                         format == GL_DEPTH_COMPONENT32F ? MTLPixelFormatDepth32Float : MTLPixelFormatRGBA8Unorm;
    info->depth = format == GL_DEPTH_COMPONENT32F;
    memcpy(info->swizzle, (uint8_t[]){0, 1, 2, 3}, 4);
    return true;
}

/* PRODUCTION_FUNCTIONS */

static struct glm_share_group share;
static struct glm_context context = {.share = &share};
static struct glm_texture texture;
static void reset(GLenum target, GLenum format)
{
    glm_backend_release(texture.backend);
    glm_backend_release(texture.view);
    memset(&texture, 0, sizeof texture);
    texture.target = target;
    texture.sampler.min_filter = GL_LINEAR;
    texture.max_level = 1000;
    texture.depth_texture_mode = GL_LUMINANCE;
    memcpy(texture.swizzle, (uint8_t[]){0, 1, 2, 3}, 4);
    context.profile = GLM_PROFILE_LEGACY;
    assert(define_level(&context, &texture, target, 0, format, 64, 64, 1));
}
static id<MTLTexture> sample(bool skip) { bool mip; return sampling_view(&context, &texture, skip, &mip); }
static void mutation_tests(void)
{
    reset(GL_TEXTURE_2D, GL_RGBA8);
    id<MTLTexture> first = sample(false);
    assert(first && first == (__bridge id)texture.backend);
    properties = lookups = 0;
    for (int i = 0; i < 1000; ++i) { assert(sample(false) == first); assert(glm_backend_texture_ensure_storage(&context, &texture)); }
    assert(properties == 0 && lookups == 0);
    unsigned previous = allocations;
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 1));
    assert(sample(false) == first && allocations == previous);
    // Texel writes and unrelated sampler fields do not affect a view.
    ++texture.gpu_serial;
    texture.sampler.max_anisotropy = 4;
    texture.sampler.wrap_s = GL_CLAMP_TO_BORDER;
    texture.sampler.compare_mode = GL_COMPARE_R_TO_TEXTURE;
    assert(sample(false) == first && lookups == 1); // define_level's format validation only.
    texture.max_level = 0;
    assert(sample(false) != first);
    texture.max_level = 1000;
    assert(sample(false) == first);
    texture.base_level = 1;
    assert(!sample(false));
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32, 1));
    id<MTLTexture> rebased = sample(false);
    assert(rebased && rebased != first && rebased.width == 32);
    texture.base_level = 0;
    texture.sampler.min_filter = GL_LINEAR_MIPMAP_LINEAR;
    assert(!sample(false));
    for (int l = 2; l <= 6; ++l) assert(define_level(&context, &texture, GL_TEXTURE_2D, l, GL_RGBA8, 64 >> l, 64 >> l, 1));
    assert(sample(false) == first);
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 4, GL_RGBA8, 5, 4, 1));
    assert(!sample(false));
    texture.sampler.min_filter = GL_LINEAR;
    assert(sample(false));
    // Changed base dimensions rebuild storage and select the new view.
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 1));
    id<MTLTexture> larger = sample(false);
    assert(larger && larger != first && larger.width == 128);
    texture.storage_dirty = true;
    assert(sample(false) != larger);
    reset(GL_TEXTURE_2D, GL_SRGB8_ALPHA8);
    id<MTLTexture> decoded = sample(false), linear = sample(true);
    assert(decoded.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB && linear.pixelFormat == MTLPixelFormatRGBA8Unorm);
    assert(sample(false) == decoded && sample(true) == linear);
    texture.swizzle[0] = 3;
    assert(sample(true).swizzle.red == MTLTextureSwizzleAlpha);
    reset(GL_TEXTURE_2D, GL_DEPTH_COMPONENT32F);
    assert(sample(false).swizzle.green == MTLTextureSwizzleRed);
    context.profile = GLM_PROFILE_CORE;
    assert(sample(false).swizzle.green == MTLTextureSwizzleZero);
    context.profile = GLM_PROFILE_LEGACY;
    texture.depth_texture_mode = GL_ALPHA;
    assert(sample(false).swizzle.alpha == MTLTextureSwizzleRed);
    // Allocation failures and failed view replacements must remain retryable.
    reset(GL_TEXTURE_2D, GL_RGBA8);
    allocation_failure = true;
    assert(!sample(false));
    allocation_failure = false;
    assert(sample(false));
    texture.max_level = 0;
    assert(sample(false));
    texture.swizzle[0] = 3;
    view_failure = true;
    assert(!sample(false));
    view_failure = false;
    texture.swizzle[0] = 0;
    assert(sample(false));
    // Storage recreation preserves the existing pending-upload callback.
    struct glm_pending_level pending = {0};
    texture.pending = &pending;
    texture.storage_dirty = true;
    previous = pending_flushes;
    assert(sample(false) && pending_flushes == previous + 1);
    texture.pending = NULL;
    // Object/name reuse begins with empty per-object caches.
    reset(GL_TEXTURE_2D, GL_RGBA8);
    assert(!texture.sampling_validation_valid && sample(false));
    // The real GPU-mipmap bookkeeping must invalidate a cached decision.
    texture.sampler.min_filter = GL_LINEAR_MIPMAP_LINEAR;
    assert(!sample(false));
    uint64_t definitions = texture.definition_generation;
    uint64_t resources = __atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE);
    glm_backend_generate_mipmaps(&context, &texture);
    assert(texture.definition_generation > definitions && sample(false));
    assert(__atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE) > resources);
    assert(texture.levels[0][6].defined);
    // GL format aliases can change FBO validity without replacing Metal storage.
    texture.sampler.min_filter = GL_LINEAR;
    id<MTLTexture> same_storage = sample(false);
    resources = __atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE);
    uint64_t storage_generation = texture.storage_generation;
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 1));
    assert(!texture.storage_dirty);
    assert(__atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE) > resources);
    assert(sample(false) == same_storage && texture.storage_generation == storage_generation);
    resources = __atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE);
    assert(define_level(&context, &texture, GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 1));
    assert(__atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE) == resources);
    // Successful backend replacement advances the shared generation independently.
    texture.storage_dirty = true;
    resources = __atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE);
    assert(sample(false) != same_storage && texture.storage_generation > storage_generation);
    assert(__atomic_load_n(&share.framebuffer_resource_generation, __ATOMIC_ACQUIRE) > resources);
    // All cube faces and their generated levels participate in completeness.
    reset(GL_TEXTURE_CUBE_MAP, GL_RGBA8);
    assert(!sample(false));
    for (int face = 1; face < 6; ++face)
        assert(define_level(&context, &texture, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, 64, 64, 1));
    assert(sample(false));
    texture.sampler.min_filter = GL_LINEAR_MIPMAP_LINEAR;
    glm_backend_generate_mipmaps(&context, &texture);
    assert(sample(false) && texture.levels[5][6].defined);
    assert(define_level(&context, &texture, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 6, GL_RGBA8, 2, 1, 1));
    assert(!sample(false));
    reset(GL_TEXTURE_2D_ARRAY, GL_RGBA8);
    assert(define_level(&context, &texture, GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 64, 64, 4));
    assert(sample(false).arrayLength == 4);
    assert(define_level(&context, &texture, GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 64, 64, 8));
    assert(sample(false).arrayLength == 8);
    reset(GL_TEXTURE_1D_ARRAY, GL_RGBA8);
    assert(define_level(&context, &texture, GL_TEXTURE_1D_ARRAY, 0, GL_RGBA8, 64, 5, 1));
    assert(sample(false).height == 1 && sample(false).arrayLength == 5);
    reset(GL_TEXTURE_3D, GL_RGBA8);
    assert(define_level(&context, &texture, GL_TEXTURE_3D, 0, GL_RGBA8, 64, 64, 4));
    assert(sample(false).depth == 4);
    assert(define_level(&context, &texture, GL_TEXTURE_3D, 0, GL_RGBA8, 64, 64, 8));
    assert(sample(false).depth == 8);
    reset(GL_TEXTURE_2D_MULTISAMPLE, GL_RGBA8);
    texture.samples = 2;
    assert(sample(false).sampleCount == 2);
    texture.samples = 4;
    texture.storage_dirty = true;
    assert(sample(false).sampleCount == 4);
    reset(GL_TEXTURE_RECTANGLE_ARB, GL_RGBA8);
    bool mip = true;
    assert(sampling_view(&context, &texture, false, &mip) && !mip);
    mip = true;
    assert(sampling_view(&context, &texture, false, &mip) && !mip);
    // Undefined base levels still fall back to level zero for storage uploads.
    texture.base_level = 3;
    assert(glm_backend_texture_ensure_storage(&context, &texture));
    assert(!sample(false));
    reset(GL_TEXTURE_2D, GL_RGBA8);
    texture.immutable = true;
    texture.immutable_levels = 1;
    texture.sampler.min_filter = GL_LINEAR_MIPMAP_LINEAR;
    assert(sample(false));
    texture.immutable_levels = 7;
    assert(!sample(false));
    fprintf(stdout, "Mutation checks passed; stable bindings performed zero Metal property calls and format lookups.\n");
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
__attribute__((noinline)) static uintptr_t bench(bool cached, int count)
{
    uintptr_t sum = 0;
    for (int i = 0; i < count; ++i) {
        if (!cached) { texture.sampling_validation_valid = false; texture.storage_validation_valid = false; }
        sum ^= (uintptr_t)(__bridge void *)sample(false);
    }
    return sum;
}
int main(void)
{
    @autoreleasepool {
        mutation_tests();
        reset(GL_TEXTURE_2D, GL_RGBA8);
        texture.sampler.min_filter = GL_LINEAR_MIPMAP_LINEAR;
        for (int l = 1; l <= 6; ++l) assert(define_level(&context, &texture, GL_TEXTURE_2D, l, GL_RGBA8, 64 >> l, 64 >> l, 1));
        assert(sample(false));
        int count = 500000;
        properties = lookups = 0;
        double start = now();
        volatile uintptr_t sum = bench(false, count);
        double slow = now() - start;
        unsigned slow_properties = properties, slow_lookups = lookups;
        properties = lookups = 0;
        start = now(); sum ^= bench(true, count); double fast = now() - start;
        assert(properties == 0 && lookups == 0);
        printf("%d mipmapped bindings: validation %.2f ms, cached %.2f ms, %.2fx; property calls %u -> %u, format lookups %u -> %u.\n",
               count, slow * 1000, fast * 1000, slow / fast, slow_properties, properties, slow_lookups, lookups);
        reset(GL_TEXTURE_2D, GL_RGBA8);
    }
    return 0;
}
