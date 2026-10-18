/* Small legacy entry points as Apple's OpenGL answers them: colour-index
   state in RGBA mode, texture priorities and residency, integer clear
   colours, the vertex point size, vertex blend weights, and calls of
   extensions Apple does not advertise (errors only). make
   test-legacy-misc diffs a run on Apple's against one on GLMetal.

   legacy_misc [provider.dylib] */
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define F(name) typeof(&name) p##name = dlsym(lib, #name)
#define G(type, name, args) type(*p##name) args = dlsym(lib, #name)

static void *lib;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    lib = dlopen(argc > 1 ? argv[1] : "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_NOW);
    if (!lib) return fprintf(stderr, "%s\n", dlerror()), 2;
    bool (*init)(void) = dlsym(lib, "glmetal_initialize");
    if (init) init();
    F(CGLChoosePixelFormat); F(CGLCreateContext); F(CGLSetCurrentContext);
    F(glGetError); F(glGetIntegerv); F(glGetFloatv); F(glGetBooleanv); F(glIsEnabled); F(glEnable); F(glDisable);
    F(glIndexi); F(glIndexf); F(glIndexd); F(glIndexs); F(glIndexub); F(glIndexfv); F(glClearIndex); F(glIndexMask);
    F(glGenTextures); F(glBindTexture); F(glPrioritizeTextures); F(glAreTexturesResident); F(glGetTexParameterfv);
    F(glTexParameterf); F(glTexImage2D); F(glDeleteTextures); F(glPushAttrib); F(glPopAttrib); F(glBegin); F(glEnd);
    F(glClearColorIiEXT); F(glClearColorIuiEXT); F(glVertexPointSizefAPPLE); F(glDepthBoundsEXT);
    F(glWeightfvARB); F(glWeightPointerARB); F(glVertexBlendARB); F(glNewList); F(glEndList); F(glCallList);
    F(glGenLists);
    G(void, glCombinerParameteriNV, (GLenum, GLint));
    G(void, glPNTrianglesiATI, (GLenum, GLint));
    G(void, glNamedStringARB, (GLenum, GLint, const char *, GLint, const char *));
    G(GLboolean, glIsNamedStringARB, (GLint, const char *));
    G(void, glGetNamedStringivARB, (GLint, const char *, GLenum, GLint *));
    G(void, glDeleteNamedStringARB, (GLint, const char *));

    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, 0};
    CGLPixelFormatObj format;
    GLint count;
    CGLContextObj context;
    pCGLChoosePixelFormat(attributes, &format, &count);
    pCGLCreateContext(format, NULL, &context);
    pCGLSetCurrentContext(context);
    while (pglGetError()) {}
