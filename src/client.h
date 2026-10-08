#ifndef GLMETAL_CLIENT_H
#define GLMETAL_CLIENT_H
#include <stdbool.h>
#include <stddef.h>

/* Optional host integration. Configure before glmetal_initialize, from a
   single startup thread. The default uses Metal's own shared allocation.
   Custom allocations must be page aligned and release the entire requested
   range. Callbacks must remain valid until all contexts are destroyed. */
bool glmetal_set_shared_buffer_allocator(void *(*allocate)(size_t),
                                         void (*deallocate)(void *, size_t));
/* Explicitly prepare reusable command queues. No warmup runs by default.
   Call after initialization, before creating contexts. */
bool glmetal_prewarm_command_queues(unsigned queues, unsigned submissions);
#endif
