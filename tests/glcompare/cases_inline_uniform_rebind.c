#include "glc_gl_legacy.h"
#include "glcompare.h"

/* Fixed-function uniforms and programmable sampler metadata use slot 16.
   Reusing identical inline bytes must still rebind after a buffer used it. */
GLC_CASE(legacy_inline_uniform_rebind, .profile = GLC_LEGACY)
{
    GLuint program = glc_program(
        "#version 120\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}",
        "#version 120\nuniform sampler2D tex;void main(){gl_FragColor=texture2D(tex,gl_TexCoord[0].xy);}",NULL);
    glUniform1i(glGetUniformLocation(program,"tex"),0);
    GLuint textures[2];glGenTextures(2,textures);
    for (int green=0;green<2;++green) {
        unsigned char color[4]={green?0:255,green?255:0,0,255};
        glBindTexture(GL_TEXTURE_2D,textures[green]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,color);
    }
    glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glColor4f(1,1,1,1);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    for (int strip=0;strip<8;++strip) {
        glViewport(0,strip*8,64,8);
        glUseProgram(strip&1?0:program);glBindTexture(GL_TEXTURE_2D,textures[strip&1]);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(.5f,.5f);glVertex2f(-1,-1);glVertex2f(3,-1);glVertex2f(-1,3);
        glEnd();
    }
    for (int strip=0;strip<8;++strip) {
        unsigned char color[4];glReadPixels(32,strip*8+4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,color);
        if (color[0] != (strip&1?0:255) || color[1] != (strip&1?255:0) || color[2])
            glc_fail("inline uniforms lost after buffer rebinding at strip %d: %u,%u,%u",strip,color[0],color[1],color[2]);
    }
    if (glGetError()) glc_fail("inline uniform rebind GL error");
    glViewport(0,0,64,64);glDisable(GL_TEXTURE_2D);glUseProgram(0);
    glDeleteTextures(2,textures);glDeleteProgram(program);
}
