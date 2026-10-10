/* Immutable sampler reflection only. Texture units and sampler/texture objects
 * remain live draw state and must never enter this plan. */
#ifndef GLM_SAMPLER_PLAN_H
#define GLM_SAMPLER_PLAN_H
#include "shader_compiler.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { GLM_SAMPLER_PLAN_SLOTS = 32, GLM_SAMPLER_PLAN_CAPACITY = 128 };
struct glm_sampler_plan_item {
    int uniform_index, arb_unit, arb_target;
    bool arb;
};
struct glm_sampler_plan {
    const void *owner;
    const struct glm_uniform_info *uniforms;
    uint64_t link;
    int uniform_count, count;
    bool valid;
    struct glm_sampler_plan_item items[GLM_SAMPLER_PLAN_CAPACITY];
};
struct glm_sampler_plan_cache {
    struct glm_sampler_plan plans[GLM_SAMPLER_PLAN_SLOTS];
};
static inline bool glm_sampler_arb_name(const char *name, int *unit, int *target)
{
    /* Ordinary GLSL sampler names need no locale-aware scanf machinery. */
    return name && !strncmp(name, "glm_arb_tex", 11) &&
        sscanf(name, "glm_arb_tex%d_%d", unit, target) == 2;
}
static inline const struct glm_sampler_plan *glm_sampler_plan_get(struct glm_sampler_plan_cache *cache,
    const void *owner, uint64_t link, const struct glm_uniform_info *uniforms, int uniform_count)
{
    uintptr_t hash = ((uintptr_t)owner >> 4) ^ ((uintptr_t)uniforms >> 5) ^ (uintptr_t)(link * 31);
    struct glm_sampler_plan *plan = &cache->plans[hash % GLM_SAMPLER_PLAN_SLOTS];
    if (plan->valid && plan->owner == owner && plan->link == link &&
        plan->uniforms == uniforms && plan->uniform_count == uniform_count) return plan;
    plan->valid = false;
    plan->owner = owner; plan->link = link; plan->uniforms = uniforms;
    plan->uniform_count = uniform_count; plan->count = 0;
    for (int i = 0; i < uniform_count; ++i) {
        const struct glm_uniform_info *uniform = &uniforms[i];
        if (uniform->sampler_slot < 0 || uniform->offset >= 0) continue;
        if (plan->count == GLM_SAMPLER_PLAN_CAPACITY) return NULL;
        struct glm_sampler_plan_item *item = &plan->items[plan->count++];
        item->uniform_index = i;
        item->arb_unit = item->arb_target = 0;
        item->arb = glm_sampler_arb_name(uniform->name, &item->arb_unit, &item->arb_target);
    }
    plan->valid = true;
    return plan;
}
#endif
