/* Learn the most frequently used pipeline configuration for each vertex
   input signature. New programs can prepare that configuration while the
   application translates subsequent shaders. Predictions never select draw
   state: acquisition compares functions and every descriptor field used by
   pipeline_for. A different target, blend mode or vertex layout is a miss.

   Two background compiler jobs, at most 32 outstanding predictions and 512
   cached states bound speculation. Up to 128 pairs skipped before recipe
   confidence are retained for ten seconds and retried four at a time when
   matching recipes are learned. A draw can claim a queued job immediately
   rather than waiting behind predictions it will never use. */
#import "pipeline_prewarm.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CACHE_LIMIT = 512, PENDING_LIMIT = 32, SIGNATURE_LIMIT = 128, RECIPE_LIMIT = 4, DEFERRED_LIMIT = 128, RETRY_LIMIT = 4 };
@interface GLMPipelineEntry : NSObject {
@public
    dispatch_group_t group;
    MTLRenderPipelineDescriptor *descriptor;
    id<MTLRenderPipelineState> state;
    NSError *error;
    bool started, done, predicted;
}
@end
@implementation GLMPipelineEntry
@end
@interface GLMPipelineRecipe : NSObject {
@public
    MTLRenderPipelineDescriptor *descriptor;
    NSData *key;
    unsigned uses;
}
@end
@implementation GLMPipelineRecipe
@end

/* Retain only recently uploaded pairs whose recipe was not yet confident. */
@interface GLMDeferredPair : NSObject {
@public
    id<MTLDevice> device;
    id<MTLFunction> vertex, fragment;
    NSData *signature;
    uint64_t added_ns;
}
@end
@implementation GLMDeferredPair
@end
@interface GLMPipelineJob : NSObject {
@public
    id<MTLDevice> device;
    NSData *key;
    GLMPipelineEntry *entry;
}
@end
@implementation GLMPipelineJob
@end
static NSMutableArray<GLMDeferredPair *> *deferred;
static uint64_t deferred_now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static const uint64_t DEFERRED_MAX_AGE_NS = 10ull * 1000000000ull;
static void retry_deferred(NSData *signature);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary<NSData *, GLMPipelineEntry *> *cache;
static NSMutableArray<NSData *> *order;
static NSMutableDictionary<NSData *, NSMutableArray<GLMPipelineRecipe *> *> *recipes;
static NSOperationQueue *compiler;
static unsigned pending;
static bool disabled;
static struct glm_pipeline_stats stats;

static void initialize(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        cache = [NSMutableDictionary dictionary];
        order = [NSMutableArray array];
        recipes = [NSMutableDictionary dictionary];
        deferred = [NSMutableArray array];
        compiler = [NSOperationQueue new];
        compiler.name = @"GLMetal pipeline prewarm";
        compiler.maxConcurrentOperationCount = 2;
        /* A first draw can be waiting for a running prediction. */
        compiler.qualityOfService = NSQualityOfServiceUserInitiated;
        disabled = getenv("GLMETAL_NO_PIPELINE_PREWARM") != NULL;
    });
}

static NSData *descriptor_key(MTLRenderPipelineDescriptor *d)
{
    /* Fixed-size integers avoid structure padding and native enum widths.
       These are all fields assigned by pipeline_for, plus full vertex fetch
       descriptors. Function addresses remain alive in cached descriptors. */
    uint64_t v[320] = {0}; unsigned n = 0;
#define VALUE(x) v[n++] = (uint64_t)(x)
    VALUE((uintptr_t)(__bridge void *)d.vertexFunction);
    VALUE((uintptr_t)(__bridge void *)d.fragmentFunction);
    VALUE(d.rasterSampleCount); VALUE(d.alphaToCoverageEnabled);
    VALUE(d.rasterizationEnabled); VALUE(d.inputPrimitiveTopology);
    VALUE(d.depthAttachmentPixelFormat); VALUE(d.stencilAttachmentPixelFormat);
    VALUE(d.maxTessellationFactor); VALUE(d.tessellationFactorFormat);
    VALUE(d.tessellationFactorStepFunction); VALUE(d.tessellationControlPointIndexType);
    VALUE(d.tessellationPartitionMode); VALUE(d.tessellationOutputWindingOrder);
    for (unsigned i = 0; i < 8; ++i) {
        MTLRenderPipelineColorAttachmentDescriptor *c = d.colorAttachments[i];
        VALUE(c.pixelFormat); VALUE(c.writeMask); VALUE(c.blendingEnabled);
        VALUE(c.sourceRGBBlendFactor); VALUE(c.destinationRGBBlendFactor);
        VALUE(c.sourceAlphaBlendFactor); VALUE(c.destinationAlphaBlendFactor);
        VALUE(c.rgbBlendOperation); VALUE(c.alphaBlendOperation);
    }
    VALUE(d.vertexDescriptor != nil);
    if (d.vertexDescriptor) for (unsigned i = 0; i < 31; ++i) {
        MTLVertexAttributeDescriptor *a = d.vertexDescriptor.attributes[i];
        MTLVertexBufferLayoutDescriptor *b = d.vertexDescriptor.layouts[i];
        VALUE(a.format); VALUE(a.offset); VALUE(a.bufferIndex);
        VALUE(b.stride); VALUE(b.stepFunction); VALUE(b.stepRate);
    }
#undef VALUE
    return [NSData dataWithBytes:v length:n * sizeof(*v)];
}

