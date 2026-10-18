/* Small legacy entry points, answering as Apple's OpenGL does
 * (tests/probes/legacy_misc.c): colour-index state, kept though the
 * framebuffer is RGBA; texture priorities and residency; integer clear
 * colours; vertex blend weights and the vertex point size (state only:
 * GLMetal does not blend vertices or size points per vertex); and the
 * calls of extensions Apple exports without advertising, which it accepts
 * without effect (depth bounds, PN triangles, NV register combiners). */
#include "glm_internal.h"

#include <string.h>

/* ---- colour index (GL 2.1 2.7, 4.2.2, 4.2.3) -------------------------------- */

static void set_index(float index)
{
    GLM_CONTEXT(ctx);
    ctx->state.current_index = index;
}
GLM_EXPORT void glIndexf(GLfloat c) { set_index(c); }
GLM_EXPORT void glIndexd(GLdouble c) { set_index((float)c); }
GLM_EXPORT void glIndexi(GLint c) { set_index((float)c); }
GLM_EXPORT void glIndexs(GLshort c) { set_index((float)c); }
GLM_EXPORT void glIndexub(GLubyte c) { set_index((float)c); }
GLM_EXPORT void glIndexfv(const GLfloat *c) { set_index(c[0]); }
GLM_EXPORT void glIndexdv(const GLdouble *c) { set_index((float)c[0]); }
GLM_EXPORT void glIndexiv(const GLint *c) { set_index((float)c[0]); }
GLM_EXPORT void glIndexsv(const GLshort *c) { set_index((float)c[0]); }
GLM_EXPORT void glIndexubv(const GLubyte *c) { set_index((float)c[0]); }

GLM_EXPORT void glClearIndex(GLfloat c)
{
    GLM_CONTEXT(ctx);
    ctx->state.clear_index = c;
}

GLM_EXPORT void glIndexMask(GLuint mask)
{
    GLM_CONTEXT(ctx);
    ctx->state.index_mask = mask;
}

/* ---- texture priorities and residency (GL 2.1 3.8.12) ----------------------- */

/* Names not yet bound (not texture objects) are skipped. */
GLM_EXPORT void glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    GLM_CONTEXT(ctx);
    if (n < 0) return glm_error(ctx, GL_INVALID_VALUE);
    for (GLsizei i = 0; i < n; ++i) {
        struct glm_texture *t = textures[i] ? glm_texture_get(ctx, textures[i]) : NULL;
        if (t && t->target) t->priority = priorities[i] < 0 ? 0 : priorities[i] > 1 ? 1 : priorities[i];
    }
}

/* A texture with an image at its base level is resident. When all are,
   `residences` is left alone. */
GLboolean glm_texture_resident(const struct glm_texture *t)
{
    int base = t->base_level >= 0 && t->base_level < GLM_MAX_LEVELS ? t->base_level : 0;
    return t->levels[0][base].defined && t->levels[0][base].width > 0;
}

GLM_EXPORT GLboolean glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GLM_CONTEXT(ctx, GL_FALSE);
    if (n < 0) return glm_error(ctx, GL_INVALID_VALUE), GL_FALSE;
    bool all = true;
    for (GLsizei i = 0; i < n; ++i) {
        struct glm_texture *t = textures[i] ? glm_texture_get(ctx, textures[i]) : NULL;
        if (!t || !t->target) return glm_error(ctx, GL_INVALID_VALUE), GL_FALSE;
        all &= glm_texture_resident(t);
    }
    if (all) return GL_TRUE;
    for (GLsizei i = 0; i < n; ++i) residences[i] = glm_texture_resident(glm_texture_get(ctx, textures[i]));
    return GL_FALSE;
}

/* ---- integer clear colours (EXT_texture_integer) ----------------------------- */

