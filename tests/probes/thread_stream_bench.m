/* Completed CPU command-stream throughput, without a GL or Metal device. */
#import <Foundation/Foundation.h>
#include "glm_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static __thread struct glm_context *current;
void glm_set_current(struct glm_context *ctx){current=ctx;}
struct glm_context *glm_current(void){return current;}
void glm_log(const char *format,...){(void)format;}
#ifndef GLM_THREAD_SOURCE
#define GLM_THREAD_SOURCE "../../src/thread.m"
#endif
#include GLM_THREAD_SOURCE
static uint64_t consumed;
struct state { uint64_t serial; };
static void execute(const void *data){const struct state *state=data;assert(state->serial==consumed);++consumed;glm_thread_state_executed(current);}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){@autoreleasepool{
    struct glm_context ctx={0};glm_thread_start(&ctx);
    const uint64_t count=2000000;
    for(int round=0;round<3;++round){
        uint64_t first=consumed;double start=now();
        for(uint64_t i=0;i<count;++i){glm_thread_state_recorded(&ctx);struct state *s=glm_thread_alloc(&ctx,sizeof *s,execute);s->serial=first+i;}
        glm_thread_sync(&ctx);
        assert(consumed==first+count && glm_thread_recorded(&ctx)==consumed && glm_thread_executed(&ctx)==consumed && glm_thread_state_settled(&ctx));
        printf("command-stream round %d: %.2f ns/command including completion\n",round,(now()-start)*1e9/count);
    }
    glm_thread_stop(&ctx);
}}
