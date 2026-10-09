/* Alpha test state changes must not need new ARB fragment shader sources.
   Check all comparisons, exact equality, disable/re-enable and reference
   clamping while retaining fragment discard's depth-write behavior. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdbool.h>
#include <math.h>

static const char *const refs[] = {"negative", "zero", "half", "one", "above_one"};
GLC_CASE_VARIANTS(arb_alpha_state, refs, .profile = GLC_LEGACY)
{
    static const float references[] = {-1, 0, .5f, 1, 2};
    static const float alphas[] = {0, .25f, .5f, .75f, 1};
    float requested = references[glc_variant];
    float reference = requested < 0 ? 0 : requested > 1 ? 1 : requested;
    GLuint vertex = glc_arb_program(GL_VERTEX_PROGRAM_ARB,
        "!!ARBvp1.0\nMOV result.position, vertex.position;\nMOV result.color, vertex.color;\nEND\n");
    GLuint fragment = glc_arb_program(GL_FRAGMENT_PROGRAM_ARB,
        "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND\n");
    glEnable(GL_SCISSOR_TEST); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS);
    for (int enabled = 0; enabled < 2; ++enabled) {
        if (enabled) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
        for (int function = 0; function < 8; ++function) {
            glAlphaFunc(GL_NEVER + function, requested);
            for (int row = 0; row < 5; ++row) {
                int x = function * 8, y = (enabled * 5 + row) * 6;
                glScissor(x, y, 8, 6);
                glClearColor(1, 0, 0, 1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glColor4f(0, 1, 0, alphas[row]);
                glBegin(GL_TRIANGLES); glVertex3f(-1, -1, 0); glVertex3f(3, -1, 0); glVertex3f(-1, 3, 0); glEnd();
                bool pass = !enabled || function == 7 ||
                    (function == 1 && alphas[row] < reference) || (function == 2 && alphas[row] == reference) ||
                    (function == 3 && alphas[row] <= reference) || (function == 4 && alphas[row] > reference) ||
                    (function == 5 && alphas[row] != reference) || (function == 6 && alphas[row] >= reference);
                unsigned char pixel[4]; float depth;
                glReadPixels(x + 4, y + 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                glReadPixels(x + 4, y + 3, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                if ((pass && (pixel[0] || pixel[1] != 255 || fabsf(depth - .5f) > .000001f)) ||
                    (!pass && (pixel[0] != 255 || pixel[1] || fabsf(depth - 1) > .000001f)))
                    glc_fail("alpha state enabled=%d function=%d alpha=%g ref=%g: color=%u,%u depth=%g",
                        enabled, function, alphas[row], requested, pixel[0], pixel[1], depth);
            }
        }
    }
    glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
    glDisable(GL_VERTEX_PROGRAM_ARB); glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDeleteProgramsARB(1, &vertex); glDeleteProgramsARB(1, &fragment);
}
