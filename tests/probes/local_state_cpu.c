/* CPU mock command storage runs freshly generated wrappers and actual
   implementations. Queries and display-list callbacks retain production
   marshal ordering. No driver library or GL/Metal context is created. */
#include "glm_internal.h"
#include "marshal_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct glm_context context;
static struct { void *payload; glm_exec_fn exec; } commands[128], lists[128];
static unsigned command_count, list_count, syncs, shadow_calls, list_notes;
static int pending[3];
static bool execute_list;
static GLenum shadow_active;
static GLint texture_parameter;
struct glm_context *glm_current(void) { return &context; }
void glm_error(struct glm_context *ctx, GLenum error) { ctx->error = error; }
int glm_profile_rejected(struct glm_context *ctx, int core) { return 0; }
void *glm_thread_alloc(struct glm_context *ctx, size_t bytes, glm_exec_fn exec)
{
    assert(command_count < 128);
    void *payload = calloc(1, bytes);
    assert(payload);
    commands[command_count++] = (__typeof__(commands[0])){payload, exec};
    return payload;
}
void glm_thread_state_recorded(struct glm_context *ctx) { ++pending[0]; }
void glm_thread_state_executed(struct glm_context *ctx) { assert(pending[0] > 0); --pending[0]; }
int glm_thread_state_settled(struct glm_context *ctx) { return pending[0] == 0; }
void glm_thread_pending_add(struct glm_context *ctx, int which, int delta)
{ pending[which] += delta; assert(pending[which] >= 0); }
int glm_thread_pending_zero(struct glm_context *ctx, int which) { return pending[which] == 0; }
static void drain(void)
{
    for (unsigned i = 0; i < command_count; ++i) { commands[i].exec(commands[i].payload); free(commands[i].payload); }
    command_count = 0;
    assert(!pending[0] && !pending[1] && !pending[2]);
}
void glm_thread_sync_named(struct glm_context *ctx, const char *name) { ++syncs; drain(); }
void *glm_list_alloc(struct glm_context *ctx, size_t bytes, glm_exec_fn exec)
{
    assert(list_count < 128);
    void *payload = calloc(1, bytes);
    assert(payload);
    lists[list_count++] = (__typeof__(lists[0])){payload, exec};
    return payload;
}
int glm_list_executes(struct glm_context *ctx) { return execute_list; }
void glm_list_note_shadowed(struct glm_context *ctx) { ++list_notes; }
void glm_list_unsupported(struct glm_context *ctx, const char *name) { abort(); }
size_t glm_param_count(unsigned pname) { return 1; }
int glm_cheap_pname(unsigned pname) { return 0; }
void glm_shadow_glActiveTexture(struct glm_context *ctx, GLenum unit) { ++shadow_calls; shadow_active = unit; }
void glm_shadow_glActiveTextureARB(struct glm_context *ctx, GLenum unit) { ++shadow_calls; shadow_active = unit; }
void glm_shadow_glViewport(struct glm_context *ctx, GLint x, GLint y, GLsizei width, GLsizei height) { ++shadow_calls; }
void glm_shadow_glScissor(struct glm_context *ctx, GLint x, GLint y, GLsizei width, GLsizei height) { ++shadow_calls; }
void glm_shadow_glViewportArrayv(struct glm_context *ctx, GLuint first, GLsizei count, const GLfloat *v) { ++shadow_calls; }
void glm_shadow_glViewportIndexedf(struct glm_context *ctx, GLuint index, GLfloat x, GLfloat y, GLfloat w, GLfloat h) { ++shadow_calls; }
void glm_shadow_glScissorArrayv(struct glm_context *ctx, GLuint first, GLsizei count, const GLint *v) { ++shadow_calls; }
void glm_shadow_glScissorIndexed(struct glm_context *ctx, GLuint index, GLint x, GLint y, GLsizei w, GLsizei h) { ++shadow_calls; }
void glm_shadow_glTexParameteri(struct glm_context *ctx, GLenum target, GLenum pname, GLint param) { ++shadow_calls; }
void glm_shadow_glGenerateMipmap(struct glm_context *ctx, GLenum target) { ++shadow_calls; }

#include "glm_impl_rename.h"
/* PRODUCTION_IMPLEMENTATIONS */
/* IMPLEMENTATION_UNRENAME */

void glm_impl_glTexParameteri(GLenum target, GLenum pname, GLint value) { texture_parameter = value; }
void glm_impl_glGenerateMipmap(GLenum target) {}
void glm_impl_glGetFloatv(GLenum pname, GLfloat *out)
{
    switch (pname) {
    case GL_ACTIVE_TEXTURE: *out = context.state.active_texture; return;
    case GL_DEPTH_FUNC: *out = context.state.depth_func; return;
    case GL_DEPTH_WRITEMASK: *out = context.state.depth_mask; return;
    case GL_VIEWPORT:
        for (int i = 0; i < 4; ++i) out[i] = context.state.viewport[i];
        return;
    default: abort();
    }
}
#include "marshal.c"

