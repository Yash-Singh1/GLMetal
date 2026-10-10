/* CPU-only production marshal query path. A worker rendezvous can encounter
 * unrelated work even though the query's recorded end has already executed. */
#include "../../src/marshal/marshal_custom.c"
#include <assert.h>
#include <stdio.h>
static struct glm_context context;
static uint64_t executed, recorded, value;
static unsigned requests, syncs, submits, unrelated_work, snapshots;
static bool ready, valid=true, active, unrelated;
struct glm_context *glm_current(void) { return &context; }
uint64_t glm_thread_recorded(struct glm_context *ctx) { return recorded; }
uint64_t glm_thread_executed(struct glm_context *ctx) { return executed; }
void glm_thread_submit(struct glm_context *ctx) { ++submits; }
void glm_thread_sync_named(struct glm_context *ctx, const char *entry) { ++syncs; }
void glm_thread_request(struct glm_context *ctx,uint64_t after,glm_exec_fn fn,const void *arg)
{
    ++requests;
    if(unrelated) ++unrelated_work;
    assert(executed>=after);fn(arg);
}
bool glm_query_completed_value(struct glm_context *ctx,GLuint name,uint64_t *out)
{
    ++snapshots;
    if(!valid||active||!ready) return false;
    *out=value;return true;
}
void glm_impl_glGetQueryObjectuiv(GLuint name,GLenum pname,GLuint *out)
{
    if(!valid||active || (pname!=GL_QUERY_RESULT && pname!=GL_QUERY_RESULT_AVAILABLE)) return;
    if(pname==GL_QUERY_RESULT_AVAILABLE) *out=ready;
    else { assert(ready);*out=value>UINT32_MAX?UINT32_MAX:(GLuint)value; }
}
void glm_impl_glGetQueryObjectiv(GLuint name,GLenum pname,GLint *out)
{
    if(!valid||active || (pname!=GL_QUERY_RESULT && pname!=GL_QUERY_RESULT_AVAILABLE)) return;
    if(pname==GL_QUERY_RESULT_AVAILABLE) *out=ready;
    else { assert(ready);*out=value>INT32_MAX?INT32_MAX:(GLint)value; }
}
static void ended(GLuint name)
{
    glm_shadow_glBeginQuery(&context,GL_SAMPLES_PASSED,name);
    glm_shadow_glEndQuery(&context,GL_SAMPLES_PASSED);
    executed=++recorded;
}
int main(void)
{
    context.thread=(struct glm_thread *)(uintptr_t)1;
    GLuint name=7,out=99;GLint signed_out=99;
    ended(name);ready=false;
    /* An unexecuted end submits and answers false without a rendezvous. */
    executed=0;glGetQueryObjectuivARB(name,GL_QUERY_RESULT_AVAILABLE,&out);
    assert(!out&&!requests&&submits==1);
    executed=recorded;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT_AVAILABLE,&out);
    assert(!out&&requests==1&&!shadow(&context)->query_ends[name].completed);
    /* First successful poll is still a real worker query; exact zero counts. */
    ready=true;value=0;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT_AVAILABLE,&out);
    assert(out==1&&requests==2);
    unrelated=true;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT,&out);
    assert(out==0&&requests==2&&!unrelated_work);
    glGetQueryObjectiv(name,GL_QUERY_RESULT_AVAILABLE,&signed_out);
    assert(signed_out==1&&requests==2);
    /* Preserve raw64 bits and each public32 saturation rule. */
    ended(name);value=UINT64_MAX;unrelated=false;
    glGetQueryObjectiv(name,GL_QUERY_RESULT,&signed_out);
    assert(signed_out==INT32_MAX&&requests==3);
    assert(shadow(&context)->query_ends[name].completed_value==UINT64_MAX);
    unrelated=true;glGetQueryObjectuiv(name,GL_QUERY_RESULT,&out);
    assert(out==UINT32_MAX&&requests==3&&!unrelated_work);
    /* Unsupported getter pnames delegate and cannot publish any snapshot. */
    unsigned before_snapshots=snapshots;
    out=123;glGetQueryObjectuiv(name,GL_TEXTURE_2D,&out);
    assert(out==123&&snapshots==before_snapshots);
    /* Delete, name reuse and every mutation must discard completion. */
    glm_shadow_glDeleteQueriesARB(&context,1,&name);valid=false;out=123;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT,&out);
    assert(out==123&&syncs==2);
    valid=true;ended(name);value=17;unrelated=false;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT_AVAILABLE,&out);
    assert(out&&shadow(&context)->query_ends[name].completed);
    glm_shadow_glQueryCounter(&context,name,GL_TIMESTAMP);
    assert(!shadow(&context)->query_ends[name].mark);
    ended(name);glGetQueryObjectuiv(name,GL_QUERY_RESULT,&out);
    glm_shadow_glBeginQueryIndexed(&context,GL_PRIMITIVES_GENERATED,1,name);
    assert(!shadow(&context)->query_ends[name].mark);
    ended(name);glGetQueryObjectuiv(name,GL_QUERY_RESULT,&out);
    glm_shadow_glBeginQueryARB(&context,GL_SAMPLES_PASSED,name);active=true;out=456;
    glGetQueryObjectuivARB(name,GL_QUERY_RESULT,&out);
    assert(out==456&&!shadow(&context)->query_ends[name].completed);
    active=false;glm_shadow_glEndQueryIndexed(&context,GL_SAMPLES_PASSED,0);
    assert(!shadow(&context)->active_queries[0]);
    ended(name);glGetQueryObjectuiv(name,GL_QUERY_RESULT,&out);
    assert(shadow(&context)->query_ends[name].completed);
    shadow_queries_invalidate_all(shadow(&context));
    assert(!shadow(&context)->query_ends[name].mark&&!shadow(&context)->query_ends[name].completed);
    printf("query-shadow CPU PASS: second completed getter skipped worker request and unrelated work; %u requests\n",requests);
    free(shadow(&context)->query_ends);free(context.shadow);
    return 0;
}
