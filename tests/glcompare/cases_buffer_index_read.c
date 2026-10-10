/* The first direct EBO draw after a flush must retain its original indices
 * when an overlapping CPU write occurs before the command buffer commits. */
#include "glc_gl_core.h"
#include "glcompare.h"
#include <string.h>

static const char *const index_write_variants[] = {"subdata", "mapped"};

GLC_CASE_VARIANTS(core_buffer_index_read_after_flush, index_write_variants, .profile = GLC_CORE)
{
    static const char *const attributes[] = {"position", NULL};
    GLuint program = glc_program(
        "#version 150 core\nin vec2 position;void main(){gl_Position=vec4(position,0,1);}",
        "#version 150 core\nuniform vec4 shade;out vec4 color;void main(){color=shade;}",
        attributes);
    const GLfloat vertices[] = {
        -.9f, -.8f, -.1f, -.8f, -.5f, .8f,
         .1f, -.8f,  .9f, -.8f,  .5f, .8f,
    };
    const GLushort left[] = {0, 1, 2}, right[] = {3, 4, 5};
    GLuint vao, buffers[2];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof left, left, GL_DYNAMIC_DRAW);
    glUseProgram(program);
    GLint shade = glGetUniformLocation(program, "shade");
    glViewport(0, 0, glc_width, glc_height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* UINT16 indices at offset zero use the direct Metal index-buffer path.
     * No readback or flush may separate this draw from the overlapping write. */
    glUniform4f(shade, 1, 0, 0, 1);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
    if (glc_variant == 0) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof right, right);
    } else {
        void *mapped = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof right, GL_MAP_WRITE_BIT);
        if (!mapped) glc_fail("Index write map failed");
        memcpy(mapped, right, sizeof right);
        if (!glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER)) glc_fail("Index write unmap failed");
    }
    glUniform4f(shade, 0, 1, 0, 1);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);

    const GLubyte expected[2][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}};
    for (int side = 0; side < 2; ++side) {
        GLubyte pixel[4];
        glReadPixels(glc_width * (side ? 3 : 1) / 4, glc_height * 3 / 8,
                     1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (memcmp(pixel, expected[side], sizeof pixel))
            glc_fail("Index %s write changed %s triangle: RGBA %u/%u/%u/%u",
                     index_write_variants[glc_variant], side ? "right" : "left",
                     pixel[0], pixel[1], pixel[2], pixel[3]);
    }
    GLenum error = glGetError();
    if (error) glc_fail("Index %s write GL error %x", index_write_variants[glc_variant], error);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteBuffers(2, buffers);
    glDeleteVertexArrays(1, &vao);
}
