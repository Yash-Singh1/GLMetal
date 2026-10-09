/* Client vertices and indices remain valid for the recorded draw even when
   the application reuses both immediately. Two draws also exercise VAO memo
   invalidation when the worker's private VAO uses the same stack address. */
#include "glc_gl_legacy.h"
#include "glcompare.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *const layouts[] = {
    "byte_tight", "short_tight", "int_tight",
    "byte_interleaved", "short_interleaved", "int_interleaved",
    "byte_mixed", "short_mixed", "int_mixed",
    "byte_base", "short_base", "int_base"
};
GLC_CASE_VARIANTS(client_vertex_snapshot, layouts, .profile = GLC_LEGACY)
{
    struct vertex { float p[2]; unsigned char color[4], padding[4]; } vertices[6] = {0};
    float positions[6][2] = {{0}};
    unsigned char colors[6][4] = {{0}};
    int layout = glc_variant / 3, base = layout == 3 ? 3 : 0;
    static const float triangle[3][2] = {{-1,-1}, {0,-1}, {-.5f,1}};
    for (int i = 0; i < 3; ++i) {
        memcpy(positions[base+i], triangle[i], sizeof triangle[i]);
        memcpy(vertices[base+i].p, triangle[i], sizeof triangle[i]);
        colors[base+i][0] = vertices[base+i].color[0] = 255;
        colors[base+i][3] = vertices[base+i].color[3] = 255;
    }
    GLuint program = glc_program("#version 120\nvoid main(){gl_Position=gl_Vertex;gl_FrontColor=gl_Color;}",
        "#version 120\nvoid main(){gl_FragColor=gl_Color;}", NULL);
    GLuint buffer = 0;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (layout == 1) {
        glVertexPointer(2, GL_FLOAT, sizeof vertices[0], vertices[0].p);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof vertices[0], vertices[0].color);
    } else {
        if (layout == 2) {
            glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof positions, positions, GL_STREAM_DRAW);
        }
        glVertexPointer(2, GL_FLOAT, 0, layout == 2 ? NULL : positions);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    }
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    static const GLenum types[] = {GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT, GL_UNSIGNED_INT};
    uint8_t bytes[3] = {0,1,2}; uint16_t shorts[3] = {0,1,2}; uint32_t ints[3] = {0,1,2};
    void *indices = glc_variant % 3 == 0 ? (void *)bytes : glc_variant % 3 == 1 ? (void *)shorts : (void *)ints;
    extern void *glc_lookup(const char *name);
    void (*draw_base)(GLenum,GLsizei,GLenum,const void *,GLint) = glc_lookup("glDrawElementsBaseVertex");
    if (base && !draw_base) glc_fail("base-vertex draw missing");
    for (int draw = 0; draw < 2; ++draw) {
        glScissor(0, draw * 32, 64, 32);
        bytes[0]=shorts[0]=ints[0]=0; bytes[1]=shorts[1]=ints[1]=1; bytes[2]=shorts[2]=ints[2]=2;
        if (base) draw_base(GL_TRIANGLES,3,types[glc_variant%3],indices,base);
        else glDrawRangeElements(GL_TRIANGLES,0,2,3,types[glc_variant%3],indices);
        memset(bytes,0,sizeof bytes); memset(shorts,0,sizeof shorts); memset(ints,0,sizeof ints);
        for (int i = 0; i < 3; ++i) {
            positions[base+i][0] += 1; vertices[base+i].p[0] += 1;
            colors[base+i][0] = vertices[base+i].color[0] = 0;
            colors[base+i][1] = vertices[base+i].color[1] = 255;
        }
        if (!draw && buffer) {
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferSubData(GL_ARRAY_BUFFER,0,sizeof positions,positions);
            glBindBuffer(GL_ARRAY_BUFFER,0);
        }
    }
    memset(positions,0,sizeof positions); memset(vertices,0,sizeof vertices); memset(colors,0,sizeof colors);
    unsigned char first[4], second[4];
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,first);
    glReadPixels(48,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,second);
    if (first[0] != 255 || first[1] || second[0] || second[1] != 255)
        glc_fail("client vertices changed: first=%u,%u second=%u,%u",first[0],first[1],second[0],second[1]);
    void *pointer = NULL; glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer);
    if (pointer != (layout == 1 ? (void *)vertices[0].p : layout == 2 ? NULL : (void *)positions))
        glc_fail("snapshot changed the bound vertex pointer");
    if (glGetError()) glc_fail("client vertex draw error");
    glDisable(GL_SCISSOR_TEST); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
    if (buffer) glDeleteBuffers(1,&buffer);
    glUseProgram(0); glDeleteProgram(program);
}

/* The last referenced element need not include the unused end of its stride. */
#include <sys/mman.h>
#include <unistd.h>
GLC_CASE(client_vertex_snapshot_guarded_stride, .profile = GLC_LEGACY)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED || mprotect(memory + page, page, PROT_NONE)) glc_fail("guarded mapping failed");
    float *position = (float *)(memory + page - 2 * sizeof(float));
    position[0] = position[1] = 0;
    GLuint program = glc_program("#version 120\nvoid main(){gl_Position=gl_Vertex;}",
        "#version 120\nvoid main(){gl_FragColor=vec4(0,1,0,1);}", NULL);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glVertexPointer(2,GL_FLOAT,128,position); glEnableClientState(GL_VERTEX_ARRAY);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glPointSize(8);
    unsigned short index = 0; glDrawElements(GL_POINTS,1,GL_UNSIGNED_SHORT,&index);
    position[0] = position[1] = 10; index = 0xffff;
    unsigned char pixel[4]; glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (pixel[0] || pixel[1] != 255 || glGetError()) glc_fail("guarded client element lost");
    glDisableClientState(GL_VERTEX_ARRAY); glPointSize(1); glUseProgram(0); glDeleteProgram(program);
    munmap(memory,page*2);
}

/* Two separate prefixes would exceed the snapshot budget. Their shared
   stride fits in one 192 KiB prefix and retains the original fetch layout. */
GLC_CASE(client_vertex_snapshot_grouped_prefix, .profile = GLC_LEGACY)
{
    struct vertex { float position[2]; unsigned char color[4], padding[4]; };
    enum { VERTICES = 12000 };
    struct vertex *vertices = calloc(VERTICES, sizeof *vertices);
    if (!vertices) glc_fail("interleaved allocation failed");
    vertices[VERTICES-1].color[1] = vertices[VERTICES-1].color[3] = 255;
    GLuint program = glc_program("#version 120\nvoid main(){gl_Position=gl_Vertex;gl_FrontColor=gl_Color;}",
        "#version 120\nvoid main(){gl_FragColor=gl_Color;}", NULL);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glVertexPointer(2,GL_FLOAT,sizeof *vertices,vertices[0].position);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof *vertices,vertices[0].color);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glPointSize(8);
    unsigned int index = VERTICES-1;
    glDrawRangeElements(GL_POINTS,index,index,1,GL_UNSIGNED_INT,&index);
    vertices[VERTICES-1].position[0] = vertices[VERTICES-1].position[1] = 10;
    memset(vertices[VERTICES-1].color,0,sizeof vertices[VERTICES-1].color); index = 0;
    unsigned char pixel[4]; glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (pixel[0] || pixel[1] != 255 || glGetError()) glc_fail("grouped client prefix changed");
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
    glPointSize(1); glUseProgram(0); glDeleteProgram(program); free(vertices);
}
