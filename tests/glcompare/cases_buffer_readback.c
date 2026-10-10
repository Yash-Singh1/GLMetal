#include "glc_gl_core.h"
#include "glcompare.h"
#include <string.h>

static const char *const readback_names[] = {"cpu_upload", "transform_feedback"};
GLC_CASE_VARIANTS(core_buffer_readback, readback_names, .profile = GLC_CORE)
{
    const char *vertex = "#version 150 core\nout float value;void main(){value=float(gl_VertexID)+10.;vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
    const char *fragment = "#version 150 core\nout vec4 color;void main(){color=vec4(0,1,0,1);}";
    GLuint program = glc_program(vertex,fragment,NULL), vao = 0, buffer = 0;
    const char *varying = "value";
    glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program); glUseProgram(program);
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&buffer); glBindBuffer(GL_ARRAY_BUFFER,buffer);
    float initial[4] = {1,2,3,4}, actual[4] = {0};
    glBufferData(GL_ARRAY_BUFFER,sizeof initial,initial,GL_DYNAMIC_DRAW);
    const char *error = NULL;
    if (glc_variant == 0) {
        /* A pending draw only reads GPU resources; it need not complete for
           a CPU-authored buffer copy. Earlier CPU uploads must be visible. */
        glDrawArrays(GL_TRIANGLES,0,3);
        float patch[2] = {31,32};
        glBufferSubData(GL_ARRAY_BUFFER,sizeof(float),sizeof patch,patch);
        initial[1] = 31; initial[2] = 32;
    } else {
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
        glEnable(GL_RASTERIZER_DISCARD);
        glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS,0,4); glEndTransformFeedback();
        glDisable(GL_RASTERIZER_DISCARD);
        for (int i=0;i<4;++i) initial[i] = (float)i+10;
    }
    /* No intervening finish or readback may hide the GPU-writer dependency. */
    glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof actual,actual);
    if (memcmp(actual,initial,sizeof actual)) error = "Buffer readback lost CPU uploads or GPU output";
    glDrawArrays(GL_TRIANGLES,0,3);
    if (glGetError()) error = "Buffer readback GL error";
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,0);
    glDeleteBuffers(1,&buffer); glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
    if (error) glc_fail("%s",error);
}
