#include "glc_gl_core.h"
#include "glcompare.h"
#include <stdint.h>

static const char *const modes[] = {"restored", "constant", "format_offset", "buffer_lifetime"};

/* A cached layout must survive irrelevant/restored state, but still notice
   changes to a used input's fetch format, offset, enable bit and buffer. */
GLC_CASE_VARIANTS(core_vertex_layout_reuse, modes, .profile = GLC_CORE)
{
    GLuint program = glc_program(
        "#version 410 core\nlayout(location=0) in vec2 p;layout(location=1) in vec4 c;out vec4 color;"
        "void main(){gl_Position=vec4(p,0,1);color=c;}",
        "#version 410 core\nin vec4 color;out vec4 frag;void main(){frag=color;}", NULL);
    GLuint vao, positions, colors;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &positions); glBindBuffer(GL_ARRAY_BUFFER, positions);
    const float triangle[] = {-1,-1,3,-1,-1,3};
    glBufferData(GL_ARRAY_BUFFER, sizeof triangle, triangle, GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(0);
    glGenBuffers(1, &colors);
    float floats[2][3][4];
    uint8_t bytes[2][3][4];
    for (int color = 0; color < 2; ++color)
        for (int vertex = 0; vertex < 3; ++vertex)
            for (int c = 0; c < 4; ++c) {
                floats[color][vertex][c] = c == color || c == 3 ? 1 : 0;
                bytes[color][vertex][c] = c == color || c == 3 ? 255 : 0;
            }
    glBindBuffer(GL_ARRAY_BUFFER, colors);
    glBufferData(GL_ARRAY_BUFFER,sizeof floats,floats,GL_STATIC_DRAW);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(1);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    for (int strip = 0; strip < 8; ++strip) {
        int green = glc_variant == 0 ? 0 : strip & 1;
        glViewport(0,strip*8,64,8);
        if (glc_variant == 0) {
            /* The last change affects only an input absent from this shader. */
            glDisableVertexAttribArray(1); glEnableVertexAttribArray(1);
            glBindBuffer(GL_ARRAY_BUFFER,positions);
            glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,NULL);
            glBindBuffer(GL_ARRAY_BUFFER,colors);
            glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,0,NULL);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(7,2,GL_FLOAT,GL_FALSE,16,(void *)(uintptr_t)(strip*4));
            glVertexAttribDivisor(7,(GLuint)strip);
            glEnableVertexAttribArray(7);
        } else if (glc_variant == 1) {
            glDisableVertexAttribArray(1);
            glVertexAttrib4f(1,!green,green,0,1);
            /* Disabled fetch state must not hide subsequent current values. */
            glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void *)(uintptr_t)(strip*4));
        } else if (glc_variant == 2) {
            int packed = (strip / 2) & 1;
            glBindBuffer(GL_ARRAY_BUFFER,colors);
            glBufferData(GL_ARRAY_BUFFER,packed ? sizeof bytes : sizeof floats,
                         packed ? (const void *)bytes : floats,GL_STATIC_DRAW);
            glVertexAttribPointer(1,4,packed ? GL_UNSIGNED_BYTE : GL_FLOAT,packed ? GL_TRUE : GL_FALSE,
                                  packed ? 4 : 16,(void *)(uintptr_t)(green * (packed ? 12 : 48)));
        } else {
            glDeleteBuffers(1,&colors); glGenBuffers(1,&colors); glBindBuffer(GL_ARRAY_BUFFER,colors);
            glBufferData(GL_ARRAY_BUFFER,sizeof floats[green],floats[green],GL_STATIC_DRAW);
            glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(1);
        }
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    for (int strip = 0; strip < 8; ++strip) {
        unsigned char pixel[4];
        int green = glc_variant == 0 ? 0 : strip & 1;
        glReadPixels(32,strip*8+4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if (pixel[0] != (green ? 0 : 255) || pixel[1] != (green ? 255 : 0) || pixel[2])
            glc_fail("%s strip %d has stale vertex input: %u,%u,%u",modes[glc_variant],strip,
                     pixel[0],pixel[1],pixel[2]);
    }
    if (glGetError()) glc_fail("%s vertex layout error",modes[glc_variant]);
    glViewport(0,0,64,64); glDeleteBuffers(1,&positions); glDeleteBuffers(1,&colors);
    glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
}
