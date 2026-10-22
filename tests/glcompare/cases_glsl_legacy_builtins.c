/* GLSL 1.20 built-ins tests/probes/glsl_compile.c found GLMetal lacking:
   the 1D, projective and shadow lookups with a bias (fragment shaders),
   texture1DProj's vec2 form, texture1DProjLod, texture3DProjLod, the
   EXT_texture_array 1D-array forms (texture1DArrayLod, shadow1DArray),
   ARB_draw_instanced's gl_InstanceIDARB and EXT_gpu_shader4's `varying
   out` without gl_FragColor. noise1..noise4 compile but return 0, which
   GLSL 1.20 allows; Apple's GL returns gradient noise, so they are
   checked only by the compile sweep. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>

/* A colour per mipmap level. */
static void level_color(unsigned char *t, int level)
{
    t[0] = (unsigned char)(255 - level * 40);
    t[1] = (unsigned char)(level * 40);
    t[2] = (unsigned char)((level & 1) * 200);
    t[3] = 255;
}

static void make_textures(void)
{
    static unsigned char texels[32 * 32 * 4 * 4];
    GLuint t[6];
    glGenTextures(6, t);
    /* unit 0: 1D 32 texels, a colour per level */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_1D, t[0]);
    for (int level = 0, size = 32; size; ++level, size /= 2) {
        for (int i = 0; i < size; ++i) level_color(texels + i * 4, level);
        glTexImage1D(GL_TEXTURE_1D, level, GL_RGBA8, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 1: 2D depth 16x16 (depth = s), mipmapped, compared */
    static float depth[16 * 16];
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t[1]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size * size; ++i) depth[i] = ((i % size) + 0.5f) / size * (level ? 0.6f : 1.0f);
        glTexImage2D(GL_TEXTURE_2D, level, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    /* unit 2: 1D depth 16, compared */
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_1D, t[2]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size; ++i) depth[i] = (i + 0.5f) / size * (level ? 0.6f : 1.0f);
        glTexImage1D(GL_TEXTURE_1D, level, GL_DEPTH_COMPONENT24, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    }
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    /* unit 3: 1D array 16x4, a colour per level */
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_1D_ARRAY_EXT, t[3]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size * 4; ++i) {
            level_color(texels + i * 4, level);
            texels[i * 4 + 2] = (unsigned char)((i / size) * 60);
        }
        glTexImage2D(GL_TEXTURE_1D_ARRAY_EXT, level, GL_RGBA8, size, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    glTexParameteri(GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    /* unit 4: 1D array depth 16x4 (depth = s / layer), compared */
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_1D_ARRAY_EXT, t[4]);
    for (int i = 0; i < 16 * 4; ++i) depth[i] = ((i % 16) + 0.5f) / 16 / (1 + i / 16);
    glTexImage2D(GL_TEXTURE_1D_ARRAY_EXT, 0, GL_DEPTH_COMPONENT24, 16, 4, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    glTexParameteri(GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D_ARRAY_EXT, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    /* unit 5: 3D 16^3, a colour per level */
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, t[5]);
    for (int level = 0, size = 16; size; ++level, size /= 2) {
        for (int i = 0; i < size * size * size; ++i) level_color(texels + i * 4, level);
        glTexImage3D(GL_TEXTURE_3D, level, GL_RGBA8, size, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glActiveTexture(GL_TEXTURE0);
}

static const char vertex_shader[] =
    "#version 120\nvarying vec2 uv;\nvoid main() { uv = gl_Vertex.xy * 0.5 + 0.5; gl_Position = gl_Vertex; }\n";

static const char *const names[] = {
    "texture1D_bias", "texture1DProj_vec2", "texture1DProj_vec4_bias", "texture1DProjLod", "shadow2D_bias",
    "shadow2DProj_bias", "shadow1D_bias", "shadow1DProjLod", "texture1DArrayLod", "shadow1DArray",
    "shadow1DArray_bias", "texture3DProjLod",
};
static const char *const bodies[] = {
    "gl_FragColor = texture1D(s0, uv.x, uv.y * 4.0);",
    "gl_FragColor = texture1DProj(s0, vec2(uv.x * 2.0, 2.0));",
    "gl_FragColor = texture1DProj(s0, vec4(uv.x * 3.0, 9.0, 9.0, 3.0), uv.y * 3.0);",
    "gl_FragColor = texture1DProjLod(s0, vec4(uv.x * 2.0, 0.0, 0.0, 2.0), uv.y * 5.0);",
    "gl_FragColor = shadow2D(s1, vec3(uv, 0.5), uv.y * 3.0) * vec4(1.0, 0.5, 0.25, 1.0);",
    "gl_FragColor = shadow2DProj(s1, vec4(uv * 2.0, 1.0, 2.0), 2.0);",
    "gl_FragColor = shadow1D(s2, vec3(uv.x, 0.0, uv.y), uv.y * 2.0);",
    "gl_FragColor = shadow1DProjLod(s2, vec4(uv.x * 2.0, 0.0, 0.7, 2.0), floor(uv.y * 2.0));",
    "gl_FragColor = texture1DArrayLod(s3, vec2(uv.x, floor(uv.y * 4.0)), uv.x * 4.0);",
    "gl_FragColor = shadow1DArray(s4, vec3(uv.x, floor(uv.y * 4.0), 0.2));",
    "gl_FragColor = shadow1DArray(s4, vec3(uv.x, floor(uv.y * 4.0), 0.2), 1.0);",
    "gl_FragColor = texture3DProjLod(s5, vec4(uv * 2.0, 0.5, 2.0), uv.x * 4.0);",
};

GLC_CASE_VARIANTS(glsl_legacy_builtins, names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    make_textures();
    char fragment[1024];
    snprintf(fragment, sizeof fragment,
             "#version 120\n#extension GL_EXT_texture_array : enable\n#extension GL_EXT_gpu_shader4 : enable\n"
             "uniform sampler1D s0;\nuniform sampler2DShadow s1;\nuniform sampler1DShadow s2;\nuniform sampler1DArray s3;\n"
             "uniform sampler1DArrayShadow s4;\nuniform sampler3D s5;\nvarying vec2 uv;\nvoid main() { %s }\n",
             bodies[glc_variant]);
    GLuint program = glc_program(vertex_shader, fragment, NULL);
    for (int u = 0; u < 6; ++u) {
        char sampler[4] = {'s', (char)('0' + u), 0};
        glUniform1i(glGetUniformLocation(program, sampler), u);
    }
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
    glUseProgram(0);
    glDeleteProgram(program);
}

/* gl_InstanceIDARB: four instances of a quad, each placed and coloured by
   its id. */
GLC_CASE(glsl_instance_id_arb, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint program = glc_program(
        "#version 120\n#extension GL_ARB_draw_instanced : enable\nvarying vec4 color;\n"
        "void main() { float i = float(gl_InstanceIDARB);\n"
        "  gl_Position = vec4(gl_Vertex.xy * 0.4 + vec2(mod(i, 2.0) - 0.5, floor(i / 2.0) - 0.5), 0.0, 1.0);\n"
        "  color = vec4(i / 3.0, 1.0 - i / 3.0, mod(i, 2.0), 1.0); }\n",
        "#version 120\nvarying vec4 color;\nvoid main() { gl_FragColor = color; }\n", NULL);
    static const float quad[] = {-1, -1, 1, -1, 1, 1, -1, 1};
    glVertexPointer(2, GL_FLOAT, 0, quad);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDrawArraysInstancedARB(GL_QUADS, 0, 4, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glUseProgram(0);
    glDeleteProgram(program);
}

/* EXT_gpu_shader4 `varying out`, located by glBindFragDataLocationEXT or
   left to the linker, with no gl_FragColor. */
static const char *const output_names[] = {"bound", "unbound"};
GLC_CASE_VARIANTS(glsl_varying_out_only, output_names, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const char *vertex = "#version 120\nvarying vec2 uv;\nvoid main() { uv = gl_Vertex.xy * 0.5 + 0.5; gl_Position = gl_Vertex; }\n";
    const char *fragment = "#version 120\n#extension GL_EXT_gpu_shader4 : enable\nvarying vec2 uv;\nvarying out vec4 color;\n"
                           "void main() { color = vec4(uv, 0.5, 1.0); }\n";
    GLuint program = glCreateProgram();
    const char *sources[2] = {vertex, fragment};
    for (int stage = 0; stage < 2; ++stage) {
        GLuint shader = glCreateShader(stage ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
        glShaderSource(shader, 1, &sources[stage], NULL);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof log, NULL, log);
            glc_fail("%s shader: %s", stage ? "fragment" : "vertex", log);
        }
        glAttachShader(program, shader);
    }
    if (glc_variant == 0) glBindFragDataLocationEXT(program, 0, "color");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) glc_fail("link failed");
    glUseProgram(program);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
    glUseProgram(0);
    glDeleteProgram(program);
}
