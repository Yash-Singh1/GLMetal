/* Upload/draw ordering across a 4 MiB staging chunk boundary. Nine 512 KiB
 * images stay in one command buffer, followed by a flush and seven smaller
 * updates. The final image retains every earlier draw, so overwriting staging
 * bytes or losing an array/volume offset changes a tile's captured colors. */
#include "glc_gl_core.h"
#include "glcompare.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *const upload_staging_names[] = {"2d", "3d", "array_2d", "array_1d"};

GLC_CASE_VARIANTS(core_texture_upload_staging, upload_staging_names, .profile = GLC_CORE, .tolerance = 1)
{
    int variant = glc_variant;
    GLenum target = variant == 0 ? GL_TEXTURE_2D : variant == 1 ? GL_TEXTURE_3D :
                    variant == 2 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_1D_ARRAY;
    int width = variant == 0 || variant == 3 ? 256 : 128;
    int height = variant == 0 || variant == 3 ? 128 : 64;
    int depth = variant == 1 || variant == 2 ? 4 : 1;
    const char *types[] = {"sampler2D", "sampler3D", "sampler2DArray", "sampler1DArray"};
    const char *samples[] = {
        "texelFetch(atlas,xy,0)", "texelFetch(atlas,ivec3(xy,layer),0)",
        "texelFetch(atlas,ivec3(xy,layer),0)", "texelFetch(atlas,ivec2(xy.x,layer),0)"
    };
    const char *vertex = "#version 150 core\nin vec2 position;out vec2 uv;"
                         "void main(){gl_Position=vec4(position,0,1);uv=position*.5+.5;}";
    char fragment[512];
    snprintf(fragment, sizeof fragment,
             "#version 150 core\nuniform %s atlas;uniform ivec4 region;uniform int layer;"
             "in vec2 uv;out vec4 color;void main(){ivec2 xy=region.xy+ivec2(uv*vec2(region.zw));color=%s;}",
             types[variant], samples[variant]);
    const char *attributes[] = {"position", NULL};
    GLuint program = glc_program(vertex, fragment, attributes);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "atlas"), 0);
    GLint region_location = glGetUniformLocation(program, "region");
    GLint layer_location = glGetUniformLocation(program, "layer");
    GLuint vao, buffer, texture;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    const GLfloat quad[] = {-1,-1, 1,-1, -1,1, 1,1};
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, texture);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLfloat *pixels = malloc((size_t)width * height * depth * 4 * sizeof *pixels);
    if (!pixels) glc_fail("Upload staging allocation failed");
    unsigned char expected[16][4];
    int tile_width = glc_width / 4, tile_height = glc_height / 4;
    for (unsigned step = 0; step < 16; ++step) {
        if (step == 9) glFlush();
        int partial = step >= 9;
        int w = partial ? 64 : width;
        int h = partial ? (variant == 3 ? 2 : 32) : height;
        int d = partial ? (variant == 1 || variant == 2 ? 2 : 1) : depth;
        int x = partial ? 7 : 0, y = partial ? 3 : 0, z = partial ? 1 : 0;
        for (int image = 0; image < d; ++image) for (int row = 0; row < h; ++row) for (int column = 0; column < w; ++column) {
            size_t at = (((size_t)image * h + row) * w + column) * 4;
            pixels[at] = (20 + (step * 11 + image * 17 + row * 3) % 200) / 255.f;
            pixels[at + 1] = (20 + (column * 3 + row * 5) % 200) / 255.f;
            pixels[at + 2] = (20 + (step * 19 + column + row + image * 29) % 200) / 255.f;
            pixels[at + 3] = 1;
        }
        if (variant == 0 || variant == 3) {
            if (partial) glTexSubImage2D(target, 0, x, y, w, h, GL_RGBA, GL_FLOAT, pixels);
            else glTexImage2D(target, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, pixels);
        } else {
            if (partial) glTexSubImage3D(target, 0, x, y, z, w, h, d, GL_RGBA, GL_FLOAT, pixels);
            else glTexImage3D(target, 0, GL_RGBA32F, w, h, d, 0, GL_RGBA, GL_FLOAT, pixels);
        }
        int layer = variant == 3 ? (partial ? y + (step & 1) : (int)(step * 13) % h) :
                    variant ? (partial ? z + (step & 1) : (int)step % d) : 0;
        glUniform1i(layer_location, layer);
        glUniform4i(region_location, x, variant == 3 ? 0 : y, w, variant == 3 ? 1 : h);
        int column = (int)(((tile_width / 2 + .5) / tile_width) * w);
        int row = variant == 3 ? layer - y : (int)(((tile_height / 2 + .5) / tile_height) * h);
        int image = variant == 1 || variant == 2 ? layer - z : 0;
        size_t probe = (((size_t)image * h + row) * w + column) * 4;
        for (int channel = 0; channel < 4; ++channel)
            expected[step][channel] = (unsigned char)lroundf(pixels[probe + channel] * 255.f);
        glViewport((step & 3) * tile_width, (step >> 2) * tile_height, tile_width, tile_height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    free(pixels);
    for (unsigned step = 0; step < 16; ++step) {
        unsigned char pixel[4];
        glReadPixels((step & 3) * tile_width + tile_width / 2,
                     (step >> 2) * tile_height + tile_height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        for (int channel = 0; channel < 4; ++channel)
            if (abs((int)pixel[channel] - expected[step][channel]) > 1)
                glc_fail("Upload staging %s step %u channel %d expected %u got %u",
                         upload_staging_names[variant], step, channel, expected[step][channel], pixel[channel]);
    }
    GLenum error = glGetError();
    if (error) glc_fail("Upload staging %s GL error %x", upload_staging_names[variant], error);
    glViewport(0, 0, glc_width, glc_height);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
}
