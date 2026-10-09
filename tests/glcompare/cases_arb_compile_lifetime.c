/* Upload-time validation remains synchronous. Pending native compiles must
   survive program deletion/replacement, and first-use draws must complete. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const lifetimes[] = {"first_use", "replace_pending", "delete_pending"};
GLC_CASE_VARIANTS(arb_compile_lifetime, lifetimes, .profile = GLC_LEGACY)
{
    GLuint vertex = glc_arb_program(GL_VERTEX_PROGRAM_ARB,
        "!!ARBvp1.0\nMOV result.position, vertex.position;\nEND\n");
    GLuint programs[24]; glGenProgramsARB(24, programs);
    for (int i = 0; i < 24; ++i) {
        char source[256];
        snprintf(source, sizeof source, "!!ARBfp1.0\nMOV result.color, {%.8f, 0.5, 0.75, 1};\nEND\n", (i + 1) / 32.0);
        glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, programs[i]);
        glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(source), source);
        if (glGetError()) glc_fail("valid upload %d failed", i);
    }
    int red = 8;
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, programs[0]);
    if (glc_variant == 1) {
        const char *replacement = "!!ARBfp1.0\nMOV result.color, {0.25, 0.5, 0.75, 1};\nEND\n";
        glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(replacement), replacement);
        red = 64;
    } else if (glc_variant == 2) {
        glDeleteProgramsARB(23, programs + 1);
    }
    glEnable(GL_VERTEX_PROGRAM_ARB); glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES); glVertex2f(-1, -1); glVertex2f(3, -1); glVertex2f(-1, 3); glEnd();
    unsigned char pixel[4]; glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (abs(pixel[0] - red) > 1 || abs(pixel[1] - 128) > 1 || abs(pixel[2] - 191) > 1 || pixel[3] != 255)
        glc_fail("first-use draw lost/replaced: %u,%u,%u,%u", pixel[0], pixel[1], pixel[2], pixel[3]);
    /* A malformed upload must set the error before any draw. */
    GLuint invalid; glGenProgramsARB(1, &invalid); glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, invalid);
    const char *bad = "!!ARBfp1.0\nINVALID result.color;\nEND\n";
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(bad), bad);
    if (glGetError() != GL_INVALID_OPERATION) glc_fail("malformed upload error was deferred");
    GLint position = -1; glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &position);
    if (position < 0) glc_fail("missing upload error position");
    glDeleteProgramsARB(1, &invalid); glDeleteProgramsARB(24, programs); glDeleteProgramsARB(1, &vertex);
    glDisable(GL_VERTEX_PROGRAM_ARB); glDisable(GL_FRAGMENT_PROGRAM_ARB);
}
