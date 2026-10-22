/* Indirect draws: the command's last word is the base instance in Apple's
   glDrawArraysIndirect and glDrawElementsIndirect (reserved in GL 4.1), and
   Apple's glMultiDrawElementsIndirectAPPLE /
   glMultiDrawElementsIndirectBaseInstanceAPPLE (not in the SDK headers;
   GL 4.3's glMultiDrawElementsIndirect arguments) draw a buffer of them.

   Each instance is a small quad coloured by two instanced attributes
   (divisors 1 and 2), so the picture shows the instance count, the base
   vertex and where each instanced array starts: base + floor(i / divisor)
   on Apple's and GLMetal alike. Quads are placed by gl_InstanceID, except
   in the single draws with a base instance: there Apple's gl_InstanceID
   counts from the base instance (Metal's instance_id), while GLMetal's
   counts from 0 as GL 4.2 defines it and as Apple's multi-draws do, so
   those place quads by a third instanced attribute. */
#include "glc_gl_core.h"
#include "glcompare.h"

void *glc_lookup(const char *name);

#define GLSL "#version 410 core\n"

static const char *const names[] = {"elements", "elements_base_instance", "arrays_base_instance", "multi_packed",
                                    "multi_stride", "multi_base_instance_apple", "multi_none"};

GLC_CASE_VARIANTS(draw_indirect, names, .profile = GLC_CORE)
{
    GLuint program = glc_program(
        GLSL "layout(location=0) in vec2 p; layout(location=1) in vec3 colour; layout(location=2) in float shade;\n"
             "layout(location=3) in float slot; out vec4 c; uniform float row, by_id;\n"
             "void main(){ c = vec4(colour * shade, 1.0);\n"
             "  vec2 at = vec2(-0.9 + 0.3 * (by_id > 0.5 ? float(gl_InstanceID) : slot), row);\n"
             "  gl_Position = vec4(at + p * 0.12, 0.0, 1.0); }",
        GLSL "in vec4 c; out vec4 frag; void main(){ frag = c; }", NULL);
    glUseProgram(program);
    GLint row = glGetUniformLocation(program, "row");
    glUniform1f(glGetUniformLocation(program, "by_id"), glc_variant == 1 || glc_variant == 2 ? 0.0f : 1.0f);
    GLuint vao, buffers[6];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(6, buffers);
    /* Two quads' corners: the second is a smaller one, for base vertex 4. */
    static const float corners[] = {-1, -1, 1, -1, 1, 1, -1, 1, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    static const float colours[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0.5f, 0.5f, 0.5f};
    glBindBuffer(GL_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof colours, colours, GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribDivisor(1, 1);
    static const float shades[] = {1.0f, 0.75f, 0.5f, 0.25f, 0.9f, 0.6f};
    glBindBuffer(GL_ARRAY_BUFFER, buffers[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof shades, shades, GL_STATIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribDivisor(2, 2);
    static const float slots[] = {0, 1, 2, 3, 4, 5, 6};
    glBindBuffer(GL_ARRAY_BUFFER, buffers[5]);
    glBufferData(GL_ARRAY_BUFFER, sizeof slots, slots, GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribDivisor(3, 1);
    static const GLuint indices[] = {0, 1, 2, 0, 2, 3};
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
    /* Element commands (count, instances, first index, base vertex, base
       instance), 8 words apart for the strided variant. */
    static const GLuint commands[] = {
        6, 3, 0, 0, 0, 0xdead, 0xbeef, 7,
        6, 2, 0, 4, 2, 0xdead, 0xbeef, 7,
        3, 4, 3, 0, 1, 0xdead, 0xbeef, 7,
    };
    static const GLuint packed[] = {6, 3, 0, 0, 0, 6, 2, 0, 4, 2, 3, 4, 3, 0, 1};
    /* Array commands (count, instances, first, base instance). */
    static const GLuint arrays[] = {4, 3, 0, 2, 4, 2, 4, 3};
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffers[4]);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    typedef void (*multi_fn)(GLenum, GLenum, const void *, GLsizei, GLsizei);
    multi_fn multi = (multi_fn)glc_lookup(glc_variant == 5 ? "glMultiDrawElementsIndirectBaseInstanceAPPLE"
                                                            : "glMultiDrawElementsIndirectAPPLE");
    switch (glc_variant) {
    case 0: case 1:
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof packed, packed, GL_STATIC_DRAW);
        for (int i = 0; i < 3; ++i) {
            glUniform1f(row, 0.6f - 0.6f * (float)i);
            /* elements: the first command only, at each row; base instance: each command */
            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void *)(uintptr_t)(glc_variant ? i * 20 : 0));
        }
        break;
    case 2:
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof arrays, arrays, GL_STATIC_DRAW);
        for (int i = 0; i < 2; ++i) {
            glUniform1f(row, 0.6f - 0.6f * (float)i);
            glDrawArraysIndirect(GL_TRIANGLE_FAN, (const void *)(uintptr_t)(i * 16));
        }
        break;
    case 3: case 5:
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof packed, packed, GL_STATIC_DRAW);
        glUniform1f(row, 0.3f);
        multi(GL_TRIANGLES, GL_UNSIGNED_INT, 0, 3, 0);
        break;
    case 4:
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof commands, commands, GL_STATIC_DRAW);
        glUniform1f(row, -0.3f);
        multi(GL_TRIANGLES, GL_UNSIGNED_INT, (const void *)(uintptr_t)32, 2, 32);
        break;
    case 6:
        /* No commands: nothing drawn, no error. */
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof packed, packed, GL_STATIC_DRAW);
        multi(GL_TRIANGLES, GL_UNSIGNED_INT, 0, 0, 0);
        break;
    }
    GLenum error = glGetError();
    if (error) glc_fail("indirect draw: error %#x", error);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(6, buffers);
    glUseProgram(0);
    glDeleteProgram(program);
}
