/* gl_FragColor and ARB result.color broadcast to every draw buffer (GL 2.1
   3.11.2, ARB_draw_buffers), with 1 to 8 colour attachments, and
   gl_FragData[n] reaching only its own. Each attachment's colour is drawn
   back as a square: row = attachment count, column = attachment. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <string.h>

static GLuint glsl(const char *fragment)
{
    const char *vertex = "void main() { gl_Position = gl_Vertex; }";
    GLuint program = glCreateProgram();
    const char *sources[2] = {vertex, fragment};
    const GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    for (int i = 0; i < 2; ++i) {
        GLuint shader = glCreateShader(types[i]);
        glShaderSource(shader, 1, &sources[i], NULL);
        glCompileShader(shader);
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glLinkProgram(program);
    return program;
}

static void quad(void)
{
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
}

static const char *const kinds[] = {"glsl", "arb", "fragdata"};
GLC_CASE_VARIANTS(fragcolor_broadcast, kinds, .profile = GLC_LEGACY)
{
    GLint target = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
    GLuint program = 0, arb = 0;
    if (glc_variant == 0) program = glsl("void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }");
    if (glc_variant == 2)
        program = glsl("void main() { for (int i = 0; i < 8; ++i) gl_FragData[i] = vec4(float(i) / 7.0, 1.0, 0.5, 1.0); }");
    if (glc_variant == 1)
        arb = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, "!!ARBfp1.0\nMOV result.color, { 0.0, 1.0, 0.0, 1.0 };\nEND\n");
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int count = 1; count <= 8; ++count) {
        GLuint fbo, textures[8];
        GLenum buffers[8];
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(count, textures);
        for (int i = 0; i < count; ++i) {
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, textures[i], 0);
            buffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        glDrawBuffers(count, buffers);
        glViewport(0, 0, 4, 4);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (program) glUseProgram(program);
        else glEnable(GL_FRAGMENT_PROGRAM_ARB);
        quad();
        glUseProgram(0);
        glDisable(GL_FRAGMENT_PROGRAM_ARB);
        /* Each attachment's colour as a square of the output. */
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target);
        for (int i = 0; i < count; ++i) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
            glBlitFramebuffer(0, 0, 4, 4, i * 8, (count - 1) * 8, i * 8 + 6, (count - 1) * 8 + 6, GL_COLOR_BUFFER_BIT,
                              GL_NEAREST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)target);
        glDeleteTextures(count, textures);
        glDeleteFramebuffers(1, &fbo);
    }
    glViewport(0, 0, 64, 64);
    if (program) glDeleteProgram(program);
    if (arb) glDeleteProgramsARB(1, &arb);
}
