/* Legacy-profile features tools/apple_enum_sweep.c found GLMetal
   lacking: point sprites replacing texture coordinates (GL_COORD_REPLACE,
   GL_POINT_SPRITE_COORD_ORIGIN, NV_point_sprite's r mode),
   ATI_texture_env_combine3, APPLE_specular_vector, ARB program matrices
   (glMatrixMode(GL_MATRIXn_ARB), state.matrix.program[n]) and
   ARB_color_buffer_float's vertex and fragment colour clamping. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>

/* An 8x8 texture: s in red, t in green, a checker in blue. */
static GLuint coordinate_texture(void)
{
    unsigned char texels[8 * 8 * 4];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            unsigned char *p = texels + (y * 8 + x) * 4;
            p[0] = (unsigned char)(x * 32 + 16);
            p[1] = (unsigned char)(y * 32 + 16);
            p[2] = ((x ^ y) & 1) ? 255 : 0;
            p[3] = 255;
        }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

static const char *const sprite_names[] = {"upper_left", "lower_left", "no_replace", "sprite_disabled", "second_unit",
                                           "q_two", "r_mode_s", "polygon_points"};
GLC_CASE_VARIANTS(point_sprite_coord_replace, sprite_names, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    int unit = glc_variant == 4 ? 1 : 0;
    glActiveTexture(GL_TEXTURE0 + unit);
    GLuint texture = coordinate_texture();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    if (glc_variant != 2) glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
    if (glc_variant == 1) glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT);
    if (glc_variant == 6) glPointParameteri(GL_POINT_SPRITE_R_MODE_NV, GL_S);
    if (glc_variant != 3) glEnable(GL_POINT_SPRITE);
    glPointSize(16);
    float q = glc_variant == 5 ? 2 : 1;
    glMultiTexCoord4f(GL_TEXTURE0 + unit, 0.3f * q, 0.6f * q, 0, q);
    if (glc_variant == 7) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        glBegin(GL_TRIANGLES);
        glVertex2f(-0.6f, -0.6f); glVertex2f(0.6f, -0.6f); glVertex2f(0, 0.6f);
        glEnd();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
        glBegin(GL_POINTS);
        for (int i = 0; i < 9; ++i) glVertex2f(-0.6f + 0.6f * (i % 3), -0.6f + 0.6f * (i / 3));
        glEnd();
    }
    glDisable(GL_POINT_SPRITE);
    glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_FALSE);
    glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);
    glPointParameteri(GL_POINT_SPRITE_R_MODE_NV, GL_ZERO);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
}

/* ATI_texture_env_combine3: its three functions over a texture, the
   primary colour and the constant, and the GL_ZERO / GL_ONE sources. */
static const char *const combine3_names[] = {"modulate_add", "modulate_signed_add", "modulate_subtract", "zero_one_sources",
                                             "alpha_modulate_add"};
GLC_CASE_VARIANTS(ati_texture_env_combine3, combine3_names, .profile = GLC_LEGACY, .tolerance = 1)
{
    glClearColor(0, 0, 0, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint texture = coordinate_texture();
    glEnable(GL_TEXTURE_2D);
    static const float constant[4] = {0.25f, 0.5f, 0.75f, 0.5f};
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    static const GLenum functions[] = {GL_MODULATE_ADD_ATI, GL_MODULATE_SIGNED_ADD_ATI, GL_MODULATE_SUBTRACT_ATI,
                                       GL_MODULATE_ADD_ATI, GL_REPLACE};
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, (GLint)functions[glc_variant]);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, glc_variant == 3 ? GL_ONE : GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, glc_variant == 3 ? GL_ZERO : GL_CONSTANT);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, glc_variant == 4 ? GL_MODULATE_ADD_ATI : GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_CONSTANT);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_ALPHA, GL_TEXTURE);
    glBegin(GL_QUADS);
    glColor4f(0.1f, 0.4f, 0.2f, 0.25f); glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glColor4f(0.6f, 0.1f, 0.3f, 0.75f); glTexCoord2f(1, 0); glVertex2f(1, -1);
    glColor4f(0.3f, 0.7f, 0.1f, 0.5f); glTexCoord2f(1, 1); glVertex2f(1, 1);
    glColor4f(0.2f, 0.2f, 0.8f, 1.0f); glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &texture);
}

/* APPLE_specular_vector on a tessellated lit patch, with an infinite and a
   local viewer and a directional and a positional light. */
static const char *const specular_names[] = {"off", "on", "on_local_viewer", "on_positional_light", "two_side"};
GLC_CASE_VARIANTS(apple_specular_vector, specular_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glFrustum(-0.5, 0.5, -0.5, 0.5, 0.5, 4);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    const float position[4] = {0.4f, 0.3f, glc_variant == 3 ? 0.2f : 1.0f, glc_variant == 3 ? 1.0f : 0.0f};
    static const float white[4] = {1, 1, 1, 1}, dim[4] = {0.2f, 0.25f, 0.4f, 1}, black[4] = {0, 0, 0, 1};
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dim);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 12);
    if (glc_variant) glLightModeli(GL_LIGHT_MODEL_SPECULAR_VECTOR_APPLE, GL_TRUE);
    if (glc_variant == 2 || glc_variant == 3) glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    if (glc_variant == 4) glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    for (int y = 0; y < 32; ++y) {
        glBegin(GL_QUAD_STRIP);
        for (int x = 0; x <= 32; ++x) {
            float px = x / 16.0f - 1, py0 = y / 16.0f - 1, py1 = (y + 1) / 16.0f - 1;
            /* A bumpy normal so the reflection and the half vector differ. */
            glNormal3f(0.4f * px, 0.3f * py0, 1);
            glVertex3f(px, py0, -1.2f);
            glNormal3f(0.4f * px, 0.3f * py1, 1);
            glVertex3f(px, py1, -1.2f);
        }
        glEnd();
    }
    glLightModeli(GL_LIGHT_MODEL_SPECULAR_VECTOR_APPLE, GL_FALSE);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
}