static uint64_t serial(void) { return __atomic_load_n(&context.state_serial, __ATOMIC_RELAXED); }
static void setters(void)
{
    glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
    glViewport(1, 2, 640, 480); glScissor(3, 4, 320, 240); glActiveTexture(GL_TEXTURE0);
}
static void direct_tests(void)
{
    setters();
    uint64_t previous = serial();
    assert(previous == 6);
    unsigned shadows = shadow_calls;
    for (int i = 0; i < 1000; ++i) setters();
    assert(serial() == previous && shadow_calls > shadows && command_count == 0);
    glActiveTextureARB(GL_TEXTURE0 + 3);
    assert(serial() == previous && context.state.active_texture == GL_TEXTURE0 + 3 && shadow_active == GL_TEXTURE0 + 3);
    glDepthMask(2); glDepthMask(255);
    assert(serial() == previous && context.state.depth_mask);
    glDepthMask(GL_FALSE); assert(serial() == ++previous);
    glDepthFunc(GL_GREATER); assert(serial() == ++previous);
    glCullFace(GL_FRONT); assert(serial() == ++previous);
    glFrontFace(GL_CW); assert(serial() == ++previous);
    glDepthFunc(0); assert(context.error == GL_INVALID_ENUM && serial() == previous); context.error = 0;
    glCullFace(0); assert(context.error == GL_INVALID_ENUM && serial() == previous); context.error = 0;
    glFrontFace(0); assert(context.error == GL_INVALID_ENUM && serial() == previous); context.error = 0;
    glActiveTexture(GL_TEXTURE0 + GLM_MAX_TEXTURE_UNITS);
    assert(context.error == GL_INVALID_ENUM && serial() == previous); context.error = 0;
    glViewport(1, 2, -1, 4); assert(context.error == GL_INVALID_VALUE && serial() == previous); context.error = 0;
    glScissor(1, 2, 4, -1); assert(context.error == GL_INVALID_VALUE && serial() == previous); context.error = 0;
    glViewportIndexedf(5, 7, 8, 100, 90);
    previous = serial(); // Indexed variants retain their conservative generated bump.
    glViewport(1, 2, 640, 480); assert(serial() == ++previous);
    double values[4]; assert(glm_indexed_state_query(&context, GL_VIEWPORT, 5, values) == 4);
    assert(values[0] == 1 && values[2] == 640);
    glScissorIndexed(6, 9, 10, 30, 40); previous = serial();
    glScissor(3, 4, 320, 240); assert(serial() == ++previous);
    assert(glm_indexed_state_query(&context, GL_SCISSOR_BOX, 6, values) == 4 && values[0] == 3 && values[3] == 240);
    glViewport(16777217, 2, 20000, 21000); previous = serial();
    glViewport(16777217, 2, 20000, 21000); assert(serial() == previous);
    assert(context.state.viewport[0] == 16777217 && context.state.viewport[2] == 20000);
    assert(context.state.viewports[0][2] == 16384 && context.state.viewports[15][3] == 16384);
}
static void stream_tests(void)
{
    context.thread = (struct glm_thread *)(uintptr_t)1;
    uint64_t previous = serial();
    GLenum before = context.state.depth_func;
    glDepthFunc(GL_ALWAYS); glActiveTexture(GL_TEXTURE0 + 2);
    assert(pending[0] == 2 && !glm_thread_state_settled(&context));
    assert(serial() == previous && context.state.depth_func == before);
    GLfloat value = 0;
    unsigned sync_before = syncs;
    glGetFloatv(GL_DEPTH_FUNC, &value);
    assert(value == GL_ALWAYS && serial() == previous + 1 && syncs == sync_before + 1);
    assert(context.state.active_texture == GL_TEXTURE0 + 2 && !pending[0]);
    previous = serial();
    glDepthFunc(GL_ALWAYS); glActiveTexture(GL_TEXTURE0 + 2);
    assert(pending[0] == 2 && serial() == previous);
    glGetFloatv(GL_ACTIVE_TEXTURE, &value);
    assert(value == GL_TEXTURE0 + 2 && serial() == previous && syncs == sync_before + 2);
    glGetFloatv(GL_ACTIVE_TEXTURE, &value); assert(syncs == sync_before + 2);
    glDepthFunc(0); assert(pending[0] == 1); drain();
    assert(context.error == GL_INVALID_ENUM && serial() == previous); context.error = 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    assert(pending[0] == 1 && pending[1] == 1 && pending[2] == 0);
    drain(); assert(texture_parameter == GL_LINEAR && serial() == previous + 1);
    previous = serial();
    glGenerateMipmap(GL_TEXTURE_2D);
    assert(pending[0] == 1 && pending[1] == 1 && pending[2] == 1);
    drain(); assert(serial() == previous + 1);
    context.thread = NULL;
}
static void replay_lists(void)
{ for (unsigned i = 0; i < list_count; ++i) lists[i].exec(lists[i].payload); }
static void clear_lists(void)
{ for (unsigned i = 0; i < list_count; ++i) free(lists[i].payload); list_count = 0; }
static void list_tests(void)
{
    context.listing = (struct glm_list_build *)(uintptr_t)1;
    execute_list = false;
    uint64_t previous = serial();
    GLenum old = context.state.depth_func;
    unsigned shadows = shadow_calls;
    glDepthFunc(GL_NEVER); glActiveTexture(GL_TEXTURE0 + 1); glViewport(9, 10, 50, 60);
    assert(serial() == previous && context.state.depth_func == old && shadow_calls == shadows && list_count == 3);
    assert(!pending[0] && !pending[1] && !pending[2] && list_notes);
    context.listing = NULL;
    replay_lists();
    assert(context.state.depth_func == GL_NEVER && context.state.active_texture == GL_TEXTURE0 + 1 && serial() == previous + 2);
    previous = serial(); replay_lists(); assert(serial() == previous && !pending[0]);
    clear_lists();
    context.listing = (struct glm_list_build *)(uintptr_t)1;
    execute_list = true;
    glDepthFunc(GL_LESS); assert(serial() == previous + 1 && list_count == 1);
    context.listing = NULL;
    previous = serial(); replay_lists(); assert(serial() == previous);
    clear_lists();
}
int main(void)
{
    context.profile = GLM_PROFILE_CORE;
    direct_tests(); stream_tests(); list_tests();
    printf("Local state CPU checks passed: 7000 repeated setters caused zero render-state serial changes; pending counters, query ordering, indexed state, and generated list replay preserved.\n");
    return 0;
}
