/* Pixel rectangles' fragments (GL 2.1 3.6.4, 3.7, 4.1): glDrawPixels,
   glCopyPixels and glBitmap are textured with the raster texture
   coordinates, fogged by the raster distance, alpha tested, depth tested,
   blended and put through logic ops like any fragment; with a GLSL program
   the rectangle bypasses those (variant "glsl"). */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static const char *const variants[] = {"texture", "fog_alpha", "logic_depth", "glsl"};
GLC_CASE_VARIANTS(pixel_fragments, variants, .profile = GLC_LEGACY, .tolerance = 1)
{
    glClearColor(0.1f, 0.2f, 0.3f, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    GLubyte image[12 * 10 * 4], bits[16 * 4];
    for (int i = 0; i < 12 * 10; ++i) {
        image[i * 4 + 0] = (GLubyte)(i * 23);
        image[i * 4 + 1] = (GLubyte)(255 - i * 9);
        image[i * 4 + 2] = (GLubyte)(i * 51);
        image[i * 4 + 3] = (GLubyte)(i * 13);
    }
    for (int i = 0; i < (int)sizeof bits; ++i) bits[i] = (GLubyte)(0x5A ^ (i * 37));
    /* A 2x2 texture the raster texture coordinates pick from. */
    const GLubyte texels[] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 255, 255, 0, 64};
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLuint program = 0;
    switch (glc_variant) {
    case 0:
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        break;
    case 1: {
        const GLfloat fog[] = {0.9f, 0.6f, 0.1f, 1};
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 0);
        glFogf(GL_FOG_END, 8);
        glFogfv(GL_FOG_COLOR, fog);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.4f);
        break;
    }
    case 2:
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glColor3f(0.5f, 0.5f, 0.5f);
        glRectf(0, 0, 64, 32); /* at depth 0.5: rectangles in front pass */
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);
        break;
    case 3: {
        const char *fs = "void main() { gl_FragColor = vec4(gl_Color.bgr, 1.0); }";
        program = glCreateProgram();
        GLuint shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(shader, 1, &fs, NULL);
        glCompileShader(shader);
        glAttachShader(program, shader);
        glDeleteShader(shader);
        glLinkProgram(program);
        glUseProgram(program);
        break;
    }
    }
    /* Raster positions: different texture coordinates and eye depths. */
    const float spots[4][4] = {{2, 2, 0.25f, 0.25f}, {20, 2, 0.75f, 0.25f}, {2, 34, 0.25f, 0.75f}, {20, 34, 0.75f, 0.75f}};
    for (int i = 0; i < 4; ++i) {
        glTexCoord2f(spots[i][2], spots[i][3]);
        glColor4f(1, 0.8f, 0.6f, 0.9f);
        glRasterPos3f(spots[i][0], spots[i][1], -1.0f - 2.0f * (float)i);
        glPixelZoom(1 + (float)i * 0.5f, 1);
        glDrawPixels(12, 10, GL_RGBA, GL_UNSIGNED_BYTE, image);
    }
    glPixelZoom(1, 1);
    /* Bitmaps with the raster colour. */
    for (int i = 0; i < 4; ++i) {
        glTexCoord2f(spots[i][2], spots[i][3]);
        glColor4f(0.3f + 0.2f * (float)i, 1, 0.5f, 0.6f + 0.1f * (float)i);
        glRasterPos3f(44, 2 + 14 * (float)i, -2.0f * (float)i);
        glBitmap(16, 4, 0, 0, 0, 0, bits);
        glBitmap(16, 4, 0, -5, 0, 0, bits + 2);
    }
    /* Copies of what is drawn (through the same fragment operations). */
    glTexCoord2f(0.75f, 0.75f);
    glRasterPos3f(2, 50, -3);
    glCopyPixels(2, 2, 30, 12, GL_COLOR);
    glRasterPos3f(36, 50, -1);
    glCopyPixels(44, 2, 20, 12, GL_COLOR);
    glUseProgram(0);
    if (program) glDeleteProgram(program);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDeleteTextures(1, &texture);
}
