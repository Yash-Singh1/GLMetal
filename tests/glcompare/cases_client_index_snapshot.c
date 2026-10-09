/* Client index memory may be reused as soon as an indexed draw returns,
   even when VBO vertex draws remain in the asynchronous command stream. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdint.h>
#include <string.h>
static const char *const index_cases[] = {
    "byte_elements", "short_elements", "int_elements",
    "byte_range", "short_range", "int_range",
    "byte_instanced", "short_instanced", "int_instanced",
    "byte_base", "short_base", "int_base"
};
GLC_CASE_VARIANTS(client_index_snapshot, index_cases, .profile = GLC_LEGACY)
{
    GLuint program=glc_program("#version 120\nvoid main(){gl_Position=gl_Vertex;}",
        "#version 120\nvoid main(){gl_FragColor=vec4(0,1,0,1);}", NULL);
    const float positions[]={-1,-1, 0,-1, -.5f,1, 0,-1, 1,-1, .5f,1};
    GLuint buffer; glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof positions,positions,GL_STATIC_DRAW);
    glVertexPointer(2,GL_FLOAT,0,NULL);glEnableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    static const GLenum types[]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT,GL_UNSIGNED_INT};
    uint8_t bytes[]={0,1,2};uint16_t shorts[]={0,1,2};uint32_t ints[]={0,1,2};
    void *indices=glc_variant%3==0?(void *)bytes:glc_variant%3==1?(void *)shorts:(void *)ints;
    GLenum type=types[glc_variant%3];int call=glc_variant/3;
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    if(call==0)glDrawElements(GL_TRIANGLES,3,type,indices);
    else if(call==1)glDrawRangeElements(GL_TRIANGLES,0,2,3,type,indices);
    else {
        extern void *glc_lookup(const char *name);
        if(call==2) {
            void (*draw)(GLenum,GLsizei,GLenum,const void *,GLsizei)=glc_lookup("glDrawElementsInstanced");
            if(!draw)glc_fail("instanced draw missing");draw(GL_TRIANGLES,3,type,indices,1);
        } else {
            void (*draw)(GLenum,GLsizei,GLenum,const void *,GLint)=glc_lookup("glDrawElementsBaseVertex");
            if(!draw)glc_fail("base-vertex draw missing");draw(GL_TRIANGLES,3,type,indices,3);
        }
    }
    memset(bytes,0,sizeof bytes);memset(shorts,0,sizeof shorts);memset(ints,0,sizeof ints);
    unsigned char left[4],right[4];glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
    glReadPixels(48,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
    if(left[1]!=(call==3?0:255) || right[1]!=(call==3?255:0))
        glc_fail("client index snapshot changed: left=%u right=%u",left[1],right[1]);
    if(glGetError())glc_fail("client index draw error");
    glDisableClientState(GL_VERTEX_ARRAY);glDeleteBuffers(1,&buffer);glUseProgram(0);glDeleteProgram(program);
}

GLC_CASE(client_index_unused_pointer, .profile = GLC_LEGACY)
{
    const float positions[]={-1,-1, 1,-1, 0,1};
    GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof positions,positions,GL_STATIC_DRAW);
    glVertexPointer(2,GL_FLOAT,0,NULL);glEnableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    extern void *glc_lookup(const char *name);
    void (*draw)(GLenum,GLsizei,GLenum,const void *,GLsizei)=glc_lookup("glDrawElementsInstanced");
    if(!draw)glc_fail("instanced draw missing");
    while(glGetError()){}
    const void *unused=(const void *)(uintptr_t)1;
    draw(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,unused,0);
    if(glGetError()!=GL_NO_ERROR)glc_fail("zero instances produced an error");
    draw(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,unused,-1);
    if(glGetError()!=GL_INVALID_VALUE)glc_fail("negative instances not rejected");
    draw(0xffffffffu,3,GL_UNSIGNED_SHORT,unused,1);
    if(glGetError()!=GL_INVALID_ENUM)glc_fail("invalid draw mode not rejected");
    glBegin(GL_POINTS);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,unused);
    glEnd();
    if(glGetError()!=GL_INVALID_OPERATION)glc_fail("draw inside Begin/End not rejected");
    glBegin(GL_POINTS);
    glVertexPointer(2,GL_FLOAT,0,unused);
    glEnd();
    if(glGetError()!=GL_INVALID_OPERATION)glc_fail("array pointer inside Begin/End not rejected");
    void *pointer=unused;glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer);
    if(pointer)glc_fail("rejected array pointer changed the binding");
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glDisableClientState(GL_VERTEX_ARRAY);glDeleteBuffers(1,&buffer);
}
