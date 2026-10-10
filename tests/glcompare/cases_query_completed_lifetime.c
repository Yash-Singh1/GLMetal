#include "glc_gl_core.h"
#include "glcompare.h"

static GLuint completed_result(GLuint query)
{
    GLuint available = 0;
    glFlush();
    for (unsigned attempt = 0; attempt < 2000000 && !available; ++attempt)
        glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) glc_fail("Query never became available");
    GLuint value = ~0u, again = ~0u;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &value);
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &again);
    if (value != again) glc_fail("Completed query changed between getters");
    return value;
}
static void expect_query_error(GLenum expected, const char *operation)
{
    GLenum actual = glGetError();
    if (actual != expected) glc_fail("%s error %x, expected %x", operation, actual, expected);
}
static void query_draw(GLuint query, bool visible, bool indexed)
{
    /* Empty scissor produces exactly zero samples, independent of raster edges. */
    glScissor(0, 0, visible ? glc_width : 0, glc_height);
    if (indexed) glBeginQueryIndexed(GL_SAMPLES_PASSED, 0, query);
    else glBeginQuery(GL_SAMPLES_PASSED, query);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (indexed) glEndQueryIndexed(GL_SAMPLES_PASSED, 0);
    else glEndQuery(GL_SAMPLES_PASSED);
    GLuint count = completed_result(query);
    if ((count != 0) != visible) glc_fail("Reused %s query returned stale %s result",
        indexed ? "indexed" : "regular", visible ? "zero" : "nonzero");
}
GLC_CASE(core_query_completed_lifetime, .profile = GLC_CORE)
{
    const char *vs = "#version 150 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}";
    const char *fs = "#version 150 core\nout vec4 c;void main(){c=vec4(0,1,0,1);}";
    GLuint program = glc_program(vs, fs, NULL), vao = 0, query = 0;
    glGenVertexArrays(1, &vao);glBindVertexArray(vao);glUseProgram(program);
    glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glGenQueries(1, &query);
    GLuint scratch = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &scratch);
    expect_query_error(GL_INVALID_OPERATION, "Generated but unused query");
    /* One name repeatedly changes its result; each first successful poll
       must supply the new generation to the following result getter. */
    for (unsigned pass = 0; pass < 6; ++pass) query_draw(query, !(pass & 1), false);
    query_draw(query, true, true);
    query_draw(query, false, false);
    glGetQueryObjectuiv(query, GL_TEXTURE_2D, &scratch);
    expect_query_error(GL_INVALID_ENUM, "Invalid getter pname on completed query");
    if (completed_result(query)) glc_fail("Invalid getter corrupted completed zero result");
    glBeginQuery(GL_SAMPLES_PASSED, query);
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &scratch);
    expect_query_error(GL_INVALID_OPERATION, "Getter on active reused query");
    glEndQuery(GL_SAMPLES_PASSED);
    if (completed_result(query)) glc_fail("Empty reused query returned cached samples");
    GLuint deleted = query;
    glDeleteQueries(1, &query);
    glGetQueryObjectuiv(deleted, GL_QUERY_RESULT, &scratch);
    expect_query_error(GL_INVALID_OPERATION, "Getter on deleted query");
    glGenQueries(1, &query);
    query_draw(query, true, false);query_draw(query, false, true);
    glDeleteQueries(1, &query);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 1, 0, 1);glClear(GL_COLOR_BUFFER_BIT);
    glDeleteVertexArrays(1, &vao);glDeleteProgram(program);
    expect_query_error(GL_NO_ERROR, "Query lifetime cleanup");
}
