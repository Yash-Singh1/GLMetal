/* All depth compare functions against custom borders. Each 64x64 image has
 * four quadrants: borders .25/.75 in nearest/linear filtering. Horizontal
 * samples cross both texture edges; seven reference bands include values
 * outside [0,1], which distinguish fixed and floating depth rules. */
#include "glc_gl_core.h"
#include "glcompare.h"

static const char *const border_compare_names[] = {
    "depth24_never", "depth24_less", "depth24_equal", "depth24_lequal",
    "depth24_greater", "depth24_notequal", "depth24_gequal", "depth24_always",
    "depth32f_never", "depth32f_less", "depth32f_equal", "depth32f_lequal",
    "depth32f_greater", "depth32f_notequal", "depth32f_gequal", "depth32f_always"
};

GLC_CASE_VARIANTS(core_shadow_border_compare, border_compare_names, .profile = GLC_CORE, .tolerance = 2)
{
    const char *vertex =
        "#version 150 core\nin vec2 position;out vec2 uv;"
        "void main(){gl_Position=vec4(position,0,1);uv=position*.5+.5;}";
    const char *fragment =
        "#version 150 core\nuniform sampler2DShadow atlas;in vec2 uv;out vec4 color;"
        "void main(){int band=min(int(uv.y*7.),6);"
        "float r=band==0?-.125:band==1?0.:band==2?.25:band==3?.5:band==4?.75:band==5?1.:1.125;"
        "vec2 p=vec2(uv.x*1.5-.25,.5);float q=band<3?.5:2.;"
        "color=vec4(textureLod(atlas,vec3(p,r),0.),textureProj(atlas,vec4(p*q,r*q,q)),0,1);}";
    const char *attributes[] = {"position", NULL};
    GLuint program = glc_program(vertex, fragment, attributes);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "atlas"), 0);
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
    glBindTexture(GL_TEXTURE_2D, texture);
    GLfloat depths[16];
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x)
        depths[y * 4 + x] = x < 2 ? 0.f : 1.f;
    glTexImage2D(GL_TEXTURE_2D, 0, glc_variant < 8 ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT32F,
                 4, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_NEVER + (glc_variant & 7));
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    for (unsigned quadrant = 0; quadrant < 4; ++quadrant) {
        GLenum filter = quadrant & 1 ? GL_LINEAR : GL_NEAREST;
        GLfloat border = quadrant < 2 ? .25f : .75f;
        GLfloat color[] = {border, border, border, border};
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color);
        glViewport((quadrant & 1) * (glc_width / 2), (quadrant >> 1) * (glc_height / 2),
                   glc_width / 2, glc_height / 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    GLenum error = glGetError();
    if (error) glc_fail("Shadow border comparison variant %u error %x", glc_variant, error);
    glViewport(0, 0, glc_width, glc_height);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
}