static NSData *input_signature(id<MTLFunction> vertex)
{
    uint64_t inputs[31] = {0};
    for (MTLVertexAttribute *a in vertex.vertexAttributes)
        if (a.active && a.attributeIndex < 31) inputs[a.attributeIndex] = a.attributeType + 1;
    return [NSData dataWithBytes:inputs length:sizeof inputs];
}

/* All cache/recipe bookkeeping is under lock, never native compilation. */
static void trim_cache(void)
{
    while (cache.count > CACHE_LIMIT) {
        NSUInteger i;
        for (i = 0; i < order.count; ++i) if (cache[order[i]]->done) break;
        if (i == order.count) break;
        [cache removeObjectForKey:order[i]]; [order removeObjectAtIndex:i];
    }
}

static void compile_entry(id<MTLDevice> device, NSData *key, GLMPipelineEntry *entry)
{
    NSError *error = nil;
    id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:entry->descriptor error:&error];
    pthread_mutex_lock(&lock);
    entry->state = state; entry->error = error; entry->done = true;
    if (entry->predicted) --pending;
    if (!state && cache[key] == entry) {
        [cache removeObjectForKey:key]; [order removeObject:key];
    }
    trim_cache();
    pthread_mutex_unlock(&lock);
    dispatch_group_leave(entry->group);
}

static void learn_recipe(MTLRenderPipelineDescriptor *d)
{
    if (!d.vertexFunction || !d.fragmentFunction || !d.rasterizationEnabled) return;
    NSData *signature = input_signature(d.vertexFunction);
    MTLRenderPipelineDescriptor *copy = [d copy];
    copy.vertexFunction = nil; copy.fragmentFunction = nil;
    NSData *key = descriptor_key(copy);
    pthread_mutex_lock(&lock);
    NSMutableArray<GLMPipelineRecipe *> *list = recipes[signature];
    if (!list) {
        if (recipes.count >= SIGNATURE_LIMIT) { pthread_mutex_unlock(&lock); return; }
        recipes[signature] = list = [NSMutableArray array];
    }
    for (GLMPipelineRecipe *r in list) if ([r->key isEqual:key]) {
        ++r->uses; pthread_mutex_unlock(&lock); retry_deferred(signature); return;
    }
    if (list.count == RECIPE_LIMIT) {
        /* A bounded frequent-item counter can adapt after an application
           changes render targets; the first configurations cannot pin the
           prediction forever. Only positive counts are prediction candidates. */
        for (GLMPipelineRecipe *r in list) if (r->uses) --r->uses;
        for (NSUInteger i = 0; i < list.count; ++i) if (!list[i]->uses) {
            [list removeObjectAtIndex:i]; break;
        }
    }
    if (list.count < RECIPE_LIMIT) {
        GLMPipelineRecipe *r = [GLMPipelineRecipe new];
        r->descriptor = copy; r->key = key; r->uses = 1; [list addObject:r];
    }
    pthread_mutex_unlock(&lock);
    retry_deferred(signature);
}

id<MTLRenderPipelineState> glm_pipeline_acquire(id<MTLDevice> device,
    MTLRenderPipelineDescriptor *descriptor, bool learn, NSError **error)
{
    initialize();
    if (disabled) return [device newRenderPipelineStateWithDescriptor:descriptor error:error];
    NSData *key = descriptor_key(descriptor);
    pthread_mutex_lock(&lock);
    /* A demand consumes this pair even if its actual descriptor differs from
       the eventual recipe. Never spend deferred work on it afterwards. */
    for (NSUInteger i = deferred.count; i-- > 0;) {
        GLMDeferredPair *pair = deferred[i];
        if (pair->device == device && pair->vertex == descriptor.vertexFunction &&
            pair->fragment == descriptor.fragmentFunction) [deferred removeObjectAtIndex:i];
    }
    GLMPipelineEntry *entry = cache[key];
    bool start = !entry || !entry->started;
    if (!entry) {
        entry = [GLMPipelineEntry new]; entry->descriptor = [descriptor copy];
        entry->group = dispatch_group_create(); dispatch_group_enter(entry->group);
        cache[key] = entry; [order addObject:key]; trim_cache();
        ++stats.demanded;
    } else {
        if (entry->predicted) ++stats.reused;
        if (entry->started && !entry->done) ++stats.waited;
    }
    if (start) entry->started = true;
    pthread_mutex_unlock(&lock);
    if (start) compile_entry(device, key, entry);
    else dispatch_group_wait(entry->group, DISPATCH_TIME_FOREVER);
    if (error) *error = entry->error;
    if (learn && entry->state) learn_recipe(descriptor);
    return entry->state;
}

