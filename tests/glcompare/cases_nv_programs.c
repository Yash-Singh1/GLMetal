/* ARB programs with the NV options (NV_fragment_program_option,
   NV_fragment_program2, NV_vertex_program2_option, NV_vertex_program3), the
   parts Apple's OpenGL runs as the extensions say: condition codes and
   conditional writes, KIL on a condition, IF/ELSE and REP blocks, the "set
   on" and arithmetic instructions, |abs| operands, saturation and
   precision suffixes, derivatives, fragment.facing, TXL/TXB/TXP in both
   stages, vertex texture fetch, the two vertex condition codes and the
   vertex address register. Colours encode the computed values.
   (tests/probes/nv_programs.txt covers the instructions Apple's GL gets
   wrong: TXD, CAL/RET/BRA, BRK, LOOP, pack/unpack, ARA/ARR/PUSHA/POPA.) */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>
#include <string.h>

/* A 32x32 mipmapped RGBA8 texture, one colour per level. */
static GLuint level_texture(void)
{
    static unsigned char texels[32 * 32 * 4];
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    for (int level = 0, size = 32; size; ++level, size /= 2) {
        for (int i = 0; i < size * size; ++i) {
            unsigned char *t = texels + i * 4;
            t[0] = (unsigned char)(255 - level * 40);
            t[1] = (unsigned char)(level * 40);
            t[2] = (unsigned char)((level & 1) * 200);
            t[3] = 255;
        }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return texture;
}

/* An 8x8 pattern texture, nearest filtering. */
static GLuint pattern_texture(void)
{
    unsigned char texels[8 * 8 * 4];
    glc_pattern_rgba8(texels, 8, 8, 7);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

/* The full viewport, texture coordinate 0 running (-1,-1) to (1,1) and
   coordinate 1 (0,0) to (1,1). */
static void quad(void)
{
    static const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        glMultiTexCoord4f(GL_TEXTURE0, corners[i][0], corners[i][1], corners[i][0] * corners[i][1], 0.5f);
        glMultiTexCoord2f(GL_TEXTURE1, corners[i][0] * 0.5f + 0.5f, corners[i][1] * 0.5f + 0.5f);
        glVertex2f(corners[i][0], corners[i][1]);
    }
    glEnd();
}

static const char *const fragment_names[] = {
    "write_masks", "write_mask_swizzle", "tests", "nan_codes", "kil", "kil_swizzle", "if_else", "if_nested",
    "rep", "rep_nested", "rep_local", "partial_update", "abs_sat", "set_ops", "div_dp2", "ddx_ddy", "precision",
    "scs_lit", "x_precision_option", "short_long",
};
static const char *const fragment_programs[] = {
    /* write_masks */
    "TEMP t, r;\nSUBC t, fragment.texcoord[0], {0.25, -0.25, 0, 0};\nMOV r, {0.2, 0.2, 0.2, 1};\n"
    "MOV r.xyz (GT), {0.9, 0.8, 0.7, 1};\nMOV r.z (LT.y), 0.4;\nMOV result.color, r;",
    /* write_mask_swizzle */
    "TEMP t, r;\nMOVC t, fragment.texcoord[0];\nMOV r, {0, 0, 0, 1};\nMOV r.xyz (GE.yxzw), 0.75;\n"
    "MOV r.y (NE.zzzz), 0.3;\nMOV result.color, r;",
    /* tests: one test per quadrant of x */
    "TEMP t, r;\nADDC t.x, fragment.texcoord[0].x, -0.5;\nMOV r, {0, 0, 0, 1};\nMOV r.x (LE.x), 0.5;\n"
    "MOV r.y (GE.x), 0.5;\nMOV r.z (EQ.x), 1;\nMOV r.w (NE.x), 0.5;\nMOV r (FL), 0.1;\nMOV r.y (TR), r.y;\n"
    "MOV result.color, r;",
    /* nan_codes: NaN passes NE only */
    "TEMP t, r;\nRCP t.y, program.local[1].x;\nSUB t.x, t.y, t.y;\nMOV t.zw, fragment.texcoord[0].xxxy;\nMOVC t, t;\n"
    "MOV r, {0, 0, 0, 1};\nMOV r.x (NE.x), 1;\nMOV r.y (EQ.x), 1;\nMOV r.y (GE.y), 0.5;\nMOV r.z (GE.zxzw), 1;\n"
    "MOV r.w (LT.w), 0.25;\nMOV result.color, r;",
    /* kil */
    "TEMP t;\nDP3C t, fragment.texcoord[0], fragment.texcoord[0];\nSUBC t, t, 0.6;\nKIL GT.x;\n"
    "MOV result.color, fragment.texcoord[1];",
    /* kil_swizzle */
    "TEMP t;\nMOVC t, fragment.texcoord[0];\nKIL LT.yxzw;\nMOV result.color, {0.2, 0.6, 1, 1};",
    /* if_else */
    "TEMP t;\nMOVC t, fragment.texcoord[0];\nIF GT.x;\nMOV result.color, {1, 0.5, 0, 1};\nELSE;\n"
    "MOV result.color, {0, 0.5, 1, 1};\nENDIF;",
    /* if_nested */
    "TEMP t, r;\nMOV r, {0, 0, 0, 1};\nMOVC t, fragment.texcoord[0];\nIF GT.x;\n IF LT.y;\n  MOV r.x, 1;\n ELSE;\n"
    "  MOV r.y, 1;\n ENDIF;\nELSE;\n IF GT.z;\n  MOV r.z, 1;\n ENDIF;\n MOV r.x, 0.5;\nENDIF;\nMOV result.color, r;",
    /* rep */
    "TEMP t;\nMOV t, {0, 0, 0, 1};\nREP {6, 0, 0, 0};\nMAD t.xyz, fragment.texcoord[1], 0.15, t;\nENDREP;\n"
    "MOV result.color, t;",
    /* rep_nested */
    "TEMP t;\nMOV t, {0, 0, 0, 1};\nREP {2};\nREP {3};\nADD t.x, t.x, 0.1;\nREP {2};\nADD t.y, t.y, 0.05;\n"
    "ENDREP;\nENDREP;\nADD t.z, t.z, 0.4;\nENDREP;\nMUL result.color, t, fragment.texcoord[1].xyxy;",
    /* rep_local: the count from program.local[0] (3) */
    "TEMP t;\nMOV t, {0, 0, 0, 1};\nREP program.local[0];\nADD t.xyz, t, {0.3, 0.2, 0.1, 0};\nENDREP;\n"
    "MOV result.color, t;",
    /* partial_update: CC components not written keep their codes */
    "TEMP t, r;\nMOVC t.xz, fragment.texcoord[0];\nMOVC t.y, fragment.texcoord[0].x;\nMOV r, {0, 0, 0, 1};\n"
    "MOV r.xyz (GT), 1;\nMOV r.w (EQ), 0.5;\nMOV result.color, r;",
    /* abs_sat */
    "TEMP t;\nMUL_SAT t, |fragment.texcoord[0]|, 1.5;\nADD_SAT t.z, -|fragment.texcoord[0].x|, 1;\n"
    "MOV t.w, 1;\nMOV result.color, t;",
    /* set_ops */
    "TEMP t;\nSEQ t.x, fragment.texcoord[0].x, fragment.texcoord[0].x;\nSNE t.y, fragment.texcoord[0].x, 0;\n"
    "SGT t.z, fragment.texcoord[0].x, fragment.texcoord[0].y;\nSLE t.w, fragment.texcoord[0].x, 0.25;\n"
    "MAD result.color, t, 0.5, {0.25, 0.25, 0.25, 0.5};",
    /* div_dp2 */
    "TEMP t;\nDIV t, fragment.texcoord[1], 4;\nDP2 t.z, fragment.texcoord[0], fragment.texcoord[0];\n"
    "MUL t.z, t.z, 0.5;\nMOV t.w, 1;\nMOV result.color, t;",
    /* ddx_ddy */
    "TEMP t, s;\nMUL s, fragment.texcoord[1], {16, 8, 0, 0};\nDDX t.xy, s;\nDDY t.zw, s.yxyx;\nMUL t, t, 2;\n"
    "MOV t.w, 1;\nMOV result.color, t;",
    /* precision */
    "TEMP t, u;\nADDH t, fragment.texcoord[1], 0.125;\nMULX u, t, 0.5;\nMADR t, u, 2, -0.125;\nMOV t.w, 1;\n"
    "MOV result.color, t;",
    /* scs_lit */
    "TEMP t, l;\nMUL t, fragment.texcoord[0], 1.5;\nSCS t.xy, t.x;\nMAD t.xy, t, 0.5, 0.5;\n"
    "MOV l, {0.5, 0.5, 0, 0};\nMUL l.w, fragment.texcoord[1].y, 8;\nLIT l, l;\nMOV t.z, l.z;\nMOV t.w, 1;\n"
    "MOV result.color, t;",
    /* x_precision_option: NV_fragment_program_option */
    "TEMP t;\nMOVC t, fragment.texcoord[0];\nMOV t (LT), 0;\nMULH t, t, 0.8;\nMOV t.w, 1;\nMOV result.color, t;",
    /* short_long */
    "SHORT TEMP s;\nLONG TEMP l;\nMOV s, fragment.texcoord[1];\nMUL l, s, s;\nMOV l.w, 1;\nMOV result.color, l;",
};

GLC_CASE_VARIANTS(nv_fragment_program, fragment_names, .profile = GLC_LEGACY, .tolerance = 1)
{
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    char source[2048];
    snprintf(source, sizeof source, "!!ARBfp1.0\nOPTION %s;\n%s\nEND\n",
             glc_variant == 18 ? "NV_fragment_program" : "NV_fragment_program2", fragment_programs[glc_variant]);
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0, 3, 0, 0, 0);
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 1, 0, 0, 0, 0);
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, source);
    quad();
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}

