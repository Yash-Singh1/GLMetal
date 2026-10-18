/* ARB vertex programs reading generic attributes from client arrays.
   Generic attribute 5 shares the fog coordinate's slot, but a 4-component
   glVertexAttribPointerARB(5) array keeps all four components (Call of
   Duty 2 passes its D3D vertex colour there). Generic attribute 6 has no
   conventional alias and must stay at location 6 behind the program's
   uniform block. The conventional fog coordinate still reads (f, 0, 0, 1). */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static void quad(int x, const char *program, GLuint index, GLint size, GLenum type, GLboolean normalized,
                 const void *data, bool fog)
{
    static const float positions[] = {-1, -1, 0, 1, 1, -1, 0, 1, 1, 1, 0, 1, -1, 1, 0, 1};
    GLuint vertex = glc_arb_program(GL_VERTEX_PROGRAM_ARB, program);
    glScissor(x, 0, 21, 64);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointerARB(0, 4, GL_FLOAT, GL_FALSE, 0, positions);
    glEnableVertexAttribArrayARB(0);
    if (fog) {
        glFogCoordPointer(type, 0, data);
        glEnableClientState(GL_FOG_COORD_ARRAY);
    } else {
        glVertexAttribPointerARB(index, size, type, normalized, 0, data);
        glEnableVertexAttribArrayARB(index);
    }
    glDrawArrays(GL_QUADS, 0, 4);
    if (fog) glDisableClientState(GL_FOG_COORD_ARRAY);
    else glDisableVertexAttribArrayARB(index);
    glDisableVertexAttribArrayARB(0);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDeleteProgramsARB(1, &vertex);
}

GLC_CASE(arb_generic_attribs, .profile = GLC_LEGACY)
{
    static const unsigned char bgra[4][4] = {{51, 102, 204, 255}, {51, 102, 204, 255}, {51, 102, 204, 255}, {51, 102, 204, 255}};
    static const float rgba[4][4] = {{.8f, .4f, .2f, 1}, {.8f, .4f, .2f, 1}, {.8f, .4f, .2f, 1}, {.8f, .4f, .2f, 1}};
    static const float fog[4] = {.6f, .6f, .6f, .6f};
    GLuint fragment = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n");
    glEnable(GL_SCISSOR_TEST);
    quad(0, "!!ARBvp1.0\nPARAM c[4] = { program.env[23..26] };\nATTRIB p = vertex.attrib[0];\nATTRIB c5 = vertex.attrib[5];\n"
            "MOV result.position, p;\nMOV result.color, c5;\nEND\n", 5, 4, GL_UNSIGNED_BYTE, GL_TRUE, bgra, false);
    quad(21, "!!ARBvp1.0\nPARAM c[4] = { program.env[23..26] };\nMOV result.position, vertex.attrib[0];\n"
             "MOV result.color, vertex.attrib[6];\nEND\n", 6, 4, GL_FLOAT, GL_FALSE, rgba, false);
    quad(42, "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, vertex.fogcoord;\nEND\n",
         0, 1, GL_FLOAT, GL_FALSE, fog, true);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &fragment);
    const struct { int x; unsigned char rgb[3]; } expect[] = {{10, {51, 102, 204}}, {31, {204, 102, 51}}, {52, {153, 0, 0}}};
    for (int i = 0; i < 3; ++i) {
        unsigned char pixel[4];
        glReadPixels(expect[i].x, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        for (int c = 0; c < 3; ++c)
            if (pixel[c] < expect[i].rgb[c] - 1 || pixel[c] > expect[i].rgb[c] + 1)
                glc_fail("quad %d: %u,%u,%u, expected %u,%u,%u", i, pixel[0], pixel[1], pixel[2],
                         expect[i].rgb[0], expect[i].rgb[1], expect[i].rgb[2]);
    }
}
