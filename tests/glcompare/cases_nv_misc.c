/* The smaller NV extensions Apple's OpenGL advertises, in the legacy
   profile: NV_conditional_render (the NV entry points, all four modes),
   NV_texture_barrier (sampling what an earlier draw rendered to the bound
   texture) and NV_light_max_exponent (shininess and spot exponents up to
   GL_MAX_SHININESS_NV / GL_MAX_SPOT_EXPONENT_NV, 128). NV_blend_square is
   covered by blend_src / blend_dst, NV_depth_clamp by depth_clamp_legacy. */
#include "glc_gl_legacy.h"
#include "glcompare.h"

static void rect(float x0, float y0, float x1, float y1, float z)
{
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

/* Query 0 counts a hidden rectangle (no samples), query 1 a visible one;
   rectangles drawn under each then show which draws were kept. The
   NO_WAIT modes wait for the results first, so they are known. */
static const char *const conditional_names[] = {"wait", "no_wait", "by_region_wait", "by_region_no_wait"};
GLC_CASE_VARIANTS(nv_conditional_render, conditional_names, .profile = GLC_LEGACY)
{
    static const GLenum modes[] = {GL_QUERY_WAIT_NV, GL_QUERY_NO_WAIT_NV, GL_QUERY_BY_REGION_WAIT_NV,
                                   GL_QUERY_BY_REGION_NO_WAIT_NV};
    glClearColor(0, 0, 0, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glColor3f(0.3f, 0.3f, 0.3f);
    rect(-1, -1, 1, 1, -0.5f); /* the occluder */
    GLuint queries[2];
    glGenQueries(2, queries);
    for (int q = 0; q < 2; ++q) {
        glBeginQuery(GL_SAMPLES_PASSED, queries[q]);
        glColor3f(0, 0, 1);
        rect(-0.9f + q, -0.9f, -0.1f + q, -0.1f, q ? -0.9f : 0.5f);
        glEndQuery(GL_SAMPLES_PASSED);
    }
    if (modes[glc_variant] == GL_QUERY_NO_WAIT_NV || modes[glc_variant] == GL_QUERY_BY_REGION_NO_WAIT_NV) {
        GLuint result;
        for (int q = 0; q < 2; ++q) glGetQueryObjectuiv(queries[q], GL_QUERY_RESULT, &result);
    }
    glDisable(GL_DEPTH_TEST);
    for (int q = 0; q < 2; ++q) {
        glBeginConditionalRenderNV(queries[q], modes[glc_variant]);
        glColor3f(1, 0.5f * q, 0);
        rect(-0.9f + q, 0.1f, -0.1f + q, 0.9f, 0);
        /* A clear under a failed condition is skipped too. */
        glEnable(GL_SCISSOR_TEST);
        glScissor(glc_width / 2 * q + 4, glc_height / 2 + 4, 6, 6);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glEndConditionalRenderNV();
    }
    /* Outside conditional rendering everything draws again. */
    glColor3f(1, 1, 1);
    rect(-0.05f, -0.05f, 0.05f, 0.05f, 0);
    glDeleteQueries(2, queries);
}

/* A texture is drawn into, then (after glTextureBarrierNV) sampled by draws
   into other texels of the same texture; finally shown. */
static const char *const barrier_names[] = {"copy_half", "accumulate_rows"};
GLC_CASE_VARIANTS(nv_texture_barrier, barrier_names, .profile = GLC_LEGACY, .tolerance = 1)
{
    GLuint texture, framebuffer, previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *)&previous);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glViewport(0, 0, 32, 32);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Left half: a colour ramp. */
    glBegin(GL_QUADS);
    glColor3f(1, 0, 0); glVertex2f(-1, -1);
    glColor3f(0, 1, 0); glVertex2f(0, -1);
    glColor3f(0, 0, 1); glVertex2f(0, 1);
    glColor3f(1, 1, 0); glVertex2f(-1, 1);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (glc_variant == 0) {
        glTextureBarrierNV();
        /* Right half: the left half, mirrored and darkened. */
        glColor3f(0.75f, 0.75f, 0.75f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.5f, 0); glVertex2f(0, -1);
        glTexCoord2f(0, 0); glVertex2f(1, -1);
        glTexCoord2f(0, 1); glVertex2f(1, 1);
        glTexCoord2f(0.5f, 1); glVertex2f(0, 1);
        glEnd();
    } else {
        /* Each right-half column from the one before it, blended. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int x = 16; x < 32; ++x) {
            glTextureBarrierNV();
            float s = (x - 0.5f) / 32, x0 = x / 16.0f - 1;
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_QUADS);
            glTexCoord2f(s, 0); glVertex2f(x0, -1);
            glTexCoord2f(s, 0); glVertex2f(x0 + 1 / 16.0f, -1);
            glTexCoord2f(s, 1); glVertex2f(x0 + 1 / 16.0f, 1);
            glTexCoord2f(s, 1); glVertex2f(x0, 1);
            glEnd();
        }
        glDisable(GL_BLEND);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, previous);
    glViewport(0, 0, glc_width, glc_height);
    glColor3f(1, 1, 1);
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

/* A tessellated, lit patch: a local spot light with the exponent, and the
   material shininess, at and below 128. */
static const char *const exponent_names[] = {"shininess_0", "shininess_1", "shininess_64", "shininess_127_5",
                                             "shininess_128", "spot_0", "spot_50", "spot_128"};
GLC_CASE_VARIANTS(nv_light_max_exponent, exponent_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    static const float exponents[] = {0, 1, 64, 127.5f, 128, 0, 50, 128};
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    static const float position[4] = {0.1f, 0.2f, 0.2f, 1}, direction[3] = {0, 0, -1};
    static const float white[4] = {1, 1, 1, 1}, dim[4] = {0.3f, 0.3f, 0.6f, 1}, black[4] = {0, 0, 0, 1};
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dim);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 1);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
    bool spot = glc_variant >= 5;
    if (spot) {
        glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, direction);
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 60);
        glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, exponents[glc_variant]);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 16);
    } else {
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, exponents[glc_variant]);
    }
    /* Out-of-range values are errors and change nothing. */
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 200);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 300);
    /* The patch 1 in front of the eye (a local viewer at the origin). */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5, 0.5, 0.5, 4);
    glMatrixMode(GL_MODELVIEW);
    glNormal3f(0, 0, 1);
    for (int y = 0; y < 32; ++y) {
        glBegin(GL_QUAD_STRIP);
        for (int x = 0; x <= 32; ++x) {
            float px = x / 16.0f - 1;
            glVertex3f(px, y / 16.0f - 1, -1);
            glVertex3f(px, (y + 1) / 16.0f - 1, -1);
        }
        glEnd();
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
}
