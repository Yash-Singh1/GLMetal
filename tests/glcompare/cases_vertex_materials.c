/* glMaterial between glBegin and glEnd (GL 2.1 2.14.3): each vertex is lit
   with the materials current when it was specified. Smooth and flat
   shading, two-sided lighting with separate front and back changes,
   shininess, colour material tracking one property while glMaterial changes
   the others, quads and polygons, and the same calls compiled into a list. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static void set(GLenum face, GLenum pname, float r, float g, float b)
{
    const GLfloat v[4] = {r, g, b, 1};
    glMaterialfv(face, pname, v);
}

static void figures(void)
{
    /* A strip: emission, ambient and diffuse change per vertex. */
    glNormal3f(0, 0, 1);
    glBegin(GL_TRIANGLE_STRIP);
    set(GL_FRONT_AND_BACK, GL_EMISSION, 0.4f, 0, 0);
    glVertex2f(-0.95f, -0.95f);
    set(GL_FRONT_AND_BACK, GL_DIFFUSE, 0, 0.6f, 0);
    glVertex2f(-0.95f, -0.45f);
    set(GL_FRONT_AND_BACK, GL_AMBIENT, 0, 0, 0.8f);
    glVertex2f(-0.45f, -0.95f);
    set(GL_FRONT_AND_BACK, GL_EMISSION, 0, 0.3f, 0.3f);
    glVertex2f(-0.45f, -0.45f);
    glVertex2f(0.05f, -0.95f);
    set(GL_FRONT, GL_DIFFUSE, 0.9f, 0.9f, 0);
    glVertex2f(0.05f, -0.45f);
    glEnd();
    /* Specular and shininess with a tilted normal. */
    glBegin(GL_QUADS);
    glNormal3f(0.3f, 0.2f, 0.93f);
    set(GL_FRONT_AND_BACK, GL_SPECULAR, 1, 1, 1);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 2);
    glVertex2f(0.15f, -0.95f);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40);
    glVertex2f(0.95f, -0.95f);
    set(GL_FRONT_AND_BACK, GL_SPECULAR, 0.2f, 0.5f, 1);
    glVertex2f(0.95f, -0.45f);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0);
    glVertex2f(0.15f, -0.45f);
    glEnd();
    /* Back faces: a clockwise polygon with back-only changes. */
    glNormal3f(0, 0, 1);
    glBegin(GL_POLYGON);
    set(GL_BACK, GL_EMISSION, 0.5f, 0.1f, 0.6f);
    glVertex2f(-0.95f, -0.35f);
    glVertex2f(-0.95f, 0.15f);
    set(GL_BACK, GL_DIFFUSE, 0, 0.2f, 0.1f);
    glVertex2f(-0.45f, 0.15f);
    set(GL_FRONT, GL_EMISSION, 1, 1, 1);
    glVertex2f(-0.2f, -0.1f);
    glVertex2f(-0.45f, -0.35f);
    glEnd();
    /* Colour material tracks the diffuse colour while glMaterial changes
       ambient (and its tracked diffuse change is ignored). */
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
    glBegin(GL_TRIANGLES);
    glColor3f(0.2f, 0.4f, 0.9f);
    set(GL_FRONT_AND_BACK, GL_AMBIENT, 0.9f, 0, 0);
    glVertex2f(-0.1f, -0.35f);
    glColor3f(0.9f, 0.4f, 0.2f);
    set(GL_FRONT_AND_BACK, GL_DIFFUSE, 0, 1, 0);
    glVertex2f(0.6f, -0.35f);
    set(GL_FRONT_AND_BACK, GL_AMBIENT, 0, 0, 0);
    glVertex2f(0.25f, 0.25f);
    glEnd();
    glDisable(GL_COLOR_MATERIAL);
}

static void reset_materials(void)
{
    const GLfloat black[4] = {0, 0, 0, 1}, grey[4] = {0.2f, 0.2f, 0.2f, 1};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, grey);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0);
}

static const char *const variants[] = {"smooth", "flat", "list"};
GLC_CASE_VARIANTS(vertex_materials, variants, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const GLfloat white[4] = {1, 1, 1, 1}, light[4] = {0.2f, 0.4f, 1, 0}, ambient[4] = {0.3f, 0.3f, 0.3f, 1};
    glLightfv(GL_LIGHT0, GL_AMBIENT, white);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glShadeModel(glc_variant == 1 ? GL_FLAT : GL_SMOOTH);
    reset_materials();
    if (glc_variant == 2) {
        GLuint list = glGenLists(1);
        glNewList(list, GL_COMPILE);
        figures();
        glEndList();
        /* Moved up by the modelview: the materials the list set stay. */
        glMatrixMode(GL_MODELVIEW);
        glTranslatef(0, 0.6f, 0);
        glCallList(list);
        glLoadIdentity();
        glDeleteLists(list, 1);
    } else {
        figures();
    }
    /* The materials after glEnd are the last ones set: a plain quad. */
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex2f(0.7f, 0.7f); glVertex2f(0.95f, 0.7f); glVertex2f(0.95f, 0.95f); glVertex2f(0.7f, 0.95f);
    glEnd();
    glShadeModel(GL_SMOOTH);
    glDisable(GL_LIGHTING);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
}
