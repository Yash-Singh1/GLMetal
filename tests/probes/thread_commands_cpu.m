/* CPU-only command-stream regression. Build with AddressSanitizer:
   xcrun clang -fobjc-arc -fsanitize=address -g -O1 -Isrc -Ibuild/gen
     tests/probes/thread_commands_cpu.m -framework Foundation -o build/thread_commands_cpu
   No GL context or Metal device is created. */
#import <Foundation/Foundation.h>

#include "glm_internal.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static __thread struct glm_context *current_context;
void glm_set_current(struct glm_context *ctx) { current_context = ctx; }
struct glm_context *glm_current(void) { return current_context; }
void glm_log(const char *format, ...) { (void)format; }

static unsigned allocations, releases;
static void *counted_malloc(size_t bytes)
{
    void *memory = malloc(bytes);
    assert(memory);
    __atomic_add_fetch(&allocations, 1, __ATOMIC_RELAXED);
    return memory;
}
static void counted_free(void *memory)
{
    if (memory) __atomic_add_fetch(&releases, 1, __ATOMIC_RELAXED);
    free(memory);
}

/* Exercise the real allocator and worker with only their CPU dependencies. */
#define malloc counted_malloc
#define free counted_free
#include "../../src/thread.m"
#undef malloc
#undef free

struct payload {
    size_t bytes;
    unsigned sequence;
    unsigned pattern;
    unsigned char data[];
};
static struct glm_context context;
static unsigned executed;

static void execute_payload(const void *memory)
{
    const struct payload *payload = memory;
    assert(glm_current() == &context);
    assert(payload->sequence == executed);
    for (size_t i = 0; i < payload->bytes - sizeof *payload; ++i)
        assert(payload->data[i] == payload->pattern);
    ++executed;
    glm_thread_state_executed(&context);
}

static void enqueue(size_t bytes, unsigned sequence)
{
    assert(bytes >= sizeof(struct payload));
    glm_thread_state_recorded(&context);
    struct payload *payload = glm_thread_alloc(&context, bytes, execute_payload);
    *payload = (struct payload){bytes, sequence, (unsigned char)(sequence + 17)};
    memset(payload->data, payload->pattern, bytes - sizeof *payload);
}

static void check_request(const void *memory)
{
    assert(executed == *(const unsigned *)memory);
}

int main(void)
{
    @autoreleasepool {
        unsetenv("GLMETAL_THREADED");
        glm_thread_start(&context);
        assert(context.thread);
        size_t boundary = BATCH_BYTES - sizeof(struct command_header);
        enqueue(boundary, 0);
        enqueue(boundary + 1, 1);
        uint64_t mark = glm_thread_recorded(&context);
        enqueue(sizeof(struct payload) + 8, 2);
        unsigned request_count = 2;
        glm_thread_request(&context, mark, check_request, &request_count);

        /* Reuse every batch repeatedly, interleaving larger heap payloads. */
        for (unsigned i = 3; i < 35; ++i)
            enqueue(i % 8 == 0 ? 2 * BATCH_BYTES + 37 : boundary, i);
        glm_thread_sync(&context);
        assert(executed == 35);
        assert(glm_thread_recorded(&context) == 35);
        assert(glm_thread_executed(&context) == 35);
        assert(glm_thread_state_settled(&context));
        enqueue(3 * BATCH_BYTES + 19, 35);
        glm_thread_stop(&context);
        assert(!context.thread && executed == 36);
        assert(allocations == BATCHES + 6);
        /* thread itself is calloc'd; counted_free also releases that object. */
        assert(releases == allocations + 1);
        puts("Command payload bounds, order, batch reuse and shutdown cleanup passed");
    }
}