/* fragment.facing: a front- and a back-facing triangle. (GL_LINE and
   GL_POINT polygon modes are drawn as Metal lines and points, which are
   always front-facing, where Apple's report the polygon's facing.) */
GLC_CASE(nv_fragment_facing, .profile = GLC_LEGACY)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB,
                                     "!!ARBfp1.0\nOPTION NV_fragment_program2;\nTEMP t;\n"
                                     "MAD t, fragment.facing.x, {0.4, -0.4, 0, 0}, {0.5, 0.5, 0.25, 1};\n"
                                     "MOV result.color, t;\nEND\n");
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.9f, -0.9f); glVertex2f(-0.1f, -0.9f); glVertex2f(-0.5f, 0.8f); /* counter-clockwise: front */
    glVertex2f(0.1f, -0.9f); glVertex2f(0.5f, 0.8f); glVertex2f(0.9f, -0.9f);    /* clockwise: back */
    glEnd();
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}

/* TXL picks the level from coord.w, TXB biases the computed one, TXP
   divides by q; NV_fragment_program2. */
static const char *const lod_names[] = {"txl", "txl_fraction", "txb", "txp", "tex_option"};
GLC_CASE_VARIANTS(nv_fragment_texture_lod, lod_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint texture = level_texture();
    static const char *const bodies[] = {
        "TEMP c;\nMOV c, fragment.texcoord[1];\nFLR c.w, fragment.texcoord[1].x;\nMUL c.w, c.x, 6;\nFLR c.w, c.w;\n"
        "TXL result.color, c, texture[0], 2D;",
        "TEMP c;\nMOV c, fragment.texcoord[1];\nMUL c.w, c.x, 5;\nTXL result.color, c, texture[0], 2D;",
        "TEMP c;\nMOV c, fragment.texcoord[1];\nMUL c.w, c.y, 4;\nTXB result.color, c, texture[0], 2D;",
        "TEMP c;\nMUL c, fragment.texcoord[1], {3, 3, 0, 0};\nADD c.w, fragment.texcoord[1].y, 1;\n"
        "MUL c.xy, c, c.w;\nTXP result.color, c, texture[0], 2D;",
        "TEMP c;\nMUL c, fragment.texcoord[1], 0.25;\nTEX result.color, c, texture[0], 2D;",
    };
    char source[1024];
    snprintf(source, sizeof source, "!!ARBfp1.0\nOPTION %s;\n%s\nEND\n",
             glc_variant == 4 ? "NV_fragment_program" : "NV_fragment_program2", bodies[glc_variant]);
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, source);
    quad();
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
    glDeleteTextures(1, &texture);
}

