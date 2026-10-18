/* Line stipple on polygon outlines (GL_LINE polygon mode) with a one-dot
   pattern, which shows where each polygon's stipple counter starts and how
   it runs around the polygon: every primitive type, as Apple's
   implementation orders the edges (from the last vertex given; GL_POLYGON
   from its first), with the counter restarting for each polygon. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static const char *const shapes[] = {"triangle", "quad", "polygon", "two_triangles", "strip", "fan", "quad_strip"};
GLC_CASE_VARIANTS(line_stipple_outlines, shapes, .profile = GLC_LEGACY)
{
    static const float square[4][2] = {{4.5f, 4.5f}, {59.5f, 4.5f}, {59.5f, 59.5f}, {4.5f, 59.5f}};
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0x0001);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor3f(1, 1, 1);
    switch (glc_variant) {
    case 0:
        glBegin(GL_TRIANGLES);
        glVertex2fv(square[0]); glVertex2fv(square[1]); glVertex2fv(square[3]);
        glEnd();
        break;
    case 1: case 2:
        glBegin(glc_variant == 1 ? GL_QUADS : GL_POLYGON);
        for (int i = 0; i < 4; ++i) glVertex2fv(square[i]);
        glEnd();
        break;
    case 3:
        glBegin(GL_TRIANGLES);
        glVertex2f(2.5f, 2.5f); glVertex2f(30.5f, 2.5f); glVertex2f(2.5f, 30.5f);
        glVertex2f(34.5f, 34.5f); glVertex2f(61.5f, 34.5f); glVertex2f(34.5f, 61.5f);
        glEnd();
        break;
    case 4:
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(2.5f, 2.5f); glVertex2f(60.5f, 2.5f); glVertex2f(2.5f, 30.5f); glVertex2f(60.5f, 60.5f);
        glEnd();
        break;
    case 5:
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(2.5f, 2.5f); glVertex2f(60.5f, 2.5f); glVertex2f(60.5f, 60.5f); glVertex2f(2.5f, 60.5f);
        glEnd();
        break;
    case 6:
        glBegin(GL_QUAD_STRIP);
        glVertex2f(2.5f, 2.5f); glVertex2f(60.5f, 2.5f); glVertex2f(2.5f, 30.5f); glVertex2f(60.5f, 30.5f);
        glVertex2f(2.5f, 60.5f); glVertex2f(60.5f, 60.5f);
        glEnd();
        break;
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LINE_STIPPLE);
}
