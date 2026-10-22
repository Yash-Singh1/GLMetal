/* NV_fog_distance: GL_FOG_DISTANCE_MODE_NV picks the fog distance of
   fixed-function vertex processing: EYE_PLANE_ABSOLUTE_NV (the default,
   |ze|), EYE_PLANE (signed) or EYE_RADIAL_NV (|eye position|). Each
   variant draws a slab crossing the eye plane under an orthographic
   projection (top half: signed and absolute distances differ behind the
   eye) and a perspective floor reaching far to the sides (bottom half:
   radial and planar distances differ), with linear, exp and exp2 fog, the
   fog coordinate source (which ignores the mode), and ARB fragment
   programs with the ARB_fog options after fixed-function vertex
   processing. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static const GLenum distance_modes[] = {0, GL_EYE_PLANE_ABSOLUTE_NV, GL_EYE_PLANE, GL_EYE_RADIAL_NV};

static void fog_scene(GLenum fog_mode)
{
    glFogi(GL_FOG_MODE, (GLint)fog_mode);
    glFogf(GL_FOG_START, 1.5f);
    glFogf(GL_FOG_END, 9.0f);
    glFogf(GL_FOG_DENSITY, fog_mode == GL_EXP2 ? 0.2f : 0.3f);
    static const float fog_color[4] = {0.9f, 0.8f, 0.3f, 1};
    glFogfv(GL_FOG_COLOR, fog_color);
    glEnable(GL_FOG);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Top: eye z from +6 to -6 left to right (ortho, near -8, far 8). */
    glViewport(0, glc_height / 2, glc_width, glc_height / 2);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -8, 8);
    glBegin(GL_QUADS);
    glColor3f(0.1f, 0.3f, 0.8f);
    glFogCoordf(2);
    glVertex3f(-1, -1, 6); glVertex3f(1, -1, -6);
    glFogCoordf(7);
    glVertex3f(1, 1, -6); glVertex3f(-1, 1, 6);
    glEnd();

    /* Bottom: a floor 1 below the eye, 16 wide, from 1 to 12 deep. */
    glViewport(0, 0, glc_width, glc_height / 2);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.25, 0.25, 0.5, 20);
    glBegin(GL_QUADS);
    glColor3f(0.2f, 0.7f, 0.3f);
    glFogCoordf(1);
    glVertex3f(-8, -1, -1); glVertex3f(8, -1, -1);
    glFogCoordf(8);
    glVertex3f(8, -1, -12); glVertex3f(-8, -1, -12);
    glEnd();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glViewport(0, 0, glc_width, glc_height);
    glDisable(GL_FOG);
}

static const char *const fixed_names[] = {
    "default_linear", "absolute_linear", "plane_linear", "radial_linear", "default_exp", "absolute_exp",
    "plane_exp", "radial_exp", "default_exp2", "absolute_exp2", "plane_exp2", "radial_exp2",
};
GLC_CASE_VARIANTS(nv_fog_distance, fixed_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLenum mode = distance_modes[glc_variant % 4];
    if (mode) glFogi(GL_FOG_DISTANCE_MODE_NV, (GLint)mode);
    static const GLenum fog_modes[] = {GL_LINEAR, GL_EXP, GL_EXP2};
    fog_scene(fog_modes[glc_variant / 4]);
}

/* The fog coordinate source ignores the distance mode. */
static const char *const coord_names[] = {"absolute", "plane", "radial"};
GLC_CASE_VARIANTS(nv_fog_distance_coord_source, coord_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFogi(GL_FOG_DISTANCE_MODE_NV, (GLint)distance_modes[glc_variant + 1]);
    glFogi(GL_FOG_COORD_SRC, GL_FOG_COORD);
    fog_scene(GL_LINEAR);
    glFogi(GL_FOG_COORD_SRC, GL_FRAGMENT_DEPTH);
}

/* ARB fragment programs with OPTION ARB_fog_*: the distance comes from
   fixed-function vertex processing. */
static const char *const program_names[] = {
    "absolute_linear", "plane_linear", "radial_linear", "absolute_exp", "plane_exp", "radial_exp",
    "absolute_exp2", "plane_exp2", "radial_exp2",
};
GLC_CASE_VARIANTS(nv_fog_distance_arb_fog, program_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFogi(GL_FOG_DISTANCE_MODE_NV, (GLint)distance_modes[glc_variant % 3 + 1]);
    static const char *const sources[] = {
        "!!ARBfp1.0\nOPTION ARB_fog_linear;\nMOV result.color, fragment.color;\nEND\n",
        "!!ARBfp1.0\nOPTION ARB_fog_exp;\nMOV result.color, fragment.color;\nEND\n",
        "!!ARBfp1.0\nOPTION ARB_fog_exp2;\nMOV result.color, fragment.color;\nEND\n",
    };
    static const GLenum fog_modes[] = {GL_LINEAR, GL_EXP, GL_EXP2};
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, sources[glc_variant / 3]);
    fog_scene(fog_modes[glc_variant / 3]);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}

/* A fragment program reading fragment.fogcoord (the distance, unfogged). */
static const char *const read_names[] = {"absolute", "plane", "radial"};
GLC_CASE_VARIANTS(nv_fog_distance_fogcoord_read, read_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFogi(GL_FOG_DISTANCE_MODE_NV, (GLint)distance_modes[glc_variant + 1]);
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB,
                                     "!!ARBfp1.0\nTEMP t;\nMUL t, fragment.fogcoord.x, {0.1, -0.1, 0.05, 0};\n"
                                     "ADD result.color, t, {0, 0.6, 0.5, 1};\nEND\n");
    fog_scene(GL_LINEAR);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}