/* Helpers below run under lock; scheduling and compilation run outside it. */
static GLMPipelineRecipe *confident_recipe(NSData *signature)
{
    GLMPipelineRecipe *best = nil;
    for (GLMPipelineRecipe *r in recipes[signature]) if (r->uses && (!best || r->uses > best->uses)) best = r;
    unsigned second = 0;
    for (GLMPipelineRecipe *r in recipes[signature]) if (r != best && r->uses > second) second = r->uses;
    return best && best->uses >= 2 && best->uses / 2 >= second ? best : nil;
}
static void expire_deferred(uint64_t now)
{
    for (NSUInteger i = deferred.count; i-- > 0;)
        if (now - deferred[i]->added_ns >= DEFERRED_MAX_AGE_NS) [deferred removeObjectAtIndex:i];
}
static GLMPipelineJob *predict_pair(id<MTLDevice> device, id<MTLFunction> vertex,
                                   id<MTLFunction> fragment, GLMPipelineRecipe *best)
{
    MTLRenderPipelineDescriptor *d = [best->descriptor copy];
    d.vertexFunction = vertex; d.fragmentFunction = fragment;
    NSData *key = descriptor_key(d);
    if (cache[key]) return nil;
    if (pending >= PENDING_LIMIT) { ++stats.dropped; return nil; }
    GLMPipelineEntry *entry = [GLMPipelineEntry new]; entry->descriptor = d; entry->predicted = true;
    entry->group = dispatch_group_create(); dispatch_group_enter(entry->group);
    cache[key] = entry; [order addObject:key]; ++pending; ++stats.predicted; trim_cache();
    GLMPipelineJob *job = [GLMPipelineJob new];
    job->device = device; job->key = key; job->entry = entry;
    return job;
}
static void schedule_prediction(GLMPipelineJob *job)
{
    if (!job) return;
    [compiler addOperationWithBlock:^{
        @autoreleasepool {
            pthread_mutex_lock(&lock);
            bool start = !job->entry->started; if (start) job->entry->started = true;
            pthread_mutex_unlock(&lock);
            if (start) compile_entry(job->device, job->key, job->entry);
        }
    }];
}
static void retry_deferred(NSData *signature)
{
    NSMutableArray<GLMPipelineJob *> *jobs = [NSMutableArray array];
    pthread_mutex_lock(&lock);
    expire_deferred(deferred_now_ns());
    GLMPipelineRecipe *best = confident_recipe(signature);
    unsigned attempts = 0;
    if (best) for (NSUInteger i = 0; i < deferred.count && attempts < RETRY_LIMIT;) {
        GLMDeferredPair *pair = deferred[i];
        if (![pair->signature isEqual:signature]) { ++i; continue; }
        if (pending >= PENDING_LIMIT) break;
        ++attempts;
        GLMPipelineJob *job = predict_pair(pair->device, pair->vertex, pair->fragment, best);
        if (job) [jobs addObject:job];
        [deferred removeObjectAtIndex:i];
    }
    pthread_mutex_unlock(&lock);
    for (GLMPipelineJob *job in jobs) schedule_prediction(job);
}
void glm_pipeline_prewarm(id<MTLDevice> device, id<MTLFunction> vertex, id<MTLFunction> fragment)
{
    initialize();
    if (disabled || !vertex || !fragment) return;
    NSData *signature = input_signature(vertex);
    pthread_mutex_lock(&lock);
    uint64_t now = deferred_now_ns();
    expire_deferred(now);
    GLMPipelineRecipe *best = confident_recipe(signature);
    if (!best) {
        for (GLMDeferredPair *pair in deferred)
            if (pair->device == device && pair->vertex == vertex && pair->fragment == fragment) {
                pthread_mutex_unlock(&lock); return;
            }
        if (deferred.count == DEFERRED_LIMIT) [deferred removeObjectAtIndex:0];
        GLMDeferredPair *pair = [GLMDeferredPair new];
        pair->device = device; pair->vertex = vertex; pair->fragment = fragment;
        pair->signature = signature; pair->added_ns = now;
        [deferred addObject:pair];
        pthread_mutex_unlock(&lock); return;
    }
    GLMPipelineJob *job = predict_pair(device, vertex, fragment, best);
    pthread_mutex_unlock(&lock);
    schedule_prediction(job);
}

struct glm_pipeline_stats glm_pipeline_statistics(void)
{
    pthread_mutex_lock(&lock); struct glm_pipeline_stats result = stats; pthread_mutex_unlock(&lock);
    return result;
}