/* Kept for integer colour buffers; GL_COLOR_CLEAR_VALUE does not change. */
GLM_EXPORT void glClearColorIiEXT(GLint r, GLint g, GLint b, GLint a)
{
    GLM_CONTEXT(ctx);
    const GLint v[4] = {r, g, b, a};
    memcpy(ctx->state.clear_color_integer, v, sizeof v);
}
GLM_EXPORT void glClearColorIuiEXT(GLuint r, GLuint g, GLuint b, GLuint a)
{
    GLM_CONTEXT(ctx);
    const GLuint v[4] = {r, g, b, a};
    memcpy(ctx->state.clear_color_integer, v, sizeof v);
}

/* ---- vertex blend (ARB_vertex_blend) and point size (APPLE_vertex_point_size) - */

enum { MAX_VERTEX_UNITS = 4 };

static void weights(GLint size, const void *values, GLenum type)
{
    GLM_CONTEXT(ctx);
    if (size < 0 || size > MAX_VERTEX_UNITS) return glm_error(ctx, GL_INVALID_VALUE);
    float w[4] = {0, 0, 0, 0};
    for (GLint i = 0; i < size; ++i) {
        switch (type) {
        case GL_BYTE: w[i] = ((const GLbyte *)values)[i] / 127.0f; break;
        case GL_UNSIGNED_BYTE: w[i] = ((const GLubyte *)values)[i] / 255.0f; break;
        case GL_SHORT: w[i] = ((const GLshort *)values)[i] / 32767.0f; break;
        case GL_UNSIGNED_SHORT: w[i] = ((const GLushort *)values)[i] / 65535.0f; break;
        case GL_INT: w[i] = (float)(((const GLint *)values)[i] / 2147483647.0); break;
        case GL_UNSIGNED_INT: w[i] = (float)(((const GLuint *)values)[i] / 4294967295.0); break;
        case GL_DOUBLE: w[i] = (float)((const GLdouble *)values)[i]; break;
        default: w[i] = ((const GLfloat *)values)[i]; break;
        }
    }
    memcpy(ctx->state.current_weight, w, sizeof w);
}
GLM_EXPORT void glWeightbvARB(GLint size, const GLbyte *w) { weights(size, w, GL_BYTE); }
GLM_EXPORT void glWeightubvARB(GLint size, const GLubyte *w) { weights(size, w, GL_UNSIGNED_BYTE); }
GLM_EXPORT void glWeightsvARB(GLint size, const GLshort *w) { weights(size, w, GL_SHORT); }
GLM_EXPORT void glWeightusvARB(GLint size, const GLushort *w) { weights(size, w, GL_UNSIGNED_SHORT); }
GLM_EXPORT void glWeightivARB(GLint size, const GLint *w) { weights(size, w, GL_INT); }
GLM_EXPORT void glWeightuivARB(GLint size, const GLuint *w) { weights(size, w, GL_UNSIGNED_INT); }
GLM_EXPORT void glWeightfvARB(GLint size, const GLfloat *w) { weights(size, w, GL_FLOAT); }
GLM_EXPORT void glWeightdvARB(GLint size, const GLdouble *w) { weights(size, w, GL_DOUBLE); }

GLM_EXPORT void glVertexBlendARB(GLint count)
{
    GLM_CONTEXT(ctx);
    if (count < 1 || count > MAX_VERTEX_UNITS) return glm_error(ctx, GL_INVALID_VALUE);
    ctx->state.active_vertex_units = count;
}

GLM_HIDDEN void glm_set_legacy_array(struct glm_context *ctx, int which, GLint size, GLenum type, GLsizei stride,
                                     const void *pointer);
GLM_EXPORT void glWeightPointerARB(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    GLM_CONTEXT(ctx);
    if (size < 1 || size > MAX_VERTEX_UNITS) return glm_error(ctx, GL_INVALID_VALUE);
    glm_set_legacy_array(ctx, GLM_LEGACY_WEIGHT, size, type, stride, pointer);
    /* Integer weights are normalized (ARB_vertex_blend: as table 2.6). */
    ctx->vao->legacy[GLM_LEGACY_WEIGHT].normalized = type != GL_FLOAT && type != GL_DOUBLE;
}

