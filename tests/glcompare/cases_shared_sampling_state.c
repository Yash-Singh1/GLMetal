/* A shared context may change sampling metadata while the consumer keeps
   the same program, bindings, and viewport between draws. */
#include <OpenGL/OpenGL.h>
#include "glc_gl_core.h"
#include "glcompare.h"
#include <string.h>
#include <pthread.h>

extern void *glc_lookup(const char *name);

struct sampling_update {
    CGLContextObj context;
    CGLError (*set_current)(CGLContextObj);
    GLuint texture, sampler, program;
    GLint image_location;
    int mutation;
    CGLError context_error;
    GLenum gl_error;
};

static void *update_shared_sampling(void *opaque)
{
    struct sampling_update *update = opaque;
    update->context_error = update->set_current(update->context);
    if (update->context_error != kCGLNoError) return NULL;
    if (update->mutation == 2) {
        glUseProgram(update->program);
        glUniform1i(update->image_location, 1);
    } else if (update->mutation == 1) {
        glBindTexture(GL_TEXTURE_2D, update->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_GREEN);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
    } else {
        glSamplerParameteri(update->sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
    glFinish();
    update->gl_error = glGetError();
    update->set_current(NULL);
    return NULL;
}

static int run_sampling_update(struct sampling_update *update)
{
    pthread_t worker;
    if (pthread_create(&worker, NULL, update_shared_sampling, update)) return 0;
    if (pthread_join(worker, NULL)) return 0;
    return update->context_error == kCGLNoError && update->gl_error == GL_NO_ERROR;
}

GLC_CASE(core_shared_sampling_state, .profile = GLC_CORE, .width = 32, .height = 32, .tolerance = 1)
{
    CGLContextObj (*get_current)(void) = glc_lookup("CGLGetCurrentContext");
    CGLPixelFormatObj (*get_format)(CGLContextObj) = glc_lookup("CGLGetPixelFormat");
    CGLError (*create)(CGLPixelFormatObj, CGLContextObj, CGLContextObj *) = glc_lookup("CGLCreateContext");
    CGLError (*set_current)(CGLContextObj) = glc_lookup("CGLSetCurrentContext");
    CGLError (*destroy)(CGLContextObj) = glc_lookup("CGLDestroyContext");
    if (!get_current || !get_format || !create || !set_current || !destroy) {
        glc_fail("Missing shared context functions"); return;
    }
    CGLContextObj consumer = get_current(), producer = NULL;
    if (create(get_format(consumer), consumer, &producer) != kCGLNoError) {
        glc_fail("Cannot create sampling producer"); return;
    }
    GLuint texture, blue_texture, sampler, vao;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char pixels[8] = {255, 0, 0, 255, 0, 255, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &blue_texture); glBindTexture(GL_TEXTURE_2D, blue_texture);
    const unsigned char blue[4] = {0, 0, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindSampler(0, sampler);
    GLuint program = glc_program(
        "#version 150 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}",
        "#version 150 core\nuniform sampler2D image;out vec4 color;void main(){color=texture(image,vec2(1.25,.5));}", NULL);
    glUseProgram(program);
    GLint image_location = glGetUniformLocation(program, "image");
    glUniform1i(image_location, 0);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glViewport(0, 0, 32, 32);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    struct sampling_update update = {
        .context = producer, .set_current = set_current,
        .texture = texture, .sampler = sampler, .program = program,
        .image_location = image_location, .context_error = kCGLNoError,
        .gl_error = GL_NO_ERROR,
    };
    unsigned char actual[4];
    const unsigned char green[4] = {0, 255, 0, 255};
    const unsigned char red[4] = {255, 0, 0, 255};
    glDrawArrays(GL_TRIANGLES, 0, 3);
    /* Drain the consumer command stream without ending its render pass. */
    if (glGetError()) glc_fail("Initial sampling draw GL error");
    if (!run_sampling_update(&update)) {
        glc_fail("Cannot update shared sampler"); goto cleanup;
    }
    /* Rebind the same object to publish the shared change to this context. */
    glBindSampler(0, sampler);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    if (memcmp(actual, green, 4)) glc_fail("Shared sampler wrap did not refresh");
    /* Open a pass again before the second producer mutation. */
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError()) glc_fail("Sampling draw before swizzle GL error");
    update.mutation = 1;
    if (!run_sampling_update(&update)) {
        glc_fail("Cannot update shared texture"); goto cleanup;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    if (memcmp(actual, red, 4)) glc_fail("Shared texture swizzle did not refresh");
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError()) glc_fail("Sampling draw before unit change GL error");
    update.mutation = 2;
    if (!run_sampling_update(&update)) {
        glc_fail("Cannot update shared sampler unit"); goto cleanup;
    }
    glUseProgram(program);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    if (memcmp(actual, blue, 4)) glc_fail("Shared sampler unit did not refresh");
    if (glGetError()) glc_fail("Shared sampling metadata GL error");
cleanup:
    glDeleteVertexArrays(1, &vao); glDeleteProgram(program);
    glDeleteSamplers(1, &sampler); glDeleteTextures(1, &texture);
    glDeleteTextures(1, &blue_texture);
    destroy(producer);
}
