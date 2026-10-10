/* Offline preparation of exact GLMetal program requests. No GL context,
 * command queue, rendering, or GPU submissions. Runtime pipeline state still
 * requires the draw's attachment, blend and vertex-fetch descriptors.
 * Link against the same frontend objects as glmetal-compiler, so production
 * validation and program-cache keys include the current compiler stamp. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../src/shader_compiler.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !__has_feature(objc_arc)
#error "glmetal-precompile requires -fobjc-arc for bounded library and request ownership"
#endif

extern "C" const char glm_compiler_build[];
extern "C" void *glm_remote_decode_request(const unsigned char *, size_t, glm_compile_request *);
extern "C" void glm_remote_free_request(void *);
extern "C" void glm_remote_encode_request(const glm_compile_request *, std::vector<unsigned char> *);
extern "C" bool glm_compile_cache_load(const glm_compile_request *, glm_compile_result *);

static double milliseconds()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void emit(NSDictionary *record)
{
    NSData *data = [NSJSONSerialization dataWithJSONObject:record options:0 error:nil];
    fwrite(data.bytes, 1, data.length, stdout); fputc('\n', stdout); fflush(stdout);
}
static NSString *text(id value, NSString *field)
{
    if (![value isKindOfClass:NSString.class])
        throw std::runtime_error(std::string(field.UTF8String) + " must be a string");
    if (strlen([value UTF8String]) != [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding])
        throw std::runtime_error(std::string(field.UTF8String) + " contains a NUL byte");
    return value;
}
static uint32_t integer(id value, NSString *field, uint32_t fallback = 0)
{
    if (!value) return fallback;
    if (![value isKindOfClass:NSNumber.class] || [value doubleValue] < 0 ||
        [value doubleValue] > UINT32_MAX || [value doubleValue] != [value unsignedLongLongValue])
        throw std::runtime_error(std::string(field.UTF8String) + " must be a nonnegative 32-bit integer");
    return [value unsignedIntValue];
}
static NSArray *array(id value, NSString *field)
{
    if (!value) return @[];
    if (![value isKindOfClass:NSArray.class] || [value count] > 4096)
        throw std::runtime_error(std::string(field.UTF8String) + " must be an array of at most 4096 entries");
    return value;
}
static void bindings(id value, NSString *field, std::vector<glm_name_location> &out)
{
    for (id item in array(value, field)) {
        if (![item isKindOfClass:NSDictionary.class])
            throw std::runtime_error(std::string(field.UTF8String) + " entries need name and location");
        NSString *name = text(item[@"name"], field);
        uint32_t location = integer(item[@"location"], field);
        if (!item[@"location"] || location > INT_MAX)
            throw std::runtime_error("binding location is missing or outside the signed integer range");
        out.push_back({name.UTF8String, (int)location});
    }
}
static NSString *source(NSString *path, NSString *directory)
{
    if (!path.isAbsolutePath) path = [directory stringByAppendingPathComponent:path];
    NSError *error = nil;
    NSString *value = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
    if (!value) throw std::runtime_error(std::string("cannot read ") + path.UTF8String + ": " + error.localizedDescription.UTF8String);
    return text(value, @"shader source");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: glmetal-precompile requests.json | --request-dir DIR [--dry-run] [--metal-libraries] [--limit N] [--pause-ms N]\n");
        return 2;
    }
    bool dry_run = false, metal = false;
    unsigned limit = UINT_MAX, pause_ms = 25;
    NSString *request_directory = nil;
    bool binary_input = !strcmp(argv[1], "--request-dir");
    for (int i = binary_input ? 1 : 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--request-dir") && i + 1 < argc) request_directory = @(argv[++i]);
        else if (!strcmp(argv[i], "--dry-run")) dry_run = true;
        else if (!strcmp(argv[i], "--metal-libraries")) metal = true;
        else if ((!strcmp(argv[i], "--limit") || !strcmp(argv[i], "--pause-ms")) && i + 1 < argc) {
            bool is_limit = !strcmp(argv[i], "--limit"); char *end = nullptr;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (!*argv[i] || *end || value > UINT_MAX) { fprintf(stderr,"invalid numeric option\n");return 2; }
            if (is_limit) limit = (unsigned)value; else pause_ms = (unsigned)value;
        } else { fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]); return 2; }
    }
    /* This native utility performs its own serial work, never starts a helper. */
    setenv("GLMETAL_NO_COMPILER_HELPER", "1", 1);
    @autoreleasepool {
        NSError *error = nil;
        NSString *manifest = [@(argv[1]) stringByStandardizingPath];
        id records = nil;
        if (binary_input) {
            if (!request_directory) { fprintf(stderr,"--request-dir requires a directory\n"); return 2; }
            NSArray *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:request_directory error:&error];
            if (!files) { fprintf(stderr,"request directory: %s\n",error.localizedDescription.UTF8String);return 2; }
            NSMutableArray *binary_records = [NSMutableArray array];
            for (NSString *name in [files sortedArrayUsingSelector:@selector(compare:)])
                if ([name.pathExtension isEqualToString:@"request"])
                    [binary_records addObject:@{@"binary_request":[request_directory stringByAppendingPathComponent:name], @"id":name}];
            records = binary_records;
        } else {
            if (request_directory) { fprintf(stderr,"choose either a JSON manifest or --request-dir\n");return 2; }
            NSData *input = [NSData dataWithContentsOfFile:manifest options:0 error:&error];
            records = input ? [NSJSONSerialization JSONObjectWithData:input options:0 error:&error] : nil;
            if (![records isKindOfClass:NSArray.class]) {
                fprintf(stderr, "manifest must be a JSON array: %s\n", error ? error.localizedDescription.UTF8String : "wrong type"); return 2;
            }
        }
        if (!dry_run && getenv("GLMETAL_NO_SHADER_CACHE")) {
            fprintf(stderr,"GLMETAL_NO_SHADER_CACHE disables persistent preparation; unset it before running\n");return 2;
        }
        NSString *directory = manifest.stringByDeletingLastPathComponent;
        id<MTLDevice> device = nil;
        if (metal && !dry_run) {
            device = MTLCreateSystemDefaultDevice();
            if (!device) { fprintf(stderr, "no Metal device\n"); return 2; }
        }
        emit(@{@"kind":@"configuration", @"compiler_build":@(glm_compiler_build), @"dry_run":@(dry_run),
            @"metal_libraries":@(metal), @"pause_ms":@(pause_ms)});
        unsigned completed = 0, failures = 0, hits = 0;
        double began = milliseconds();
        for (id value in records) {
            if (completed + failures >= limit) break;
            @autoreleasepool {
                glm_compile_result result = {};
                void *request_storage = nullptr;
                double start = milliseconds();
                try {
                    if (![value isKindOfClass:NSDictionary.class]) throw std::runtime_error("request must be an object");
                    NSDictionary *record = value;
                    glm_compile_request request = {};
                    /* The request stores raw UTF8String pointers. Keep their
                     * owners alive through validation, linking and Metal work. */
                    __attribute__((objc_precise_lifetime)) NSString *vs = nil, *fs = nil;
                    std::vector<glm_name_location> attributes, outputs;
                    std::vector<const char *> feedback;
                    if (binary_input) {
                        NSData *payload = [NSData dataWithContentsOfFile:record[@"binary_request"]
                            options:NSDataReadingMappedIfSafe error:&error];
                        if (!payload || !payload.length || payload.length > 64 * 1024 * 1024)
                            throw std::runtime_error("binary request is missing, empty, or larger than 64 MiB");
                        request_storage = glm_remote_decode_request((const unsigned char *)payload.bytes, payload.length, &request);
                        /* The production decoder has no success flag. An exact
                         * round trip rejects truncation, trailing bytes and
                         * invalid counts without changing production code. */
                        std::vector<unsigned char> canonical;
                        glm_remote_encode_request(&request, &canonical);
                        if (canonical.size() != payload.length || memcmp(canonical.data(), payload.bytes, payload.length))
                            throw std::runtime_error("binary request is malformed or uses an incompatible protocol");
                    } else {
                        NSSet *allowed = [NSSet setWithArray:@[@"vertex",@"fragment",@"attributes",@"frag_outputs",@"feedback_varyings",
                            @"feedback_interleaved",@"tess_output_vertices",@"uint_inputs",@"int_inputs",@"border_samplers",@"id"]];
                        for (NSString *key in record) if (![allowed containsObject:key])
                            throw std::runtime_error(std::string("unknown request field: ") + key.UTF8String);
                        vs = source(text(record[@"vertex"], @"vertex"), directory);
                        fs = source(text(record[@"fragment"], @"fragment"), directory);
                        bindings(record[@"attributes"], @"attributes", attributes);
                        bindings(record[@"frag_outputs"], @"frag_outputs", outputs);
                        for (id name in array(record[@"feedback_varyings"], @"feedback_varyings"))
                            feedback.push_back(text(name, @"feedback_varyings").UTF8String);
                        request.sources[GLM_STAGE_VERTEX] = vs.UTF8String; request.sources[GLM_STAGE_FRAGMENT] = fs.UTF8String;
                        request.attributes = attributes.data(); request.attribute_count = (int)attributes.size();
                        request.frag_outputs = outputs.data(); request.frag_output_count = (int)outputs.size();
                        request.feedback_varyings = feedback.data(); request.feedback_count = (int)feedback.size();
                        /* New GL programs default to GL_INTERLEAVED_ATTRIBS. This bit
                         * remains part of the cache key even with no feedback names. */
                        id interleaved = record[@"feedback_interleaved"];
                        if (interleaved && (![interleaved isKindOfClass:NSNumber.class] ||
                            CFGetTypeID((__bridge CFTypeRef)interleaved) != CFBooleanGetTypeID()))
                            throw std::runtime_error("feedback_interleaved must be a JSON boolean");
                        request.feedback_interleaved = interleaved ? [interleaved boolValue] : true;
                        uint32_t vertices = integer(record[@"tess_output_vertices"], @"tess_output_vertices");
                        if (vertices > INT_MAX) throw std::runtime_error("tess_output_vertices exceeds the signed integer range");
                        request.tess_output_vertices = (int)vertices;
                        request.uint_inputs = integer(record[@"uint_inputs"], @"uint_inputs");
                        request.int_inputs = integer(record[@"int_inputs"], @"int_inputs");
                        NSArray *borders = array(record[@"border_samplers"], @"border_samplers");
                        if (borders.count > 32) throw std::runtime_error("border_samplers has more than 32 slots");
                        for (NSUInteger i = 0; i < borders.count; ++i)
                            if (borders[i] != NSNull.null) request.border_samplers[i] = text(borders[i], @"border_samplers").UTF8String;
                    }
                    if (!request.sources[0] && !request.sources[1])
                        throw std::runtime_error("request has no vertex or fragment source");
                    for (int stage = 2; stage < GLM_STAGE_COUNT; ++stage)
                        if (request.sources[stage]) throw std::runtime_error("offline utility currently supports vertex/fragment programs only");
                    bool hit = false; unsigned libraries = 0;
                    double validation_ms = 0, frontend_ms = 0, metal_ms = 0;
                    if (!dry_run) {
                        double phase = milliseconds();
                        for (int stage = 0; stage < 2; ++stage) {
                            if (!request.sources[stage]) continue;
                            char *log = nullptr;
                            bool ok = glm_shader_check((glm_stage)stage, request.sources[stage], &log);
                            std::string reason = log ? log : "validation failed"; free(log);
                            if (!ok) throw std::runtime_error(reason);
                        }
                        validation_ms = milliseconds() - phase; phase = milliseconds();
                        hit = glm_compile_cache_load(&request, &result);
                        if (!hit) glm_program_compile(&request, &result);
                        frontend_ms = milliseconds() - phase;
                        if (!result.ok) throw std::runtime_error(result.log ? result.log : "program compile failed");
                        if (result.gs || result.tess) throw std::runtime_error("geometry/tessellation results cannot use the persistent program cache");
                        if (metal) {
                            phase = milliseconds();
                            const char *sources[] = {result.msl[0], result.msl[1], result.msl_capture};
                            MTLCompileOptions *options = [MTLCompileOptions new];
                            options.fastMathEnabled = NO; options.languageVersion = MTLLanguageVersion2_3;
                            for (const char *msl : sources) {
                                if (!msl) continue;
                                id<MTLLibrary> library = [device newLibraryWithSource:@(msl) options:options error:&error];
                                if (!library || ![library newFunctionWithName:@"main0"])
                                    throw std::runtime_error(error ? error.localizedDescription.UTF8String : "missing Metal main0 function");
                                ++libraries;
                            }
                            metal_ms = milliseconds() - phase;
                        }
                    }
                    emit(@{@"kind":@"request", @"index":@(completed+failures), @"id":record[@"id"] ?: NSNull.null,
                        @"ok":@YES, @"cache_hit":@(hit), @"feedback_interleaved":@(request.feedback_interleaved),
                        @"attribute_bindings":@(request.attribute_count), @"output_bindings":@(request.frag_output_count),
                        @"validation_ms":@(validation_ms), @"frontend_ms":@(frontend_ms),
                        @"metal_ms":@(metal_ms), @"libraries":@(libraries), @"elapsed_ms":@(milliseconds()-start)});
                    ++completed; if (hit) ++hits;
                } catch (const std::exception &exception) {
                    emit(@{@"kind":@"request", @"index":@(completed+failures), @"ok":@NO, @"error":@(exception.what())});
                    ++failures;
                }
                glm_compile_result_free(&result);
                if (request_storage) glm_remote_free_request(request_storage);
            }
            if (!dry_run && pause_ms) std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
        }
        emit(@{@"kind":@"summary", @"completed":@(completed), @"failed":@(failures), @"cache_hits":@(hits), @"elapsed_ms":@(milliseconds()-began)});
        return failures ? 1 : 0;
    }
}
