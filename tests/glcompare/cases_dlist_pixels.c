/* Pixel data compiled into display lists (GL 2.1 5.4): glBitmap,
   glDrawPixels, glPolygonStipple, glTexImage2D and glTexSubImage2D unpack
   when the list is compiled, with the unpack state of that moment and from
   the bound pixel unpack buffer; the client memory and the unpack state
   change before the list runs. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <string.h>

static void textured_quad(float x0, float y0, float x1, float y1)
{
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x0, y0);
    glTexCoord2f(1, 0); glVertex2f(x1, y0);
    glTexCoord2f(1, 1); glVertex2f(x1, y1);
    glTexCoord2f(0, 1); glVertex2f(x0, y1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

static const char *const modes[] = {"compile", "compile_and_execute"};
GLC_CASE_VARIANTS(dlist_pixels, modes, .profile = GLC_LEGACY)
{
    const GLenum list_mode = glc_variant ? GL_COMPILE_AND_EXECUTE : GL_COMPILE;
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    GLubyte bits[64 * 8], pixels[16 * 16 * 4], stipple[256];
    for (int i = 0; i < (int)sizeof bits; ++i) bits[i] = (GLubyte)(i * 29 + 7);
    for (int i = 0; i < (int)sizeof pixels; ++i) pixels[i] = (GLubyte)(i * 13 + (i >> 4) * 5);
    for (int i = 0; i < (int)sizeof stipple; ++i) stipple[i] = (GLubyte)(0x3C ^ (i * 11));
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint lists = glGenLists(4);

    /* Bitmaps and pixels with skips, row length, LSB first and alignment. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 40);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);
    glNewList(lists, list_mode);
    glColor3f(1, 1, 0);
    glRasterPos2i(1, 1);
    glBitmap(13, 9, 0, 0, 16, 2, bits);
    glColor3f(0, 1, 1);
    glBitmap(7, 11, 1, 1, 10, 0, bits + 3);
    glRasterPos2i(30, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 12);
    glDrawPixels(9, 7, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glRasterPos2i(42, 1);
    glDrawPixels(5, 9, GL_RGB, GL_UNSIGNED_BYTE, pixels + 1);
    glEndList();
    /* The unpack state and the client memory change before the list runs. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    memset(bits, 0xFF, sizeof bits);
    memset(pixels, 0x40, sizeof pixels);
    glCallList(lists);

    /* A polygon stipple and texture images from a pixel unpack buffer. */
    for (int i = 0; i < (int)sizeof pixels; ++i) pixels[i] = (GLubyte)(255 - i * 7);
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof stipple + sizeof pixels, NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, sizeof stipple, stipple);
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER, sizeof stipple, sizeof pixels, pixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glNewList(lists + 1, list_mode);
    glPolygonStipple((const GLubyte *)0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const GLvoid *)sizeof stipple);
    glEndList();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    /* The buffer's contents change before the list runs. */
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    memset(stipple, 0, sizeof stipple);
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, sizeof stipple, stipple);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glCallList(lists + 1);
    glEnable(GL_POLYGON_STIPPLE);
    glColor3f(1, 0, 0.5f);
    glRectf(0, 20, 30, 40);
    glDisable(GL_POLYGON_STIPPLE);
    glColor3f(1, 1, 1);
    textured_quad(32, 20, 48, 36);

    /* A sub-image with swapped bytes, compiled then the source changes. */
    GLushort shorts[4 * 4 * 4];
    for (int i = 0; i < 64; ++i) shorts[i] = (GLushort)(i * 0x0F31 + 0x1200);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    glNewList(lists + 2, list_mode);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 2, 4, 4, GL_RGBA, GL_UNSIGNED_SHORT, shorts);
    glEndList();
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    memset(shorts, 0, sizeof shorts);
    glCallList(lists + 2);
    textured_quad(48, 20, 64, 36);

    /* A list calling the bitmap list twice from another raster position. */
    glNewList(lists + 3, GL_COMPILE);
    glRasterPos2i(2, 44);
    glCallList(lists);
    glRasterPos2i(2, 54);
    glCallList(lists);
    glEndList();
    glCallList(lists + 3);

    glDeleteLists(lists, 4);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(1, &texture);
}
