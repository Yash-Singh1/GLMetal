/* Line stipple (GL 2.1 3.4.2): the 16-bit pattern and repeat factor along
   independent lines, strips (the counter continues across segments) and
   loops, x- and y-major, polygon outlines (GL_LINE polygon mode), drawn by
   fixed function and with GLSL and ARB vertex programs. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static GLuint glsl_program(void)
{
    const char *vs = "void main() { gl_Position = ftransform(); gl_FrontColor = gl_Color; }";
    GLuint program = glCreateProgram();
    GLuint shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shader, 1, &vs, NULL);
    glCompileShader(shader);
    glAttachShader(program, shader);
    glDeleteShader(shader);
    glLinkProgram(program);
    return program;
}

static void figures(float y)
{
    /* Independent lines: x-major and y-major, a diagonal. */
    glBegin(GL_LINES);
    glVertex2f(1.5f, y + 0.5f); glVertex2f(29.5f, y + 0.5f);
    glVertex2f(1.5f, y + 2.5f); glVertex2f(29.5f, y + 4.5f);
    glVertex2f(31.5f, y + 0.5f); glVertex2f(31.5f, y + 15.5f);
    glEnd();
    /* A strip: the counter continues into the next segment. */
    glBegin(GL_LINE_STRIP);
    glVertex2f(1.5f, y + 7.5f); glVertex2f(10.5f, y + 7.5f); glVertex2f(10.5f, y + 13.5f); glVertex2f(29.5f, y + 9.5f);
    glEnd();
    /* A loop. */
    glBegin(GL_LINE_LOOP);
    glVertex2f(34.5f, y + 1.5f); glVertex2f(46.5f, y + 1.5f); glVertex2f(46.5f, y + 13.5f);
    glEnd();
    /* A polygon outline. */
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBegin(GL_QUADS);
    glVertex2f(49.5f, y + 1.5f); glVertex2f(62.5f, y + 1.5f); glVertex2f(62.5f, y + 13.5f); glVertex2f(49.5f, y + 13.5f);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static const char *const stages[] = {"fixed", "glsl", "arb"};
GLC_CASE_VARIANTS(line_stipple, stages, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    GLuint program = 0, arb = 0;
    if (glc_variant == 1) {
        program = glsl_program();
        glUseProgram(program);
    } else if (glc_variant == 2) {
        arb = glc_arb_program(GL_VERTEX_PROGRAM_ARB, "!!ARBvp1.0\nOPTION ARB_position_invariant;\n"
                                                     "MOV result.color, vertex.color;\nEND\n");
    }
    glEnable(GL_LINE_STIPPLE);
    const struct { GLint factor; GLushort pattern; float r, g, b; } settings[] = {
        {1, 0x0F0F, 1, 0, 0}, {3, 0x1C47, 0, 1, 0}, {2, 0xAAAA, 0.3f, 0.5f, 1}, {1, 0x8001, 1, 1, 0}};
    for (int i = 0; i < 4; ++i) {
        glLineStipple(settings[i].factor, settings[i].pattern);
        glColor3f(settings[i].r, settings[i].g, settings[i].b);
        figures(16.0f * (float)i);
    }
    glDisable(GL_LINE_STIPPLE);
    glUseProgram(0);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    if (program) glDeleteProgram(program);
    if (arb) glDeleteProgramsARB(1, &arb);
}
