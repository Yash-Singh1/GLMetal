/* Compressed mip data must survive uploads before the base level exists.
 * Every mip has a distinct solid color. The five strips select levels 0-4
 * by minification, then each level's compressed bytes are read back. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *const compressed_orders[] = {
    "dxt1_forward", "dxt1_reverse", "dxt5_forward", "dxt5_reverse"
};
static const uint16_t mip_colors[] = {0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f};
static const unsigned char expected_colors[][4] = {
    {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
    {255, 255, 0, 255}, {255, 0, 255, 255}
};

static GLsizei compressed_mip(unsigned char *data, int level, int block_bytes)
{
    int size = 16 >> level;
    int blocks = ((size + 3) / 4) * ((size + 3) / 4);
    memset(data, 0, (size_t)blocks * block_bytes);
    for (int i = 0; i < blocks; ++i) {
        unsigned char *block = data + i * block_bytes;
        unsigned char *color = block + (block_bytes == 16 ? 8 : 0);
        if (block_bytes == 16) block[0] = block[1] = 255;
        color[0] = (unsigned char)mip_colors[level];
        color[1] = (unsigned char)(mip_colors[level] >> 8);
    }
    return blocks * block_bytes;
}

GLC_CASE_VARIANTS(texture_compressed_level_order, compressed_orders,
                  .profile = GLC_LEGACY, .width = 80, .height = 32, .tolerance = 1)
{
    int block_bytes = glc_variant < 2 ? 8 : 16;
    GLenum format = block_bytes == 8 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    unsigned char data[16 * 16], actual[sizeof data];
    for (int step = 0; step < 5; ++step) {
        int level = glc_variant & 1 ? 4 - step : step;
        int size = 16 >> level;
        GLsizei bytes = compressed_mip(data, level, block_bytes);
        glCompressedTexImage2D(GL_TEXTURE_2D, level, format, size, size, 0, bytes, data);
        memset(data, 0xcd, sizeof data);
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int level = 0; level < 5; ++level) {
        GLfloat scale = (GLfloat)(1 << level);
        glViewport(level * 16, 0, 16, glc_height);
        glBegin(GL_QUADS);
        glTexCoord2f(0, .5f); glVertex2f(-1, -1);
        glTexCoord2f(scale, .5f); glVertex2f(1, -1);
        glTexCoord2f(scale, .5f); glVertex2f(1, 1);
        glTexCoord2f(0, .5f); glVertex2f(-1, 1);
        glEnd();
    }
    for (int level = 0; level < 5; ++level) {
        unsigned char pixel[4];
        glReadPixels(level * 16 + 8, glc_height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        for (int c = 0; c < 4; ++c)
            if (abs((int)pixel[c] - expected_colors[level][c]) > 1)
                glc_fail("Compressed %s mip %d channel %d expected %u got %u",
                         compressed_orders[glc_variant], level, c, expected_colors[level][c], pixel[c]);
        GLsizei bytes = compressed_mip(data, level, block_bytes);
        memset(actual, 0xcd, sizeof actual);
        glGetCompressedTexImage(GL_TEXTURE_2D, level, actual);
        if (memcmp(data, actual, (size_t)bytes))
            glc_fail("Compressed %s mip %d lost uploaded bytes", compressed_orders[glc_variant], level);
    }
    GLenum error = glGetError();
    if (error) glc_fail("Compressed %s GL error %x", compressed_orders[glc_variant], error);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &texture);
    glViewport(0, 0, glc_width, glc_height);
}