/* Components of result.color a fragment program leaves unwritten are 0
   (alpha shows in the image), with and without the NV option. */
static const char *const partial_names[] = {"arb_rgb", "arb_x", "nv_rgb", "nv_conditional"};
GLC_CASE_VARIANTS(fragment_program_partial_result, partial_names, .profile = GLC_LEGACY)
{
    glClearColor(0.5f, 0.5f, 0.5f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);
    static const char *const sources[] = {
        "!!ARBfp1.0\nMOV result.color.xyz, fragment.texcoord[1];\nEND\n",
        "!!ARBfp1.0\nMOV result.color.x, fragment.texcoord[1].y;\nEND\n",
        "!!ARBfp1.0\nOPTION NV_fragment_program2;\nMOV result.color.xyz, fragment.texcoord[1];\nEND\n",
        ("!!ARBfp1.0\nOPTION NV_fragment_program2;\nTEMP t;\nMOVC t, fragment.texcoord[0];\n"
         "MOV result.color.xy, fragment.texcoord[1];\nIF GT.x;\nMOV result.color.zw, 1;\nENDIF;\nEND\n"),
    };
    GLuint program = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB, sources[glc_variant]);
    quad();
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
}

/* Vertex programs: an 8x8 grid of cells, each cell's four vertices with
   the cell centre's attributes, so a cell shows one vertex's result. */
