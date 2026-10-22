/* ARB assembly programs to legacy GLSL (arb_translate.c). */
#ifndef GLM_ARB_TRANSLATE_H
#define GLM_ARB_TRANSLATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct glm_arb_translation {
    char *glsl;             /* malloc'd; NULL on failure */
    char error[256];
    int error_position;     /* byte offset of the error */
    int env_count, local_count; /* vec4s of the GLMARB block's arrays */
    uint32_t attribs_used;  /* generic attributes read as arb_attribN */
    /* glGetProgramivARB's program counters, counted as Apple's
       implementation counts them. */
    int instructions, temporaries, parameters, attribs, address_registers;
    int alu_instructions, tex_instructions, tex_indirections;
    bool program_matrices;  /* the GLMARB block ends with glm_arb_matrices[64] */
};

bool glm_arb_translate(const char *source, size_t length, bool vertex, struct glm_arb_translation *out);

#endif