GLM_EXPORT void glVertexPointSizefAPPLE(GLfloat size)
{
    GLM_CONTEXT(ctx);
    ctx->state.current_point_size = size;
}

/* ---- exported, not advertised: accepted without effect --------------------- */

GLM_EXPORT void glDepthBoundsEXT(GLclampd zmin, GLclampd zmax) { (void)zmin; (void)zmax; }
GLM_EXPORT void glPNTrianglesiATI(GLenum pname, GLint param) { (void)pname; (void)param; }
GLM_EXPORT void glPNTrianglesfATI(GLenum pname, GLfloat param) { (void)pname; (void)param; }
GLM_EXPORT void glPNTrianglesiATIX(GLenum pname, GLint param) { (void)pname; (void)param; }
GLM_EXPORT void glPNTrianglesfATIX(GLenum pname, GLfloat param) { (void)pname; (void)param; }
GLM_EXPORT void glCombinerParameterfNV(GLenum pname, GLfloat param) { (void)pname; (void)param; }
GLM_EXPORT void glCombinerParameteriNV(GLenum pname, GLint param) { (void)pname; (void)param; }
GLM_EXPORT void glCombinerParameterfvNV(GLenum pname, const GLfloat *params) { (void)pname; (void)params; }
GLM_EXPORT void glCombinerParameterivNV(GLenum pname, const GLint *params) { (void)pname; (void)params; }
GLM_EXPORT void glCombinerInputNV(GLenum stage, GLenum portion, GLenum variable, GLenum input, GLenum mapping,
                                  GLenum usage)
{
    (void)stage; (void)portion; (void)variable; (void)input; (void)mapping; (void)usage;
}
GLM_EXPORT void glCombinerOutputNV(GLenum stage, GLenum portion, GLenum ab, GLenum cd, GLenum sum, GLenum scale,
                                   GLenum bias, GLboolean ab_dot, GLboolean cd_dot, GLboolean mux)
{
    (void)stage; (void)portion; (void)ab; (void)cd; (void)sum; (void)scale; (void)bias; (void)ab_dot; (void)cd_dot;
    (void)mux;
}
GLM_EXPORT void glFinalCombinerInputNV(GLenum variable, GLenum input, GLenum mapping, GLenum usage)
{
    (void)variable; (void)input; (void)mapping; (void)usage;
}
GLM_EXPORT void glCombinerStageParameterfvNV(GLenum stage, GLenum pname, const GLfloat *params)
{
    (void)stage; (void)pname; (void)params;
}
/* Their queries answer nothing (and raise nothing). */
GLM_EXPORT void glGetCombinerInputParameterfvNV(GLenum s, GLenum p, GLenum v, GLenum n, GLfloat *o) { (void)s; (void)p; (void)v; (void)n; (void)o; }
GLM_EXPORT void glGetCombinerInputParameterivNV(GLenum s, GLenum p, GLenum v, GLenum n, GLint *o) { (void)s; (void)p; (void)v; (void)n; (void)o; }
GLM_EXPORT void glGetCombinerOutputParameterfvNV(GLenum s, GLenum p, GLenum n, GLfloat *o) { (void)s; (void)p; (void)n; (void)o; }
GLM_EXPORT void glGetCombinerOutputParameterivNV(GLenum s, GLenum p, GLenum n, GLint *o) { (void)s; (void)p; (void)n; (void)o; }
GLM_EXPORT void glGetFinalCombinerInputParameterfvNV(GLenum v, GLenum n, GLfloat *o) { (void)v; (void)n; (void)o; }
GLM_EXPORT void glGetFinalCombinerInputParameterivNV(GLenum v, GLenum n, GLint *o) { (void)v; (void)n; (void)o; }
GLM_EXPORT void glGetCombinerStageParameterfvNV(GLenum s, GLenum p, GLfloat *o) { (void)s; (void)p; (void)o; }

/* Apple-private: accepted without effect. */
GLM_EXPORT void glSamplePass(GLenum mode) { (void)mode; }
GLM_EXPORT void glSamplePassARB(GLenum mode) { (void)mode; }