static void cell_grid(void)
{
    glBegin(GL_QUADS);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            float s = (x + 0.5f) / 8, t = (y + 0.5f) / 8;
            glMultiTexCoord4f(GL_TEXTURE0, s, t, s - t, (float)(y * 8 + x));
            glMultiTexCoord4f(GL_TEXTURE1, s * 2 - 1, t * 2 - 1, (s - 0.5f) * 6, (t - 0.5f) * 6);
            float x0 = x / 4.0f - 1, y0 = y / 4.0f - 1;
            glVertex2f(x0, y0); glVertex2f(x0 + 0.25f, y0); glVertex2f(x0 + 0.25f, y0 + 0.25f); glVertex2f(x0, y0 + 0.25f);
        }
    glEnd();
}

static const char *const vertex_names[] = {
    "tex", "tex_nearest", "txl", "txb", "txp", "sin_cos", "set_ops", "cc_write_masks", "cc0_cc1", "abs_sat",
    "arl_floor", "arl_offsets", "arlc_conditional",
};
static const char *const vertex_programs[] = {
    /* tex: level 0 of the level texture */
    "TEMP c;\nMOV c, vertex.texcoord[0];\nTEX result.color, c, texture[0], 2D;",
    /* tex_nearest */
    "TEX result.color, vertex.texcoord[0], texture[1], 2D;",
    /* txl */
    "TEMP c;\nMOV c, vertex.texcoord[0];\nMUL c.w, vertex.texcoord[0].w, 0.09375;\nTXL result.color, c, texture[0], 2D;",
    /* txb: vertex lookups are at level 0 plus the bias */
    "TEMP c;\nMOV c, vertex.texcoord[0];\nMUL c.w, vertex.texcoord[0].y, 5;\nTXB result.color, c, texture[0], 2D;",
    /* txp */
    "TEMP c;\nMUL c, vertex.texcoord[0], {2, 2, 0, 0};\nMOV c.w, 2;\nTXP result.color, c, texture[1], 2D;",
    /* sin_cos */
    "TEMP t;\nSIN t.x, vertex.texcoord[1].z;\nCOS t.y, vertex.texcoord[1].w;\nMAD t, t, 0.5, 0.5;\nMOV t.zw, 1;\n"
    "MOV result.color, t;",
    /* set_ops */
    "TEMP t;\nSGT t.x, vertex.texcoord[1].x, vertex.texcoord[1].y;\nSLE t.y, vertex.texcoord[1].y, 0;\n"
    "SNE t.z, vertex.texcoord[1].x, 0;\nSEQ t.w, vertex.texcoord[0].x, vertex.texcoord[0].x;\n"
    "MAD result.color, t, 0.75, 0.125;",
    /* cc_write_masks */
    "TEMP t, r;\nMOVC t, vertex.texcoord[1];\nMOV r, {0.1, 0.1, 0.1, 1};\nMOV r.xy (GT), 0.9;\nMOV r.z (LT.y), 0.6;\n"
    "MOV result.color, r;",
    /* cc0_cc1: NV_vertex_program3 */
    "TEMP t, r;\nMOVC1 t, vertex.texcoord[1];\nSUBC0 t, vertex.texcoord[1].yxzw, 0.25;\nMOV r, {0, 0, 0, 1};\n"
    "MOV r.x (GT1.x), 0.8;\nMOV r.y (GT0.x), 0.8;\nMOV r.z (LT1.y), 0.8;\nMOV r.w (LT0.w), 0.5;\nMOV result.color, r;",
    /* abs_sat: NV_vertex_program3 saturation */
    "TEMP t;\nMUL_SAT t, |vertex.texcoord[1]|, 1.25;\nMOV t.w, 1;\nMOV result.color, t;",
    /* arl_floor */
    "ADDRESS A0;\nPARAM c[4] = { program.env[0..3] };\nTEMP t;\nMUL t, vertex.texcoord[1], 2;\nARL A0.x, t.x;\n"
    "MOV result.color, c[A0.x + 2];",
    /* arl_offsets */
    "ADDRESS A0;\nPARAM c[4] = { program.env[0..3] };\nTEMP t;\nMUL t, vertex.texcoord[0].w, 0.04;\nARL A0.x, t.x;\n"
    "ADD result.color, c[A0.x], c[A0.x + 1];",
    /* arlc_conditional: ARL with a condition code update and a conditional ARL */
    "ADDRESS A0;\nPARAM c[4] = { program.env[0..3] };\nTEMP t;\nMUL t, vertex.texcoord[1], 2;\nARLC A0.x, t.x;\n"
    "ARL A0.x (LT.x), 3;\nMOV result.color, c[A0.x];",
};