/* ARB program matrices: a vertex program transforms by program matrix 2
   (rows, transposed rows, inverse rows) after glMatrixMode(GL_MATRIX2_ARB)
   loads and multiplies it; other program matrices stay identity. */
static const char *const matrix_names[] = {"rows", "transpose", "inverse", "invtrans", "identity_unused", "load_mult"};
GLC_CASE_VARIANTS(arb_program_matrix, matrix_names, .profile = GLC_LEGACY, .tolerance = 1)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_MATRIX2_ARB);
    glLoadIdentity();
    glRotatef(25, 0, 0, 1);
    glScalef(0.7f, 0.5f, 1);
    if (glc_variant == 5) glTranslatef(0.2f, -0.1f, 0);
    glMatrixMode(GL_MODELVIEW);
    static const char *const modifiers[] = {"", ".transpose", ".inverse", ".invtrans", "", ""};
    char source[1024];
    snprintf(source, sizeof source,
             "!!ARBvp1.0\nPARAM m[4] = { state.matrix.program[%d]%s };\nTEMP p;\n"
             "DP4 p.x, m[0], vertex.position;\nDP4 p.y, m[1], vertex.position;\nDP4 p.z, m[2], vertex.position;\n"
             "DP4 p.w, m[3], vertex.position;\nMOV result.position, p;\nMOV result.color, vertex.color;\nEND\n",
             glc_variant == 4 ? 5 : 2, modifiers[glc_variant]);
    GLuint program = glc_arb_program(GL_VERTEX_PROGRAM_ARB, source);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(-0.8f, -0.6f);
    glColor3f(0, 1, 0); glVertex2f(0.8f, -0.4f);
    glColor3f(0, 0, 1); glVertex2f(0.1f, 0.9f);
    glEnd();
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
    glMatrixMode(GL_MATRIX2_ARB);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
}

/* ARB_color_buffer_float clamping: colours above 1 into a half-float
   target (shown at a quarter), from fixed function and from an ARB
   fragment program, under each vertex and fragment clamp setting. */
static const char *const clamp_names[] = {"defaults",        "vertex_false",          "vertex_fixed_only",
                                          "fragment_true",   "fragment_false",        "both_false",
                                          "program_default", "program_fragment_true", "texture_modulate_unclamped",
                                          "texture_add_unclamped"};
GLC_CASE_VARIANTS(color_buffer_float_clamp, clamp_names, .profile = GLC_LEGACY, .tolerance = 1)
{
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, 16, 16, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glViewport(0, 0, 16, 16);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const GLenum vertex_clamps[] = {GL_TRUE, GL_FALSE, GL_FIXED_ONLY_ARB, GL_TRUE, GL_TRUE,
                                           GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE};
    static const GLenum fragment_clamps[] = {GL_FIXED_ONLY_ARB, GL_FIXED_ONLY_ARB, GL_FIXED_ONLY_ARB, GL_TRUE, GL_FALSE,
                                             GL_FALSE, GL_FIXED_ONLY_ARB, GL_TRUE, GL_FALSE, GL_FALSE};
    /* A texture of 0.75 grey modulated with or added to the colours. */
    GLuint grey = 0;
    if (glc_variant >= 8) {
        static const unsigned char texel[4] = {191, 191, 191, 255};
        glGenTextures(1, &grey);
        glBindTexture(GL_TEXTURE_2D, grey);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, glc_variant == 8 ? GL_MODULATE : GL_ADD);
    }
    glClampColorARB(GL_CLAMP_VERTEX_COLOR_ARB, vertex_clamps[glc_variant]);
    glClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB, fragment_clamps[glc_variant]);
    GLuint program = 0;
    if (glc_variant >= 6)
        program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB,
                                  "!!ARBfp1.0\nMUL result.color, fragment.color, {3, 2, 1.5, 1};\nEND\n");
    glBegin(GL_QUADS);
    glColor4f(2.0f, 1.5f, 0.5f, 1); glVertex2f(-1, -1);
    glColor4f(3.0f, 0.5f, 1.25f, 1); glVertex2f(1, -1);
    glColor4f(1.0f, 2.5f, 3.5f, 1); glVertex2f(1, 1);
    glColor4f(0.5f, 0.5f, 4.0f, 1); glVertex2f(-1, 1);
    glEnd();
    if (program) {
        glDisable(GL_FRAGMENT_PROGRAM_ARB);
        glDeleteProgramsARB(1, &program);
    }
    if (grey) {
        glDisable(GL_TEXTURE_2D);
        glDeleteTextures(1, &grey);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    glClampColorARB(GL_CLAMP_VERTEX_COLOR_ARB, GL_TRUE);
    glClampColorARB(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_FIXED_ONLY_ARB);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
    glViewport(0, 0, glc_width, glc_height);
    glColor4f(0.25f, 0.25f, 0.25f, 1);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
}
