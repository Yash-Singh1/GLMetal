#include "glc_gl_core.h"
#include "glcompare.h"
#include <stdio.h>
#include <time.h>

static double seconds(void)
{
    struct timespec time; clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec * 1e-9;
}

/* Completed draw throughput with changing raster state and stable sampling.
   The tiny viewport keeps fragment work small. This is not a game FPS test. */
static void sampling_bench(int raster_changes)
{
    GLuint program = glc_program(
        "#version 150 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}",
        "#version 150 core\nuniform sampler2D image;out vec4 color;void main(){color=texture(image,vec2(.5));}",NULL);
    GLuint texture,vao; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    const unsigned char red[4]={255,0,0,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glViewport(0,0,1,1);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    for(int round=-1;round<9;++round){
        glFinish();double start=seconds();
        for(int draw=0;draw<16384;++draw){
            if(raster_changes)glDepthFunc(draw&1?GL_LEQUAL:GL_LESS);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
        glFinish();
        if(round>=0)fprintf(stderr,"sampling-bindings %s bench round %d: %.3f ms for 16384 completed draws\n",raster_changes?"raster":"stable",round,(seconds()-start)*1000);
    }
    unsigned char actual[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    if(actual[0]!=255||actual[1]||actual[2]||actual[3]!=255)glc_fail("Stable sampling changed during raster updates");
    if(glGetError())glc_fail("Sampling benchmark GL error");
    glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&texture);glDeleteProgram(program);
}

GLC_CASE(core_sampling_bindings_bench, .profile = GLC_CORE) { sampling_bench(1); }
GLC_CASE(core_sampling_bindings_stable_bench, .profile = GLC_CORE) { sampling_bench(0); }