GLC_CASE_VARIANTS(nv_vertex_program, vertex_names, .profile = GLC_LEGACY, .tolerance = 2)
{
    glClearColor(0.05f, 0.05f, 0.05f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    static const float env[4][4] = {{1, 0, 0, 0.5f}, {0, 0.5f, 0, 1}, {0, 0, 1, 0.25f}, {0.5f, 0.5f, 0, 1}};
    for (int i = 0; i < 4; ++i) glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB, (GLuint)i, env[i]);
    glActiveTexture(GL_TEXTURE1);
    GLuint pattern = pattern_texture();
    glActiveTexture(GL_TEXTURE0);
    GLuint levels = level_texture();
    bool vp3 = glc_variant <= 4 || glc_variant == 8 || glc_variant == 9;
    char source[2048];
    snprintf(source, sizeof source,
             "!!ARBvp1.0\nOPTION %s;\nMOV result.position, vertex.position;\n%s\nEND\n",
             vp3 ? "NV_vertex_program3" : "NV_vertex_program2", vertex_programs[glc_variant]);
    GLuint program = glc_arb_program(GL_VERTEX_PROGRAM_ARB, source);
    cell_grid();
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glDeleteProgramsARB(1, &program);
    glDeleteTextures(1, &levels);
    glDeleteTextures(1, &pattern);
}
