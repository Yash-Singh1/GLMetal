/* Polygon stipple (GL 2.1 3.5.2): the 32x32 pattern against window
   coordinates for fixed function and a GLSL program, unpacking with
   GL_UNPACK_LSB_FIRST and skips, glPushAttrib(GL_POLYGON_STIPPLE_BIT),
   lines in GL_LINE polygon mode (not stippled), and glGetPolygonStipple
   (its bytes drawn as a row of pixels). */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <string.h>

static void rect(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

GLC_CASE(polygon_stipple, .profile = GLC_LEGACY)
{
    GLubyte pattern[128], other[128 + 8 * 8];
    for (int i = 0; i < 128; ++i) pattern[i] = (GLubyte)(i * 37 + (i >> 2) * 11);
    for (int i = 0; i < (int)sizeof other; ++i) other[i] = (GLubyte)(0xA5 ^ (i * 13));
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_POLYGON_STIPPLE);
    /* Fixed function, default unpacking. */
    glPolygonStipple(pattern);
    glColor3f(1, 0, 0);
    rect(0, 0, 32, 32);
    /* LSB first, row length and skips. */
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 48);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glPolygonStipple(other);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glColor3f(0, 1, 0);
    rect(32, 0, 64, 32);
    /* A GLSL program; then the pushed pattern comes back. */
    glPushAttrib(GL_POLYGON_STIPPLE_BIT);
    glPolygonStipple(pattern);
    const char *vs = "void main() { gl_Position = ftransform(); }";
    const char *fs = "void main() { gl_FragColor = vec4(0.2, 0.4, 1.0, 1.0); }";
    GLuint program = glCreateProgram();
    const char *sources[2] = {vs, fs};
    const GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    for (int i = 0; i < 2; ++i) {
        GLuint shader = glCreateShader(types[i]);
        glShaderSource(shader, 1, &sources[i], NULL);
        glCompileShader(shader);
        glAttachShader(program, shader);
        glDeleteShader(shader);
    }
    glLinkProgram(program);
    glUseProgram(program);
    rect(0, 32, 32, 56);
    glUseProgram(0);
    glDeleteProgram(program);
    glPopAttrib();
    glColor3f(1, 1, 0);
    rect(32, 32, 48, 56);
    /* Outlines are not stippled. */
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor3f(1, 0, 1);
    rect(50.5f, 34.5f, 62.5f, 54.5f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_POLYGON_STIPPLE);
    /* glGetPolygonStipple with packing: the bytes as grey pixels. */
    GLubyte packed[256];
    memset(packed, 0x55, sizeof packed);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glGetPolygonStipple(packed);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBegin(GL_POINTS);
    for (int i = 0; i < 64 * 2; ++i) {
        glColor3ub(packed[i], packed[i + 128], 0);
        glVertex2f((float)(i % 64) + 0.5f, 62.5f - (float)(i / 64));
    }
    glEnd();
}
