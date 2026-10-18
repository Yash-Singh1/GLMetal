/* piglit's general/linestipple drawing (lines, restarts within one
   glBegin/glEnd, a strip, loops with repeat factors 2 and 3) under a
   translated orthographic projection, all into one image. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

struct line { GLint factor; GLushort pattern; float color[3]; GLenum mode; int count; int v[4][2]; };

GLC_CASE(line_stipple_piglit, .profile = GLC_LEGACY, .width = 160, .height = 160)
{
    static const struct line lines[] = {
        {1, 0xffff, {1, 1, 1}, GL_LINES, 2, {{0, 0}, {24, 0}}},
        {1, 0x00ff, {1, 0, 0}, GL_LINES, 4, {{0, 2}, {24, 2}, {0, 4}, {24, 4}}},
        {1, 0x0f8f, {1, 1, 0}, GL_LINE_STRIP, 3, {{0, 6}, {24, 6}, {24, 30}}},
        {1, 0x8cef, {0, 1, 0}, GL_LINE_LOOP, 4, {{26, 0}, {46, 0}, {46, 20}, {26, 20}}},
        {2, 0x838f, {0, 0, 1}, GL_LINE_LOOP, 4, {{0, 32}, {32, 32}, {32, 33}, {0, 33}}},
        {3, 0xf731, {0, 1, 1}, GL_LINE_LOOP, 4, {{0, 35}, {63, 35}, {63, 36}, {0, 36}}},
    };
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 160, 0, 160, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_LINE_STIPPLE);
    glPushMatrix();
    glTranslatef(10, 10, 0);
    for (unsigned i = 0; i < sizeof lines / sizeof *lines; ++i) {
        glLineStipple(lines[i].factor, lines[i].pattern);
        glColor3fv(lines[i].color);
        glBegin(lines[i].mode);
        for (int k = 0; k < lines[i].count; ++k) glVertex2f(lines[i].v[k][0] + 0.5f, lines[i].v[k][1] + 0.5f);
        glEnd();
    }
    glPopMatrix();
    glDisable(GL_LINE_STIPPLE);
}
