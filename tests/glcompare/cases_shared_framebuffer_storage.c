/* Rebinding an unchanged FBO must observe ordered shared storage changes,
   even while its context already has an open render pass. */
#include <OpenGL/OpenGL.h>
#include "glc_gl_core.h"
#include "glcompare.h"
#include <pthread.h>
#include <string.h>

extern void *glc_lookup(const char *name);

struct storage_change {
    CGLContextObj producer;
    CGLError (*set_current)(CGLContextObj);
    GLuint texture;
    GLuint renderbuffer;
    GLenum error;
};

static void *replace_storage(void *pointer)
{
    struct storage_change *change = pointer;
    if (change->set_current(change->producer) != kCGLNoError) {
        change->error = GL_INVALID_OPERATION;
        return NULL;
    }
    if (change->texture) {
        unsigned char blue[16 * 16 * 4];
        for (unsigned i = 0; i < sizeof blue; i += 4) {
            blue[i] = blue[i + 1] = 0; blue[i + 2] = blue[i + 3] = 255;
        }
        glBindTexture(GL_TEXTURE_2D, change->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    } else {
        glBindRenderbuffer(GL_RENDERBUFFER, change->renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
    }
    glFinish();
    change->error = glGetError();
    change->set_current(NULL);
    return NULL;
}

GLC_CASE(core_shared_framebuffer_storage, .profile = GLC_CORE)
{
    CGLContextObj (*current)(void) = glc_lookup("CGLGetCurrentContext");
    CGLPixelFormatObj (*format)(CGLContextObj) = glc_lookup("CGLGetPixelFormat");
    CGLError (*create)(CGLPixelFormatObj, CGLContextObj, CGLContextObj *) = glc_lookup("CGLCreateContext");
    CGLError (*set_current)(CGLContextObj) = glc_lookup("CGLSetCurrentContext");
    CGLError (*destroy)(CGLContextObj) = glc_lookup("CGLDestroyContext");
    CGLContextObj consumer = current(), producer = NULL;
    if (create(format(consumer), consumer, &producer) != kCGLNoError) {
        glc_fail("Cannot create shared storage context"); return;
    }
    GLint original; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &original);
    GLuint framebuffer, texture, renderbuffer;
    glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers(1, &renderbuffer); glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 8, 8);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_SCISSOR_TEST);
    for (int rb = 0; rb < 2; ++rb) {
        if (rb) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
        else glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        /* Synchronize the CPU stream without ending the render pass. */
        if (glGetError()) glc_fail("Initial shared framebuffer clear failed");
        struct storage_change change = {producer, set_current, rb ? 0 : texture, rb ? renderbuffer : 0, 0};
        pthread_t worker;
        if (pthread_create(&worker, NULL, replace_storage, &change)) {
            glc_fail("Cannot start shared storage worker"); break;
        }
        pthread_join(worker, NULL);
        if (change.error) glc_fail("Shared storage replacement GL error %x", change.error);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        unsigned char color[4];
        glReadPixels(12, 12, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color);
        if (color[0] || color[1] != 255 || color[2] || color[3] != 255)
            glc_fail("Shared %s storage stayed stale: %u,%u,%u,%u", rb ? "renderbuffer" : "texture", color[0], color[1], color[2], color[3]);
        if (glGetError()) glc_fail("Shared framebuffer replacement GL error");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, original);
    glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &texture); glDeleteRenderbuffers(1, &renderbuffer);
    destroy(producer);
}
