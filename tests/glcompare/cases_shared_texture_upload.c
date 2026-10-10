/* Small uploads from a non-presenting shared context must keep their bytes
 * alive after submission and after the producer context is destroyed. */
#include <OpenGL/OpenGL.h>
#include "glc_gl_core.h"
#include "glcompare.h"
#include <string.h>

extern void *glc_lookup(const char *name);

GLC_CASE(core_shared_texture_upload, .profile = GLC_CORE, .width = 96, .height = 64, .tolerance = 1)
{
    CGLContextObj (*get_current)(void) = glc_lookup("CGLGetCurrentContext");
    CGLPixelFormatObj (*get_format)(CGLContextObj) = glc_lookup("CGLGetPixelFormat");
    CGLError (*create)(CGLPixelFormatObj, CGLContextObj, CGLContextObj *) = glc_lookup("CGLCreateContext");
    CGLError (*set_current)(CGLContextObj) = glc_lookup("CGLSetCurrentContext");
    CGLError (*destroy)(CGLContextObj) = glc_lookup("CGLDestroyContext");
    if (!get_current || !get_format || !create || !set_current || !destroy) {
        glc_fail("Shared texture test lacks CGL context functions"); return;
    }
    CGLContextObj consumer = get_current(), producer = NULL;
    if (create(get_format(consumer), consumer, &producer) != kCGLNoError) {
        glc_fail("Cannot create texture upload producer"); return;
    }
    GLuint texture = 0, program = 0, vao = 0;
    const char *error = NULL;
    if (set_current(producer) != kCGLNoError) { error = "Cannot select texture upload producer"; goto cleanup; }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 2);
    unsigned char pixels[4 * 4 * 4], expected[8][3][4];
    for (int mip = 0; mip < 3; ++mip)
        glTexImage3D(GL_TEXTURE_2D_ARRAY, mip, GL_RGBA8, 4 >> mip, 4 >> mip, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    for (int round = 0; round < 4; ++round)
        for (int layer = 0; layer < 8; ++layer)
            for (int mip = 0; mip < 3; ++mip) {
                unsigned char color[4] = {17 + layer * 23, 31 + mip * 71, 41 + round * 53, 255};
                for (int texel = 0; texel < 16; ++texel) memcpy(pixels + texel * 4, color, 4);
                memcpy(expected[layer][mip], color, 4);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip, 0, 0, layer, 4 >> mip, 4 >> mip, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                memset(pixels, 0, sizeof pixels);
            }
    glFlush(); /* Publish without waiting for producer completion. */
    if (set_current(consumer) != kCGLNoError) { error = "Cannot restore texture upload consumer"; goto cleanup; }
    destroy(producer); producer = NULL;
    const char *vertex = "#version 150 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
    const char *fragment = "#version 150 core\nuniform sampler2DArray image;uniform int layer,mip;out vec4 color;void main(){color=texelFetch(image,ivec3(0,0,layer),mip);}";
    program = glc_program(vertex, fragment, NULL);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glUseProgram(program); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
    for (int layer = 0; layer < 8; ++layer)
        for (int mip = 0; mip < 3; ++mip) {
            int tile = layer * 3 + mip, x = tile % 6 * 16, y = tile / 6 * 16;
            glViewport(x, y, 16, 16);
            glUniform1i(glGetUniformLocation(program, "layer"), layer);
            glUniform1i(glGetUniformLocation(program, "mip"), mip);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unsigned char actual[4]; glReadPixels(x + 8, y + 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
            if (memcmp(actual, expected[layer][mip], 4)) { error = "Shared loader texture bytes changed after producer destruction"; goto cleanup; }
        }
    if (glGetError()) error = "Shared loader texture GL error";
cleanup:
    set_current(consumer);
    if (producer) destroy(producer);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
    if (texture) glDeleteTextures(1, &texture);
    if (error) glc_fail("%s", error);
}
