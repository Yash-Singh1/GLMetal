/* Separate depth and lighting programs with identical position arithmetic.
 * Change the view over a grid to expose camera-dependent depth mismatches. */
#include "glc_gl_core.h"
#include "glcompare.h"
#include <stdio.h>

static const char *const multipass_variants[] = {"ordinary", "invariant"};

GLC_CASE_VARIANTS(core_depth_multipass, multipass_variants, .profile = GLC_CORE)
{
    const char *declaration = glc_variant ? "invariant gl_Position;\n" : "";
    char depth_vertex[2048], light_vertex[2048];
    const char *header = "#version 150 core\nuniform vec4 world[4];uniform vec4 view[4];in vec4 position;\n";
    snprintf(depth_vertex, sizeof depth_vertex,
             "%s%svoid main(){vec4 p=vec4(position.xyz,1);vec4 w;"
             "w.x=dot(p,world[0]);w.y=dot(p,world[1]);w.z=dot(p,world[2]);w.w=dot(p,world[3]);"
             "vec4 c;c.x=dot(w,view[0]);c.y=dot(w,view[1]);c.z=dot(w,view[2]);c.w=dot(w,view[3]);"
             "gl_Position=c;gl_Position.z=gl_Position.z*2.-gl_Position.w;}", header, declaration);
    snprintf(light_vertex, sizeof light_vertex,
             "%s%sout vec4 lightPosition;void main(){vec4 p=vec4(position.xyz,1);vec4 w;"
             "w.w=dot(p,world[3]);w.x=dot(p,world[0]);w.y=dot(p,world[1]);w.z=dot(p,world[2]);"
             "gl_Position.x=dot(w,view[0]);gl_Position.y=dot(w,view[1]);"
             "gl_Position.z=dot(w,view[2]);gl_Position.w=dot(w,view[3]);"
             "lightPosition=w;gl_Position.z=gl_Position.z*2.-gl_Position.w;}", header, declaration);
    static const char *const attributes[] = {"position", NULL};
    GLuint programs[2] = {
        glc_program(depth_vertex, "#version 150 core\nout vec4 color;void main(){color=vec4(.8,.1,.1,1);}", attributes),
        glc_program(light_vertex, "#version 150 core\nin vec4 lightPosition;out vec4 color;"
                    "void main(){color=vec4(.1,.8,.2,1)+lightPosition*1e-9;}", attributes)
    };
    GLuint vao, buffer;
    glGenVertexArrays(1, &vao);glBindVertexArray(vao);
    glGenBuffers(1, &buffer);glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const GLfloat vertices[] = {-1,-1,.23f,1, 3,-1,.31f,1, -1,3,.47f,1};
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DITHER);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    glClearDepth(1);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    for (unsigned cell = 0; cell < 64; ++cell) {
        float x = 1024.123f + cell * .00713f, y = -4096.75f + cell * .00117f, z = 512.51f;
        GLfloat world[] = {.923f,-.385f,.13f,x, .385f,.923f,.17f,y, .019f,-.023f,1,z, 0,0,0,1};
        GLfloat view[] = {1,0,0,-x, 0,1,0,-y,
                           .00015f,-.00008f,.0001f,.5f-(x*.00015f-y*.00008f+z*.0001f),
                           .03f,-.02f,.01f,1-(x*.03f-y*.02f+z*.01f)};
        glViewport((cell%8)*8,(cell/8)*8,8,8);
        for (unsigned pass = 0; pass < 2; ++pass) {
            glUseProgram(programs[pass]);
            glUniform4fv(glGetUniformLocation(programs[pass],"world"),4,world);
            glUniform4fv(glGetUniformLocation(programs[pass],"view"),4,view);
            glDepthFunc(pass ? GL_EQUAL : GL_ALWAYS);
            glDepthMask(pass ? GL_FALSE : GL_TRUE);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
    }
    glUseProgram(0);glDeleteProgram(programs[0]);glDeleteProgram(programs[1]);
    glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);
}
