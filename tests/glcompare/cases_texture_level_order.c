#include "glc_gl_legacy.h"
#include "glcompare.h"

static const char *const level_orders[]={"copy_then_sampler", "nonzero_then_sampler", "copy_redefine", "automatic_mipmap"};
GLC_CASE_VARIANTS(texture_level_order, level_orders, .profile=GLC_LEGACY)
{
    GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    GLint level=glc_variant==1?2:0;
    GLint expected_width=8,expected_height=4;
    if(glc_variant==1)
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,8,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else
        glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,0,0,8,4,0);
    glFinish();
    if(glc_variant==2) {
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        expected_width=16;expected_height=8;
    } else if(glc_variant==3) {
        glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
        const unsigned char pixels[16*8*4]={0};
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,8,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        level=1;expected_width=8;expected_height=4;
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    GLint width=-1,height=-1,format=-1,wrap=-1;GLfloat float_width=-1;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_INTERNAL_FORMAT,&format);
    glGetTexLevelParameterfv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&float_width);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrap);
    if(width!=expected_width || height!=expected_height || format!=GL_RGBA8 || float_width!=expected_width || wrap!=GL_CLAMP_TO_EDGE)
        glc_fail("ordered level query width=%d height=%d format=%x float=%g wrap=%x",width,height,format,float_width,wrap);
    if(glGetError())glc_fail("texture level query error");
    glDeleteTextures(1,&texture);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
}
