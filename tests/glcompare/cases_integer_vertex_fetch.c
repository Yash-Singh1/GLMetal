/* Vertex storage formats must not require different shader sources. Exercise
   signedness, component expansion and normalization with exact float readback. */
#include "glcompare.h"
#include "glc_gl_core.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define WIDTHS(t) t "1", t "2", t "3", t "4"
static const char *const formats[] = {
    WIDTHS("byte"), WIDTHS("ubyte"), WIDTHS("short"), WIDTHS("ushort"), WIDTHS("int"), WIDTHS("uint"),
    WIDTHS("byte_norm"), WIDTHS("ubyte_norm"), WIDTHS("short_norm"), WIDTHS("ushort_norm"),
    WIDTHS("int_norm"), WIDTHS("uint_norm")
};
#undef WIDTHS

static void vertex_fetch(bool cpu)
{
    int kind = (glc_variant / 4) % 6, width = glc_variant % 4 + 1;
    bool normalized = glc_variant >= 24;
    static const GLenum types[] = {GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT};
    unsigned char bytes[3][16] = {{0}};
    float expected[4] = {0, 0, 0, 1};
    /* Include signed minima and the 32-bit float rounding boundary. */
    static const int8_t b[] = {INT8_MIN, -3, 4, INT8_MAX};
    static const uint8_t ub[] = {UINT8_MAX, 3, 4, 128};
    static const int16_t sh[] = {INT16_MIN, -300, 400, INT16_MAX};
    static const uint16_t ush[] = {UINT16_MAX, 300, 400, 32768};
    static const int32_t si[] = {INT32_MIN, -16777217, 16777219, INT32_MAX};
    static const uint32_t ui[] = {UINT32_MAX, 16777217, 16777219, 2147483648u};
    const void *values[] = {b, ub, sh, ush, si, ui};
    const unsigned sizes[] = {1, 1, 2, 2, 4, 4};
    const double denominators[] = {INT8_MAX, UINT8_MAX, INT16_MAX, UINT16_MAX, INT32_MAX, UINT32_MAX};
    for (int v = 0; v < 3; ++v) memcpy(bytes[v], values[kind], width * sizes[kind]);
    for (int c = 0; c < width; ++c) {
        double x = kind == 0 ? b[c] : kind == 1 ? ub[c] : kind == 2 ? sh[c] :
                   kind == 3 ? ush[c] : kind == 4 ? (double)si[c] : (double)ui[c];
        expected[c] = normalized ? (float)fmax(-1.0, x / denominators[kind]) : (float)x;
    }
    const char *point_vertex = "#version 330 core\nlayout(location=0) in vec4 value; flat out vec4 data;\n"
        "void main(){ gl_Position=vec4(0,0,0,1); gl_PointSize=4; data=value; }";
    const char *triangle_vertex = "#version 330 core\nlayout(location=0) in vec4 value; flat out vec4 data;\n"
        "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0,1); data=value; }";
    GLuint program = glc_program(cpu ? triangle_vertex : point_vertex,
        "#version 330 core\nflat in vec4 data; out vec4 color; void main(){ color=data; }", NULL);
    GLuint vao, buffer, texture, fbo;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof bytes, bytes, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, width, types[kind], normalized, 16, 0);
    GLint target; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) glc_fail("float target incomplete");
    glViewport(0, 0, 4, 4); glUseProgram(program); glEnable(GL_PROGRAM_POINT_SIZE); glDrawArrays(cpu ? GL_TRIANGLES : GL_POINTS, 0, cpu ? 3 : 1);
    float actual[4]; glReadPixels(2, 2, 1, 1, GL_RGBA, GL_FLOAT, actual);
    for (int c = 0; c < 4; ++c) {
        double tolerance = normalized ? 0.000001 : 0.0;
        if (!isfinite(actual[c]) || fabs((double)actual[c] - expected[c]) > tolerance)
            glc_fail("%s component %d: got %.9g expected %.9g", formats[glc_variant], c, actual[c], expected[c]);
    }
    GLenum error = glGetError(); if (error) glc_fail("vertex fetch GL error %x", error);
    glBindFramebuffer(GL_FRAMEBUFFER, target); glViewport(0, 0, glc_width, glc_height);
    glClearColor(.25f, .5f, .75f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer); glDeleteVertexArrays(1, &vao); glDeleteProgram(program);
}

GLC_CASE_VARIANTS(core_integer_vertex_fetch, formats, .profile = GLC_CORE)
{
    vertex_fetch(false);
}
GLC_CASE_VARIANTS(core_integer_vertex_fetch_cpu, formats, .profile = GLC_CORE)
{
    vertex_fetch(true);
}
