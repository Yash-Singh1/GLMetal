/* ARB vertex program address registers (ARL, relative PARAM addressing):
   one and two registers, positive and negative constant offsets, indices
   from generic attributes and from the conventional ones, drawn in
   immediate mode and from vertex arrays (where the generic attribute is a
   current value). One 1x64 column per check; indices stay inside the
   arrays (past them is undefined). */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>

static const float values[][2] = {{1, 1}, {2, 2}, {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, -1}, {-2, -1}, {2, -2},
                                  {1.5f, 0.9f}, {-0.5f, 2.99f}};

GLC_CASE(arb_address, .profile = GLC_LEGACY)
{
    static const float colors[4][4] = {{1, 0, 0, 0.5f}, {0, 0.5f, 0, 0.5f}, {0, 0, 1, 0.25f}, {0.5f, 0.5f, 0, 1}};
    for (int i = 0; i < 4; ++i) glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB, (GLuint)i, colors[i]);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    int x = 0;
    for (unsigned v = 0; v < sizeof values / sizeof *values; ++v)
        for (int form = 0; form < 4; ++form) {
            if (form == 2 && !(values[v][1] >= 0 && values[v][1] < 2)) { x += 1; continue; }
            int o0 = 1 - (int)values[v][0], o1 = 1 - (int)values[v][1];
            char source[768];
            if (form == 0 || form == 3) /* two registers from a generic attribute (piglit vp-address-02) */
                snprintf(source, sizeof source,
                         "!!ARBvp1.0\nPARAM colors[] = { program.env[0..3] };\nADDRESS A0, A1;\n"
                         "ARL A0.x, vertex.attrib[1].x;\nARL A1.x, vertex.attrib[1].y;\n"
                         "ADD result.color, colors[A0.x %c %d], colors[A1.x %c %d];\n"
                         "MOV result.position, vertex.position;\nEND\n",
                         o0 < 0 ? '-' : '+', o0 < 0 ? -o0 : o0, o1 < 0 ? '-' : '+', o1 < 0 ? -o1 : o1);
            else if (form == 1) /* one register from the texture coordinate */
                snprintf(source, sizeof source,
                         "!!ARBvp1.0\nPARAM colors[4] = { program.env[0..3] };\nADDRESS A0;\n"
                         "ARL A0.x, vertex.texcoord[0].x;\nMOV result.color, colors[A0.x + %d];\n"
                         "MOV result.position, vertex.position;\nEND\n", o0 < 0 ? 0 : o0);
            else /* an index built with arithmetic */
                snprintf(source, sizeof source,
                         "!!ARBvp1.0\nPARAM colors[] = { program.env[0..3] };\nADDRESS A0;\nTEMP t;\n"
                         "ADD t, vertex.attrib[1], vertex.attrib[1];\nARL A0.x, t.y;\n"
                         "MOV result.color, colors[A0.x];\nMOV result.position, vertex.position;\nEND\n");
            GLuint program = glc_arb_program(GL_VERTEX_PROGRAM_ARB, source);
            glVertexAttrib2fARB(1, values[v][0], values[v][1]);
            glMultiTexCoord2f(GL_TEXTURE0, values[v][0], values[v][1]);
            glScissor(x, 0, 1, 64);
            if (form == 3) {
                static const float quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};
                glVertexPointer(2, GL_FLOAT, 0, quad);
                glEnableClientState(GL_VERTEX_ARRAY);
                glDrawArrays(GL_QUADS, 0, 4);
                glDisableClientState(GL_VERTEX_ARRAY);
            } else {
                glBegin(GL_QUADS);
                glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
                glEnd();
            }
            glDisable(GL_VERTEX_PROGRAM_ARB);
            glDeleteProgramsARB(1, &program);
            x += 1;
        }
    glDisable(GL_SCISSOR_TEST);
}
