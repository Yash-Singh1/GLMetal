/* ARB vertex programs reading current vertex attribute values (no arrays):
   glVertexAttrib*ARB on every generic slot, the conventional calls
   (glColor, glNormal, glFogCoord, glSecondaryColor, glMultiTexCoord,
   glWeight) feeding the generic slots they alias on Apple, and generic
   values feeding the conventional bindings. One 4x64 column per check. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>
#include <string.h>

static void column(int x, const char *binding)
{
    char source[512];
    snprintf(source, sizeof source, "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, %s;\nEND\n",
             binding);
    GLuint program = glc_arb_program(GL_VERTEX_PROGRAM_ARB, source);
    glScissor(x, 0, 4, 64);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}

GLC_CASE(arb_current_attribs, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    int x = 0;
    /* Generic values, read back through vertex.attrib[n]. */
    for (GLuint n = 1; n < 16; ++n) {
        glVertexAttrib4fARB(n, 0.0625f * (float)n, 1 - 0.0625f * (float)n, 0.5f, 0.75f);
        char binding[32];
        snprintf(binding, sizeof binding, "vertex.attrib[%u]", n);
        column(x, binding);
        x += 4;
    }
    /* Fewer components: the rest default to (0, 0, 1). */
    glVertexAttrib2fARB(6, 0.25f, 0.5f);
    column(x, "vertex.attrib[6]"); x += 4;
    glVertexAttrib1fARB(7, 0.75f);
    column(x, "vertex.attrib[7]"); x += 4;
    /* Conventional calls through their aliases. */
    glColor4f(0.1f, 0.2f, 0.3f, 0.4f);
    column(x, "vertex.attrib[3]"); x += 4;
    glSecondaryColor3f(0.9f, 0.8f, 0.7f);
    column(x, "vertex.attrib[4]"); x += 4;
    glFogCoordf(0.6f);
    column(x, "vertex.attrib[5]"); x += 4;
    glMultiTexCoord4f(GL_TEXTURE1, 0.3f, 0.6f, 0.9f, 0.2f);
    column(x, "vertex.attrib[9]"); x += 4;
    glNormal3f(0.25f, 0.5f, 0.75f);
    column(x, "vertex.attrib[2]"); x += 4;
    /* Generic values through the conventional bindings. */
    glVertexAttrib4fARB(3, 0.7f, 0.1f, 0.4f, 1);
    column(x, "vertex.color"); x += 4;
    glVertexAttrib4fARB(10, 0.2f, 0.9f, 0.6f, 1);
    column(x, "vertex.texcoord[2]"); x += 4;
    glDisable(GL_SCISSOR_TEST);
}
