/* Run with glcompare --cases build/libvertex_layout_bench.dylib. This is
   deliberately separate from pixel conformance and measures completed work. */
#include "glc_gl_core.h"
#include "glcompare.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static const char *const modes[] = {"restored", "alternating_format"};
GLC_CASE_VARIANTS(vertex_layout_bench, modes, .profile = GLC_CORE)
{
    GLuint program = glc_program(
        "#version 410 core\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}",
        "#version 410 core\nout vec4 frag;void main(){frag=vec4(0,1,0,1);}", NULL);
    GLuint vao, buffer;
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&buffer); glBindBuffer(GL_ARRAY_BUFFER,buffer);
    const float triangle[] = {-1,-1,3,-1,-1,3,0};
    glBufferData(GL_ARRAY_BUFFER,sizeof triangle,triangle,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(0);
    glViewport(0,0,8,8); glDrawArrays(GL_TRIANGLES,0,3); glFinish();
    if (glc_variant) {
        /* Both storage formats supply the same vec2 to the shader. Warm both
           pipelines before timing the alternating descriptor lookups. */
        for (int size = 2; size <= 3; ++size) {
            glVertexAttribPointer(0,size,GL_FLOAT,GL_FALSE,8,NULL);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
        glFinish();
    }
    for (int round = 0; round < 3; ++round) {
        double start = now();
        for (int draw = 0; draw < 10000; ++draw) {
            if (glc_variant) glVertexAttribPointer(0,2+(draw&1),GL_FLOAT,GL_FALSE,8,NULL);
            else {
                glDisableVertexAttribArray(0); glEnableVertexAttribArray(0);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,NULL);
                glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);
            }
            glVertexAttribPointer(7,2,GL_FLOAT,GL_FALSE,16,(void *)(uintptr_t)((draw & 3)*4));
            glVertexAttribDivisor(7,(GLuint)(draw & 3));
            glDrawArrays(GL_TRIANGLES,0,3);
        }
        glFinish();
        printf("vertex-layout-bench %s round %d: %.3f us/draw including completion\n",modes[glc_variant],round,
               (now()-start)*1e6/10000);
    }
    unsigned char pixel[4]; glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (pixel[0] || pixel[1] != 255 || pixel[2] || glGetError()) glc_fail("benchmark lost its vertex input");
    glDeleteBuffers(1,&buffer); glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
}