#define ERR(what) printf("%s: error 0x%04x\n", what, pglGetError())
#define GETF(pname) do { GLfloat v[4] = {-7, -7, -7, -7}; pglGetFloatv(pname, v); \
        printf("get %s: %g %g %g %g\n", #pname, v[0], v[1], v[2], v[3]); ERR(#pname); } while (0)
#define GETI(pname) do { GLint v[4] = {-7, -7, -7, -7}; pglGetIntegerv(pname, v); \
        printf("geti %s: %d %d %d %d\n", #pname, v[0], v[1], v[2], v[3]); ERR(#pname); } while (0)

    /* Colour-index state (kept in RGBA mode). */
    GETF(GL_CURRENT_INDEX);
    GETF(GL_INDEX_CLEAR_VALUE);
    GETI(GL_INDEX_WRITEMASK);
    GETI(GL_INDEX_BITS);
    GETI(GL_INDEX_MODE);
    GETI(GL_RGBA_MODE);
    pglIndexi(7); GETF(GL_CURRENT_INDEX);
    pglIndexf(2.75f); GETF(GL_CURRENT_INDEX);
    pglIndexd(-3.5); GETF(GL_CURRENT_INDEX);
    pglIndexs(300); GETF(GL_CURRENT_INDEX);
    pglIndexub(250); GETF(GL_CURRENT_INDEX);
    GLfloat index = 1.25f;
    pglIndexfv(&index); GETF(GL_CURRENT_INDEX);
    pglBegin(GL_POINTS); pglIndexi(9); pglEnd(); ERR("index in begin"); GETF(GL_CURRENT_INDEX);
    pglClearIndex(5.5f); GETF(GL_INDEX_CLEAR_VALUE);
    pglIndexMask(0x0f0f); GETI(GL_INDEX_WRITEMASK);
    pglPushAttrib(GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);
    pglIndexi(1); pglClearIndex(2); pglIndexMask(3);
    pglPopAttrib();
    GETF(GL_CURRENT_INDEX); GETF(GL_INDEX_CLEAR_VALUE); GETI(GL_INDEX_WRITEMASK);
    GLuint list = pglGenLists(1);
    pglNewList(list, GL_COMPILE);
    pglIndexi(42); pglClearIndex(43); pglIndexMask(44);
    pglEndList();
    GETF(GL_CURRENT_INDEX);
    pglCallList(list);
    GETF(GL_CURRENT_INDEX); GETF(GL_INDEX_CLEAR_VALUE); GETI(GL_INDEX_WRITEMASK);

    /* Texture priorities and residency. */
    GLuint textures[3];
    pglGenTextures(3, textures);
    pglBindTexture(GL_TEXTURE_2D, textures[0]);
    GLfloat priority = -1;
    pglGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, &priority);
    printf("default priority %g\n", priority);
    ERR("priority");
    const GLfloat priorities[3] = {0.25f, 2.0f, -1.0f};
    pglPrioritizeTextures(3, textures, priorities);
    ERR("prioritize");
    for (int i = 0; i < 3; ++i) {
        pglBindTexture(GL_TEXTURE_2D, textures[i]);
        pglGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, &priority);
        printf("priority %d %g\n", i, priority);
    }
    pglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 0.5f);
    pglGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, &priority);
    printf("texparameter priority %g\n", priority);
    ERR("texparameter priority");
    GLuint unknown[2] = {textures[0], 999};
    pglPrioritizeTextures(2, unknown, priorities); ERR("prioritize unknown");
    pglPrioritizeTextures(-1, textures, priorities); ERR("prioritize negative");
    const GLubyte pixel[4] = {1, 2, 3, 4};
    pglBindTexture(GL_TEXTURE_2D, textures[1]);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLboolean resident[3] = {7, 7, 7};
    GLboolean all = pglAreTexturesResident(3, textures, resident);
    printf("resident all %d: %d %d %d\n", all, resident[0], resident[1], resident[2]);
    ERR("resident");
    resident[0] = resident[1] = 7;
    all = pglAreTexturesResident(2, unknown, resident);
    printf("resident unknown %d: %d %d\n", all, resident[0], resident[1]);
    ERR("resident unknown");
    GLuint zero = 0;
    all = pglAreTexturesResident(1, &zero, resident); ERR("resident zero");
    all = pglAreTexturesResident(-1, textures, resident); ERR("resident negative");
    pglGetFloatv(GL_TEXTURE_RESIDENT, &priority); ERR("get texture resident");
    GLfloat r = -7;
    pglBindTexture(GL_TEXTURE_2D, textures[1]);
    pglGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_RESIDENT, &r);
    printf("texparameter resident %g\n", r);
    ERR("texparameter resident");
    pglDeleteTextures(3, textures);

    /* Integer clear colours (EXT_texture_integer). */
    pglClearColorIiEXT(-5, 6, 70000, -1); ERR("clear color ii");
    GETF(GL_COLOR_CLEAR_VALUE);
    GETI(GL_COLOR_CLEAR_VALUE);
    pglClearColorIuiEXT(5, 4000000000u, 7, 1); ERR("clear color iui");
    GETF(GL_COLOR_CLEAR_VALUE);
    GETI(GL_RGBA_INTEGER_MODE_EXT);

    /* Vertex point size (APPLE_vertex_point_size). */
    pglVertexPointSizefAPPLE(3.5f); ERR("vertex point size");
    GETF(0x8A3F);
    GETI(GL_VERTEX_POINT_SIZE_APPLE);
    pglEnable(GL_VERTEX_POINT_SIZE_APPLE); ERR("enable vertex point size");
    printf("enabled %d\n", pglIsEnabled(GL_VERTEX_POINT_SIZE_APPLE));
    pglDisable(GL_VERTEX_POINT_SIZE_APPLE);

    /* Vertex blend (ARB_vertex_blend). */
    GETI(GL_MAX_VERTEX_UNITS_ARB);
    GETI(GL_ACTIVE_VERTEX_UNITS_ARB);
    GETI(GL_VERTEX_BLEND_ARB);
    GETF(GL_CURRENT_WEIGHT_ARB);
    const GLfloat weights[4] = {0.25f, 0.5f, 0.125f, 0.0625f};
    pglWeightfvARB(2, weights); ERR("weight 2");
    GETF(GL_CURRENT_WEIGHT_ARB);
    pglWeightfvARB(5, weights); ERR("weight 5");
    pglVertexBlendARB(2); ERR("vertex blend 2");
    GETI(GL_ACTIVE_VERTEX_UNITS_ARB);
    pglVertexBlendARB(9); ERR("vertex blend 9");
    pglEnable(GL_VERTEX_BLEND_ARB); ERR("enable vertex blend");
    pglDisable(GL_VERTEX_BLEND_ARB);
    pglWeightPointerARB(2, GL_FLOAT, 0, weights); ERR("weight pointer");
    GETI(GL_WEIGHT_ARRAY_SIZE_ARB);
    pglWeightPointerARB(5, GL_FLOAT, 0, weights); ERR("weight pointer 5");
    pglWeightPointerARB(2, GL_INT, 0, weights); ERR("weight pointer int");

    /* Extensions Apple does not advertise. */
    pglDepthBoundsEXT(0.25, 0.75); ERR("depth bounds");
    pglCombinerParameteriNV(0x854E, 1); ERR("combiner parameter");
    pglPNTrianglesiATI(0x87F2, 0x87F5); ERR("pn triangles");

    /* Named strings (ARB_shading_language_include). */
    pglNamedStringARB(0x8DAE, -1, "/a/b.h", -1, "float x;"); ERR("named string");
    printf("is named %d\n", pglIsNamedStringARB(-1, "/a/b.h")); ERR("is named");
    GLint length = -7;
    pglGetNamedStringivARB(-1, "/a/b.h", 0x8DE9, &length); ERR("named length");
    printf("named length %d\n", length);
    pglNamedStringARB(0x8DAE, -1, "a/b.h", -1, "float x;"); ERR("named string relative");
    pglDeleteNamedStringARB(-1, "/a/b.h"); ERR("delete named");

    /* NV register combiners: entry points accepted without effect. */
    G(void, glCombinerInputNV, (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum));
    G(void, glCombinerOutputNV, (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLboolean, GLboolean, GLboolean));
    G(void, glFinalCombinerInputNV, (GLenum, GLenum, GLenum, GLenum));
    G(void, glCombinerStageParameterfvNV, (GLenum, GLenum, const GLfloat *));
    G(void, glGetCombinerInputParameterivNV, (GLenum, GLenum, GLenum, GLenum, GLint *));
    G(void, glGetCombinerOutputParameterfvNV, (GLenum, GLenum, GLenum, GLfloat *));
    G(void, glGetFinalCombinerInputParameterivNV, (GLenum, GLenum, GLint *));
    G(void, glSamplePass, (GLenum));
    G(void, glSamplePassARB, (GLenum));
    G(void, glBindBufferOffsetEXT, (GLenum, GLuint, GLuint, GLintptr));
    G(void, glGetIntegerIndexedvEXT, (GLenum, GLuint, GLint *));
    F(glGenBuffers); F(glBindBuffer); F(glBufferData);
    pglEnable(0x8522 /* GL_REGISTER_COMBINERS_NV */); ERR("enable combiners");
    pglCombinerInputNV(0x8550, GL_RGB, 0x8523, 0x852C, 0x8536, GL_RGB); ERR("combiner input");
    pglCombinerOutputNV(0x8550, GL_RGB, 0x852E, 0x8530, 0x8530, GL_NONE, GL_NONE, 0, 0, 0); ERR("combiner output");
    pglFinalCombinerInputNV(0x8523, 0x852C, 0x8536, GL_RGB); ERR("final combiner input");
    const GLfloat stage[4] = {1, 0, 0, 1};
    pglCombinerStageParameterfvNV(0x8550, 0x852A, stage); ERR("combiner stage parameter");
    GLint combiner[4] = {-7, -7, -7, -7};
    pglGetCombinerInputParameterivNV(0x8550, GL_RGB, 0x8523, 0x8542, combiner); ERR("get combiner input");
    GLfloat output = -7;
    pglGetCombinerOutputParameterfvNV(0x8550, GL_RGB, 0x8545, &output); ERR("get combiner output");
    pglGetFinalCombinerInputParameterivNV(0x8523, 0x8542, combiner + 1); ERR("get final input");
    printf("combiner queries %d %d %g\n", combiner[0], combiner[1], output);
    GETI(0x854E /* GL_NUM_GENERAL_COMBINERS_NV */);
    pglSamplePass(0); ERR("sample pass");
    pglSamplePassARB(0x8F00); ERR("sample pass arb");

    /* glBindBufferOffsetEXT (EXT_transform_feedback). */
    GLuint feedback;
    pglGenBuffers(1, &feedback);
    pglBindBuffer(GL_ARRAY_BUFFER, feedback);
    pglBufferData(GL_ARRAY_BUFFER, 256, NULL, GL_STATIC_DRAW);
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, feedback, 16); ERR("bind offset 16");
    const GLenum indexed[] = {GL_TRANSFORM_FEEDBACK_BUFFER_BINDING_EXT, GL_TRANSFORM_FEEDBACK_BUFFER_START_EXT,
                              GL_TRANSFORM_FEEDBACK_BUFFER_SIZE_EXT};
    for (int k = 0; k < 3; ++k) {
        GLint value = -7;
        pglGetIntegerIndexedvEXT(indexed[k], 0, &value);
        printf("indexed %#x: %d\n", indexed[k], k ? value : value == (GLint)feedback);
        ERR("indexed");
    }
    GETI(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING_EXT);
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, feedback, 3); ERR("bind offset 3");
    pglBindBufferOffsetEXT(GL_ARRAY_BUFFER, 0, feedback, 0); ERR("bind offset array buffer");
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 99, feedback, 0); ERR("bind offset index 99");
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 0, 0, 0); ERR("bind offset buffer 0");
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 1, feedback, 512); ERR("bind offset past end");
    GLint size = -7;
    pglGetIntegerIndexedvEXT(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE_EXT, 1, &size);
    printf("size past end %d\n", size);
    pglBindBufferOffsetEXT(GL_TRANSFORM_FEEDBACK_BUFFER_EXT, 1, 0, 0);
    ERR("end");
    return 0;
}
