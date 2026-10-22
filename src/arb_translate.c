/* ARB_vertex_program / ARB_fragment_program assembly to legacy GLSL.
 *
 * The output uses the GLSL 1.x built-ins (gl_Vertex, gl_ModelViewMatrix,
 * gl_TexCoord, gl_FragColor...) so it goes through the same legacy
 * rewriting, glslang and SPIRV-Cross path as application GLSL, and pairs
 * with fixed-function and GLSL stages through the shared varying locations.
 * Program environment and local parameters live in the GLMARB block; its
 * arrays are sized to what the program references. */
#include "arb_translate.h"
#include "legacy_block.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable text ------------------------------------------------------- */

struct text {
    char *data;
    size_t length, capacity;
};

static void emit(struct text *t, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void emit(struct text *t, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (t->length + (size_t)n + 1 > t->capacity) {
        t->capacity = (t->length + (size_t)n + 1) * 2;
        t->data = realloc(t->data, t->capacity);
    }
    vsnprintf(t->data + t->length, (size_t)n + 1, format, args);
    t->length += (size_t)n;
    va_end(args);
}

/* ---- tokens -------------------------------------------------------------- */

enum token_kind { TOK_END, TOK_IDENT, TOK_NUMBER, TOK_PUNCT };

struct token {
    enum token_kind kind;
    char text[64];
    double number;
    size_t position;
};

enum { MAX_NAMES = 512 };

enum name_kind { NAME_TEMP, NAME_ADDRESS, NAME_ATTRIB, NAME_PARAM, NAME_PARAM_ARRAY, NAME_OUTPUT };

struct name {
    char ident[64];
    /* The GLSL variable (arb_n<slot>): program identifiers may be GLSL
       keywords or built-in names ("dot") or contain '$'. ALIAS shares its
       target's variable and slot. */
    char glsl[16];
    int slot;
    bool alias;
    enum name_kind kind;
    char expression[160]; /* ATTRIB/PARAM/OUTPUT: GLSL expression or variable */
    int array_size;
    /* Arrays aliasing one env/local range index the block directly. */
    const char *alias_array;
    int alias_base;
    /* Program parameter counting (count_key): PARAM's binding, and one key
       per array element. */
    char key[160];
    char (*element_keys)[160];
};

struct parser {
    const char *source;
    size_t length, at;
    struct token token;
    bool vertex;
    struct text declarations, body, prologue;
    struct name names[MAX_NAMES];
    int name_count;
    int env_max, local_max; /* highest index used, -1 none */
    bool env_dynamic, local_dynamic;
    bool position_invariant;
    int fog_option; /* 0 none, 1 linear, 2 exp, 3 exp2 */
    uint32_t results_written;
    uint16_t texcoords_written;
    int draw_buffers_written;
    uint32_t attribs_used; /* generic attributes read */
    /* Vertex attribute slots bound by conventional names (vertex.position,
       vertex.color, ...) and by vertex.attrib[n]: a slot bound both ways is
       an error (ARB_vertex_program 2.14.3.1). */
    uint32_t conventional_bound, generic_bound;
    /* glGetProgramivARB counters, as Apple's implementation counts: every
       instruction, temporaries and parameters (named ones and bindings
       deduplicated, every inline literal) actually referenced, attributes
       read, address registers declared. */
    int instructions, alu_instructions, tex_instructions, address_declared;
    char (*param_keys)[160], (*attrib_keys)[160];
    int param_count, attrib_count;
    bool temp_used[MAX_NAMES];
    bool fogcoord_read;
    uint32_t sampler_declared[16]; /* per unit: bitmask of targets */
    int temp_counter;
    char error[256];
    size_t error_position;
    char error_hint[64];     /* the token Apple's messages quote */
    struct token previous;   /* the token before the current one */
    bool failed;
    /* NV program options: 0 none; fragment 1 (NV_fragment_program,
       NV_fragment_program_option) or 2 (NV_fragment_program2); vertex 2
       (NV_vertex_program2, NV_vertex_program2_option) or 3
       (NV_vertex_program3). They add precision and condition-code opcode
       suffixes, |abs| operands, conditional writes, blocks and branches,
       more opcodes and vertex texture fetch. */
    int nv;
    bool shadow_option; /* ARB_fragment_program_shadow */
    uint8_t cc_used;    /* condition code registers 0 and 1 (NV_vertex_program3) */
    uint32_t nv_helpers; /* helper functions the body calls (enum nv_helper) */
    /* IF, REP and LOOP blocks open at this point. */
    struct block { int kind, open, close, counter; bool has_else; } blocks[64];
    int block_depth;
    /* Programs with labels, CAL, RET, BRA, PUSHA or POPA run as a state
       machine: the body is a series of segments "if (glm_pc == id) {...}"
       in a loop, a branch sets glm_pc, CAL and PUSHA push onto a 4-entry
       stack (MAX_PROGRAM_CALL_DEPTH_NV) shared with RET and POPA, and
       blocks become branches. flow_candidate: the program has one of them
       (found before parsing). */
    bool flow_candidate, flow_decided, flow;
    int segments, loop_counters;
    struct label { char name[64]; int id; bool defined; } *labels;
    int label_count;
    /* result.clip[n] written as DP4 of one vector with state.clip[n].plane
       (Direct3D clip planes): the vector becomes gl_ClipVertex. */
    uint8_t clips_written;
    bool clip_vertex_ok;
    bool dest_address; /* the last destination was an address register */
    bool program_matrices; /* reads state.matrix.program[n] */
};

enum nv_helper { HELPER_PACK = 1, HELPER_RCC = 2 };

/* Adds `key` (outer parentheses ignored) to a counted set. */
static void count_key(char (**keys)[160], int *count, const char *key)
{
    size_t length = strlen(key);
    char normal[160];
    if (length >= 2 && key[0] == '(' && key[length - 1] == ')') snprintf(normal, sizeof normal, "%.*s", (int)length - 2, key + 1);
    else snprintf(normal, sizeof normal, "%s", key);
    for (int i = 0; i < *count; ++i)
        if (!strcmp((*keys)[i], normal)) return;
    if (*count % 64 == 0) *keys = realloc(*keys, (size_t)(*count + 64) * sizeof **keys);
    snprintf((*keys)[(*count)++], 160, "%s", normal);
}

static void fail(struct parser *p, const char *format, ...) __attribute__((format(printf, 2, 3)));
/* Undefined-name errors quote the name, at its line. */
static void undefined_hint(struct parser *p, const char *ident)
{
    snprintf(p->error_hint, sizeof p->error_hint, "%s", ident);
    p->error_position = p->previous.position;
}
static void fail(struct parser *p, const char *format, ...)
{
    if (p->failed) return;
    p->failed = true;
    p->error_position = p->token.position;
    snprintf(p->error_hint, sizeof p->error_hint, "%s", p->token.text);
    va_list args;
    va_start(args, format);
    vsnprintf(p->error, sizeof p->error, format, args);
    va_end(args);
}

static void next(struct parser *p)
{
    if (p->token.kind != TOK_END) p->previous = p->token;
    const char *s = p->source;
    for (;;) {
        while (p->at < p->length && isspace((unsigned char)s[p->at])) ++p->at;
        if (p->at < p->length && s[p->at] == '#') {
            while (p->at < p->length && s[p->at] != '\n') ++p->at;
            continue;
        }
        break;
    }
    struct token *t = &p->token;
    t->position = p->at;
    t->text[0] = 0;
    if (p->at >= p->length) {
        t->kind = TOK_END;
        return;
    }
    char c = s[p->at];
    if (isalpha((unsigned char)c) || c == '_' || c == '$') {
        size_t n = 0;
        while (p->at < p->length && (isalnum((unsigned char)s[p->at]) || s[p->at] == '_' || s[p->at] == '$')) {
            if (n + 1 < sizeof t->text) t->text[n++] = s[p->at];
            ++p->at;
        }
        t->text[n] = 0;
        t->kind = TOK_IDENT;
        return;
    }
    if (isdigit((unsigned char)c) || (c == '.' && p->at + 1 < p->length && isdigit((unsigned char)s[p->at + 1]))) {
        /* Scanned by hand: "0..3" is a range, not "0." then ".3". */
        size_t end = p->at;
        while (end < p->length && isdigit((unsigned char)s[end])) ++end;
        if (end < p->length && s[end] == '.' && !(end + 1 < p->length && s[end + 1] == '.')) {
            ++end;
            while (end < p->length && isdigit((unsigned char)s[end])) ++end;
        }
        if (end < p->length && (s[end] == 'e' || s[end] == 'E')) {
            size_t e = end + 1;
            if (e < p->length && (s[e] == '+' || s[e] == '-')) ++e;
            if (e < p->length && isdigit((unsigned char)s[e])) {
                end = e;
                while (end < p->length && isdigit((unsigned char)s[end])) ++end;
            }
        }
        size_t n = end - p->at;
        if (n >= sizeof t->text) n = sizeof t->text - 1;
        memcpy(t->text, s + p->at, n);
        t->text[n] = 0;
        t->number = strtod(t->text, NULL);
        p->at = end;
        t->kind = TOK_NUMBER;
        return;
    }
    t->kind = TOK_PUNCT;
    if (c == '.' && p->at + 1 < p->length && s[p->at + 1] == '.') {
        strcpy(t->text, "..");
        p->at += 2;
        return;
    }
    t->text[0] = c;
    t->text[1] = 0;
    ++p->at;
}

static bool is(struct parser *p, const char *text) { return p->token.kind != TOK_END && !strcmp(p->token.text, text); }

static bool accept(struct parser *p, const char *text)
{
    if (!is(p, text)) return false;
    next(p);
    return true;
}

static void expect(struct parser *p, const char *text)
{
    if (accept(p, text)) return;
    if (!strcmp(text, ";")) fail(p, "missing semicolon");
    else fail(p, "expected '%s'", text);
}

static int expect_integer(struct parser *p)
{
    if (p->token.kind != TOK_NUMBER) {
        fail(p, "invalid number"); /* Apple's message */
        return 0;
    }
    int v = (int)p->token.number;
    next(p);
    return v;
}

static void expect_ident(struct parser *p, char *out, size_t size)
{
    if (p->token.kind != TOK_IDENT) {
        fail(p, "expected an identifier near '%s'", p->token.text);
        out[0] = 0;
        return;
    }
    snprintf(out, size, "%s", p->token.text);
    next(p);
}

static struct name *find_name(struct parser *p, const char *ident)
{
    for (int i = 0; i < p->name_count; ++i)
        if (!strcmp(p->names[i].ident, ident)) return &p->names[i];
    return NULL;
}

static struct name *add_name(struct parser *p, const char *ident, enum name_kind kind)
{
    if (find_name(p, ident)) {
        fail(p, "'%s' is already declared", ident);
        return NULL;
    }
    if (p->name_count == MAX_NAMES) {
        fail(p, "too many names");
        return NULL;
    }
    struct name *n = &p->names[p->name_count++];
    memset(n, 0, sizeof *n);
    snprintf(n->ident, sizeof n->ident, "%s", ident);
    n->slot = (int)(n - p->names);
    snprintf(n->glsl, sizeof n->glsl, "arb_n%d", n->slot);
    n->kind = kind;
    return n;
}

/* ---- bindings ------------------------------------------------------------ */

static void format_number(char *out, size_t size, double v)
{
    snprintf(out, size, "%.9g", v);
    if (!strpbrk(out, ".eEn")) strncat(out, ".0", size - strlen(out) - 1);
}

/* "[n]" after a binding keyword, or default. */
static int optional_index(struct parser *p, int fallback)
{
    if (!accept(p, "[")) return fallback;
    int v = expect_integer(p);
    expect(p, "]");
    return v;
}

static const char *face_prefix(struct parser *p, bool *back)
{
    *back = false;
    if (is(p, ".")) {
        /* Peek: .front or .back */
        size_t save = p->at;
        struct token saved = p->token;
        next(p);
        if (is(p, "front")) { next(p); return "Front"; }
        if (is(p, "back")) { next(p); *back = true; return "Back"; }
        p->at = save;
        p->token = saved;
    }
    return "Front";
}

/* Matrix rows: fills up to 4 row expressions of a state.matrix binding. */
static int matrix_rows(struct parser *p, char rows[4][160], int *first_row)
{
    char base[96];
    int index = 0;
    char which[32];
    expect_ident(p, which, sizeof which);
    if (!strcmp(which, "modelview")) {
        index = optional_index(p, 0);
        snprintf(base, sizeof base, "gl_ModelViewMatrix");
        if (index) fail(p, "only modelview[0] is supported");
    } else if (!strcmp(which, "projection")) snprintf(base, sizeof base, "gl_ProjectionMatrix");
    else if (!strcmp(which, "mvp")) snprintf(base, sizeof base, "gl_ModelViewProjectionMatrix");
    else if (!strcmp(which, "texture")) {
        index = optional_index(p, 0);
        snprintf(base, sizeof base, "gl_TextureMatrix");
    } else if (!strcmp(which, "program")) {
        /* glMatrixMode(GL_MATRIXn_ARB): the GLMARB block's
           glm_arb_matrices[8n..8n+3] are its columns, [8n+4..8n+7] its
           inverse's. */
        index = optional_index(p, 0);
        if (index < 0 || index > 7) fail(p, "invalid program matrix");
        p->program_matrices = true;
        snprintf(base, sizeof base, "program");
    } else if (!strcmp(which, "palette")) {
        optional_index(p, 0);
        snprintf(base, sizeof base, "identity");
    } else {
        fail(p, "unknown matrix '%s'", which);
        return 0;
    }
    /* Modifier: rows of M are columns of transpose(M). */
    const char *suffix = "Transpose"; /* rows of M */
    int lo = 0, hi = 3;
    while (accept(p, ".")) {
        char word[32];
        expect_ident(p, word, sizeof word);
        if (!strcmp(word, "inverse")) suffix = "InverseTranspose";
        else if (!strcmp(word, "transpose")) suffix = !strcmp(suffix, "InverseTranspose") ? "Inverse" : "";
        else if (!strcmp(word, "invtrans")) suffix = "Inverse";
        else if (!strcmp(word, "row")) {
            expect(p, "[");
            lo = hi = expect_integer(p);
            if (accept(p, "..")) hi = expect_integer(p);
            expect(p, "]");
            break;
        } else fail(p, "unknown matrix modifier '%s'", word);
    }
    if (lo < 0 || hi > 3 || lo > hi) fail(p, "bad matrix row range");
    *first_row = lo;
    for (int r = lo; r <= hi && r - lo < 4; ++r) {
        if (!strcmp(base, "identity"))
            snprintf(rows[r - lo], 160, "vec4(%s)", r == 0 ? "1.0, 0.0, 0.0, 0.0" : r == 1 ? "0.0, 1.0, 0.0, 0.0"
                                                     : r == 2 ? "0.0, 0.0, 1.0, 0.0" : "0.0, 0.0, 0.0, 1.0");
        else if (!strcmp(base, "program")) {
            /* Rows of M or of its inverse gather a component of each
               column; the transposed forms are the columns. */
            bool inverse = strstr(suffix, "Inverse") != NULL, rows_of = strstr(suffix, "Transpose") != NULL;
            int first = index * 8 + (inverse ? 4 : 0);
            if (rows_of)
                snprintf(rows[r - lo], 160, "vec4(glm_arb_matrices[%d][%d], glm_arb_matrices[%d][%d], glm_arb_matrices[%d][%d], "
                         "glm_arb_matrices[%d][%d])", first, r, first + 1, r, first + 2, r, first + 3, r);
            else
                snprintf(rows[r - lo], 160, "glm_arb_matrices[%d]", first + r);
        } else if (!strcmp(base, "gl_TextureMatrix"))
            snprintf(rows[r - lo], 160, "gl_TextureMatrix%s[%d][%d]", suffix, index, r);
        else
            snprintf(rows[r - lo], 160, "%s%s[%d]", base, suffix, r);
    }
    return hi - lo + 1;
}

static void use_parameter(struct parser *p, bool env, int index)
{
    int limit = !env ? GLM_ARB_LOCAL_PARAMETERS : p->vertex ? GLM_ARB_ENV_PARAMETERS : GLM_ARB_FRAGMENT_ENV_PARAMETERS;
    if (index < 0 || index >= limit) {
        fail(p, "parameter index %d out of range", index);
        return;
    }
    int *max = env ? &p->env_max : &p->local_max;
    if (index > *max) *max = index;
}

/* A state.* or program.* binding: up to `capacity` vec4 expressions (more
   than one for matrices and ranges). Returns the count. `alias` reports a
   single env/local range for direct indexing. */
static int parameter_binding(struct parser *p, char (*out)[160], int capacity, const char **alias, int *alias_base)
{
    if (alias) *alias = NULL;
    char root[32];
    expect_ident(p, root, sizeof root);
    if (!strcmp(root, "program")) {
        expect(p, ".");
        char which[16];
        expect_ident(p, which, sizeof which);
        bool env = !strcmp(which, "env");
        if (!env && strcmp(which, "local")) fail(p, "expected program.env or program.local");
        expect(p, "[");
        int lo = expect_integer(p), hi = lo;
        if (accept(p, "..")) hi = expect_integer(p);
        expect(p, "]");
        use_parameter(p, env, lo);
        use_parameter(p, env, hi);
        if (alias && hi > lo) {
            *alias = env ? "glm_arb_env" : "glm_arb_local";
            *alias_base = lo;
        }
        int n = 0;
        for (int i = lo; i <= hi && n < capacity; ++i)
            snprintf(out[n++], 160, "%s[%d]", env ? "glm_arb_env" : "glm_arb_local", i);
        return n;
    }
    if (strcmp(root, "state")) {
        fail(p, "unknown binding '%s'", root);
        return 0;
    }
    expect(p, ".");
    char group[32];
    expect_ident(p, group, sizeof group);
    if (!strcmp(group, "matrix")) {
        expect(p, ".");
        char rows[4][160];
        int first;
        int n = matrix_rows(p, rows, &first);
        for (int i = 0; i < n && i < capacity; ++i) memcpy(out[i], rows[i], 160);
        return n < capacity ? n : capacity;
    }
    if (!strcmp(group, "material")) {
        bool back;
        const char *face = face_prefix(p, &back);
        expect(p, ".");
        char prop[32];
        expect_ident(p, prop, sizeof prop);
        if (!strcmp(prop, "shininess")) snprintf(out[0], 160, "vec4(gl_%sMaterial.shininess, 0.0, 0.0, 1.0)", face);
        else if (!strcmp(prop, "ambient") || !strcmp(prop, "diffuse") || !strcmp(prop, "specular") || !strcmp(prop, "emission"))
            snprintf(out[0], 160, "gl_%sMaterial.%s", face, prop);
        else fail(p, "unknown material property '%s'", prop);
        return 1;
    }
    if (!strcmp(group, "light")) {
        int n = optional_index(p, 0);
        expect(p, ".");
        char prop[32];
        expect_ident(p, prop, sizeof prop);
        if (!strcmp(prop, "attenuation"))
            snprintf(out[0], 160, "vec4(gl_LightSource[%d].constantAttenuation, gl_LightSource[%d].linearAttenuation, "
                                  "gl_LightSource[%d].quadraticAttenuation, gl_LightSource[%d].spotExponent)", n, n, n, n);
        else if (!strcmp(prop, "spot")) {
            expect(p, ".");
            char d[32];
            expect_ident(p, d, sizeof d);
            snprintf(out[0], 160, "vec4(gl_LightSource[%d].spotDirection, gl_LightSource[%d].spotCosCutoff)", n, n);
        } else if (!strcmp(prop, "half")) snprintf(out[0], 160, "vec4(gl_LightSource[%d].halfVector.xyz, 1.0)", n);
        else if (!strcmp(prop, "ambient") || !strcmp(prop, "diffuse") || !strcmp(prop, "specular") || !strcmp(prop, "position"))
            snprintf(out[0], 160, "gl_LightSource[%d].%s", n, prop);
        else fail(p, "unknown light property '%s'", prop);
        return 1;
    }
    if (!strcmp(group, "lightmodel")) {
        expect(p, ".");
        char prop[32];
        expect_ident(p, prop, sizeof prop);
        if (!strcmp(prop, "ambient")) snprintf(out[0], 160, "gl_LightModel.ambient");
        else if (!strcmp(prop, "scenecolor")) snprintf(out[0], 160, "gl_FrontLightModelProduct.sceneColor");
        else if (!strcmp(prop, "front") || !strcmp(prop, "back")) {
            expect(p, ".");
            char s[32];
            expect_ident(p, s, sizeof s);
            snprintf(out[0], 160, "gl_%sLightModelProduct.sceneColor", !strcmp(prop, "front") ? "Front" : "Back");
        } else fail(p, "unknown lightmodel property '%s'", prop);
        return 1;
    }
    if (!strcmp(group, "lightprod")) {
        int n = optional_index(p, 0);
        bool back;
        const char *face = face_prefix(p, &back);
        expect(p, ".");
        char prop[32];
        expect_ident(p, prop, sizeof prop);
        /* The product's alpha is the material's, as Apple's (and NVIDIA's)
           ARB programs read it. */
        snprintf(out[0], 160, "vec4(gl_%sLightProduct[%d].%s.rgb, gl_%sMaterial.%s.a)", face, n, prop, face, prop);
        return 1;
    }
    if (!strcmp(group, "texgen")) {
        int n = optional_index(p, 0);
        expect(p, ".");
        char space[16], coord[8];
        expect_ident(p, space, sizeof space);
        expect(p, ".");
        expect_ident(p, coord, sizeof coord);
        snprintf(out[0], 160, "gl_%sPlane%c[%d]", !strcmp(space, "eye") ? "Eye" : "Object", toupper(coord[0]), n);
        return 1;
    }
    if (!strcmp(group, "fog")) {
        expect(p, ".");
        char prop[16];
        expect_ident(p, prop, sizeof prop);
        if (!strcmp(prop, "color")) snprintf(out[0], 160, "gl_Fog.color");
        else snprintf(out[0], 160, "vec4(gl_Fog.density, gl_Fog.start, gl_Fog.end, gl_Fog.scale)");
        return 1;
    }
    if (!strcmp(group, "clip")) {
        int n = optional_index(p, 0);
        expect(p, ".");
        char prop[16];
        expect_ident(p, prop, sizeof prop);
        snprintf(out[0], 160, "gl_ClipPlane[%d]", n);
        return 1;
    }
    if (!strcmp(group, "point")) {
        expect(p, ".");
        char prop[16];
        expect_ident(p, prop, sizeof prop);
        if (!strcmp(prop, "size"))
            snprintf(out[0], 160, "vec4(gl_Point.size, gl_Point.sizeMin, gl_Point.sizeMax, gl_Point.fadeThresholdSize)");
        else
            snprintf(out[0], 160, "vec4(gl_Point.distanceConstantAttenuation, gl_Point.distanceLinearAttenuation, "
                                  "gl_Point.distanceQuadraticAttenuation, 1.0)");
        return 1;
    }
    if (!strcmp(group, "texenv")) {
        int n = optional_index(p, 0);
        expect(p, ".");
        char prop[16];
        expect_ident(p, prop, sizeof prop);
        snprintf(out[0], 160, "gl_TextureEnvColor[%d]", n);
        return 1;
    }
    if (!strcmp(group, "depth")) {
        expect(p, ".");
        char prop[16];
        expect_ident(p, prop, sizeof prop);
        snprintf(out[0], 160, "vec4(gl_DepthRange.near, gl_DepthRange.far, gl_DepthRange.diff, 1.0)");
        return 1;
    }
    fail(p, "unknown state '%s'", group);
    return 0;
}

/* {a, b, c, d} with missing components (0, 0, 0, 1); a lone number
   replicates when `scalar_replicates`. */
static void constant_vector(struct parser *p, char *out, bool scalar_replicates)
{
    double v[4] = {0, 0, 0, 1};
    int n = 0;
    if (accept(p, "{")) {
        do {
            double sign = accept(p, "-") ? -1 : (accept(p, "+"), 1);
            if (p->token.kind != TOK_NUMBER) {
                fail(p, "expected a number near '%s'", p->token.text);
                return;
            }
            if (n < 4) v[n] = sign * p->token.number;
            ++n;
            next(p);
        } while (accept(p, ","));
        expect(p, "}");
        if (n > 4) fail(p, "too many components");
    } else {
        double sign = accept(p, "-") ? -1 : (accept(p, "+"), 1);
        if (p->token.kind != TOK_NUMBER) {
            fail(p, "expected a number near '%s'", p->token.text);
            return;
        }
        v[0] = sign * p->token.number;
        next(p);
        if (scalar_replicates) v[1] = v[2] = v[3] = v[0];
    }
    char c[4][40];
    for (int i = 0; i < 4; ++i) format_number(c[i], sizeof c[i], v[i]);
    snprintf(out, 160, "vec4(%s, %s, %s, %s)", c[0], c[1], c[2], c[3]);
}

static bool starts_constant(struct parser *p)
{
    return is(p, "{") || p->token.kind == TOK_NUMBER || is(p, "-") || is(p, "+");
}

/* One PARAM initializer item (possibly several vec4s). */
static int parameter_item(struct parser *p, char (*out)[160], int capacity, bool scalar_replicates, const char **alias,
                          int *alias_base)
{
    if (alias) *alias = NULL;
    if (starts_constant(p)) {
        constant_vector(p, out[0], scalar_replicates);
        return 1;
    }
    return parameter_binding(p, out, capacity, alias, alias_base);
}

/* Generic attributes that alias a conventional four-component one read it
   (the GLSL built-ins share the slot and current value). The normal and fog
   coordinate built-ins have fewer components, so generic attributes 2 and 5
   are generic inputs of their own: a 4-component glVertexAttribPointer(5)
   array (Call of Duty 2's vertex colour) keeps all of its components. */
static const char *legacy_attribute(int index)
{
    switch (index) {
    case 0: return "gl_Vertex";
    case 3: return "gl_Color";
    case 4: return "gl_SecondaryColor";
    default: return NULL;
    }
}

/* ".primary" / ".secondary" after a colour binding; anything else after
   the dot (a swizzle) is left alone. Returns true for secondary. */
static bool color_qualifier(struct parser *p)
{
    if (!is(p, ".")) return false;
    size_t save = p->at;
    struct token saved = p->token;
    next(p);
    if (is(p, "primary")) { next(p); return false; }
    if (is(p, "secondary")) { next(p); return true; }
    p->at = save;
    p->token = saved;
    return false;
}

/* vertex.* / fragment.* attribute bindings. */
static void attribute_binding(struct parser *p, char *out)
{
    char root[16];
    expect_ident(p, root, sizeof root);
    expect(p, ".");
    char what[32];
    expect_ident(p, what, sizeof what);
    if (p->vertex) {
        if (strcmp(root, "vertex")) { fail(p, "vertex programs read vertex.* attributes"); return; }
        if (!strcmp(what, "position")) {
            p->conventional_bound |= 1u << 0;
            snprintf(out, 160, "gl_Vertex");
        } else if (!strcmp(what, "normal")) {
            p->conventional_bound |= 1u << 2;
            snprintf(out, 160, "vec4(gl_Normal, 1.0)");
        } else if (!strcmp(what, "fogcoord")) {
            p->conventional_bound |= 1u << 5;
            snprintf(out, 160, "vec4(gl_FogCoord, 0.0, 0.0, 1.0)");
        }
        else if (!strcmp(what, "weight") || !strcmp(what, "matrixindex")) {
            optional_index(p, 0);
            if (!strcmp(what, "weight")) p->conventional_bound |= 1u << 1;
            snprintf(out, 160, "vec4(1.0, 0.0, 0.0, 0.0)");
        } else if (!strcmp(what, "color")) {
            bool secondary = color_qualifier(p);
            p->conventional_bound |= 1u << (secondary ? 4 : 3);
            snprintf(out, 160, secondary ? "gl_SecondaryColor" : "gl_Color");
        } else if (!strcmp(what, "texcoord")) {
            int n = optional_index(p, 0);
            if (n >= 0 && n < 8) p->conventional_bound |= 1u << (8 + n);
            snprintf(out, 160, "gl_MultiTexCoord%d", n);
        } else if (!strcmp(what, "attrib")) {
            int n = optional_index(p, 0);
            if (n < 0 || n >= 16) { fail(p, "attribute index out of range"); return; }
            p->generic_bound |= 1u << n;
            const char *legacy = n >= 8 ? NULL : legacy_attribute(n);
            if (n >= 8) snprintf(out, 160, "gl_MultiTexCoord%d", n - 8);
            else if (legacy) snprintf(out, 160, "%s", legacy);
            else {
                p->attribs_used |= 1u << n;
                snprintf(out, 160, "arb_attrib%d", n);
            }
        } else fail(p, "unknown vertex attribute '%s'", what);
        return;
    }
    if (strcmp(root, "fragment")) { fail(p, "fragment programs read fragment.* attributes"); return; }
    if (!strcmp(what, "color")) {
        bool secondary = color_qualifier(p);
        snprintf(out, 160, secondary ? "gl_SecondaryColor" : "gl_Color");
    } else if (!strcmp(what, "texcoord")) {
        int n = optional_index(p, 0);
        snprintf(out, 160, "gl_TexCoord[%d]", n);
    } else if (!strcmp(what, "fogcoord")) {
        p->fogcoord_read = true;
        snprintf(out, 160, "vec4(gl_FogFragCoord, 0.0, 0.0, 1.0)");
    } else if (!strcmp(what, "position")) snprintf(out, 160, "gl_FragCoord");
    /* NV_fragment_program2: +1 for front-facing primitives, -1 for back. */
    else if (!strcmp(what, "facing") && p->nv >= 2) snprintf(out, 160, "vec4(gl_FrontFacing ? 1.0 : -1.0, 0.0, 0.0, 1.0)");
    else if (!strcmp(what, "facing")) { /* Apple's message without NV_fragment_program2 */
        fail(p, "malformed declaration");
        snprintf(p->error_hint, sizeof p->error_hint, "%s", what);
    } else fail(p, "unknown fragment attribute '%s'", what);
}

enum result_slot {
    RESULT_POSITION, RESULT_COLOR0, RESULT_COLOR1, RESULT_BACK0, RESULT_BACK1, RESULT_FOG, RESULT_POINT, RESULT_DEPTH,
    RESULT_TEX0, /* 8 */ RESULT_DRAW0 = RESULT_TEX0 + 8, /* 8 draw buffers */ RESULT_CLIP0 = RESULT_DRAW0 + 8,
    RESULT_COUNT = RESULT_CLIP0 + 8,
};

static const char *result_variable(int slot)
{
    static char names[RESULT_COUNT][24];
    snprintf(names[slot], sizeof names[slot], "glm_result%d", slot);
    return names[slot];
}

/* result.* binding to a result slot. */
static int result_binding(struct parser *p)
{
    char root[16];
    expect_ident(p, root, sizeof root);
    if (strcmp(root, "result")) { fail(p, "expected result.*"); return 0; }
    expect(p, ".");
    char what[32];
    expect_ident(p, what, sizeof what);
    int slot = -1;
    if (p->vertex) {
        if (!strcmp(what, "position")) slot = RESULT_POSITION;
        else if (!strcmp(what, "fogcoord")) slot = RESULT_FOG;
        else if (!strcmp(what, "pointsize")) slot = RESULT_POINT;
        else if (!strcmp(what, "texcoord")) slot = RESULT_TEX0 + optional_index(p, 0);
        else if (!strcmp(what, "clip") && p->nv) {
            int n = optional_index(p, 0);
            slot = n >= 0 && n < 8 ? RESULT_CLIP0 + n : -1;
        }
        else if (!strcmp(what, "color")) {
            bool back = false, secondary = false;
            /* .primary .secondary .front[.primary|.secondary] .back[...] */
            for (int k = 0; k < 2 && is(p, "."); ++k) {
                size_t save = p->at;
                struct token saved = p->token;
                next(p);
                if (is(p, "front")) next(p);
                else if (is(p, "back")) { back = true; next(p); }
                else if (is(p, "primary")) next(p);
                else if (is(p, "secondary")) { secondary = true; next(p); }
                else { p->at = save; p->token = saved; break; }
            }
            slot = back ? (secondary ? RESULT_BACK1 : RESULT_BACK0) : (secondary ? RESULT_COLOR1 : RESULT_COLOR0);
        }
    } else {
        if (!strcmp(what, "color")) slot = RESULT_DRAW0 + optional_index(p, 0);
        else if (!strcmp(what, "depth")) slot = RESULT_DEPTH;
    }
    if (slot < 0 || slot >= RESULT_COUNT) {
        fail(p, "unknown result '%s'", what);
        return 0;
    }
    return slot;
}

/* ---- operands ------------------------------------------------------------ */

static int swizzle_component(char c)
{
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
}

/* ".xyzw"-style suffix after an operand. Returns a GLSL swizzle or "". */
static void source_swizzle(struct parser *p, char *out)
{
    out[0] = 0;
    if (!is(p, ".")) return;
    size_t save = p->at;
    struct token saved = p->token;
    next(p);
    if (p->token.kind != TOK_IDENT) {
        p->at = save;
        p->token = saved;
        return;
    }
    const char *s = p->token.text;
    size_t n = strlen(s);
    if (n != 1 && n != 4) {
        p->at = save;
        p->token = saved;
        return;
    }
    char comps[5] = {0};
    for (size_t i = 0; i < n; ++i) {
        int c = swizzle_component(s[i]);
        if (c < 0) { p->at = save; p->token = saved; return; }
        comps[i] = "xyzw"[c];
    }
    if (n == 1) comps[1] = comps[2] = comps[3] = comps[0];
    snprintf(out, 8, ".%s", comps);
    next(p);
}

/* A source operand as a vec4 GLSL expression. */
static void source_operand(struct parser *p, char *out, size_t size)
{
    bool negate = false;
    if (accept(p, "-")) negate = true;
    else accept(p, "+");
    /* NV: |operand| takes the absolute value, before negation. */
    bool absolute = p->nv && accept(p, "|");
    char base[200] = {0};
    if (p->token.kind == TOK_NUMBER || is(p, "{")) {
        char c[160], key[160];
        constant_vector(p, c, true);
        snprintf(base, sizeof base, "%s", c);
        /* Every inline constant counts as a parameter of its own, as in
           Apple's implementation (named PARAMs and bindings count once). */
        snprintf(key, sizeof key, "literal %d %s", p->param_count, c);
        count_key(&p->param_keys, &p->param_count, key);
    } else if (p->token.kind == TOK_IDENT) {
        if (is(p, "vertex") || is(p, "fragment")) {
            char e[160];
            attribute_binding(p, e);
            snprintf(base, sizeof base, "(%s)", e);
            count_key(&p->attrib_keys, &p->attrib_count, e);
        } else if (is(p, "state") || is(p, "program")) {
            char e[4][160];
            if (parameter_binding(p, e, 1, NULL, NULL) < 1) return;
            snprintf(base, sizeof base, "(%s)", e[0]);
            count_key(&p->param_keys, &p->param_count, e[0]);
        } else {
            char ident[64];
            expect_ident(p, ident, sizeof ident);
            struct name *n = find_name(p, ident);
            if (!n) {
                fail(p, "undefined source identifier");
                undefined_hint(p, ident);
                return;
            }
            if (n->kind == NAME_PARAM_ARRAY) {
                expect(p, "[");
                char index[96];
                if (p->token.kind == TOK_NUMBER) {
                    snprintf(index, sizeof index, "%d", expect_integer(p));
                } else {
                    /* a.x [+ offset] */
                    char address[64];
                    expect_ident(p, address, sizeof address);
                    struct name *a = find_name(p, address);
                    if (!a || a->kind != NAME_ADDRESS) fail(p, "'%s' is not an address register", address);
                    expect(p, ".");
                    char comp[8];
                    expect_ident(p, comp, sizeof comp);
                    /* NV_vertex_program2_option: any component of A0. */
                    int c = swizzle_component(comp[0]);
                    if (c < 0 || comp[1] || (c && !p->nv)) fail(p, "invalid address register component");
                    int offset = 0;
                    if (accept(p, "+")) offset = expect_integer(p);
                    else if (accept(p, "-")) offset = -expect_integer(p);
                    snprintf(index, sizeof index, "clamp(%s.%c + %d, 0, %d)", a ? a->glsl : address, "xyzw"[c < 0 ? 0 : c], offset + (n->alias_array ? n->alias_base : 0),
                             n->alias_array ? n->alias_base + n->array_size - 1 : n->array_size - 1);
                    if (n->alias_array) {
                        if (!strcmp(n->alias_array, "glm_arb_env")) p->env_dynamic = true;
                        else p->local_dynamic = true;
                    }
                    /* Relative addressing may read every element. */
                    for (int i = 0; n->element_keys && i < n->array_size; ++i)
                        count_key(&p->param_keys, &p->param_count, n->element_keys[i]);
                    expect(p, "]");
                    snprintf(base, sizeof base, "%s[%s]", n->alias_array ? n->alias_array : n->glsl, index);
                    goto swizzle;
                }
                expect(p, "]");
                if (n->alias_array) snprintf(base, sizeof base, "%s[%d]", n->alias_array, n->alias_base + atoi(index));
                else snprintf(base, sizeof base, "%s[%s]", n->glsl, index);
                int element = atoi(index);
                if (n->element_keys && element >= 0 && element < n->array_size)
                    count_key(&p->param_keys, &p->param_count, n->element_keys[element]);
            } else if (n->kind == NAME_TEMP || n->kind == NAME_PARAM || n->kind == NAME_ATTRIB) {
                snprintf(base, sizeof base, "%s", n->kind == NAME_TEMP ? n->glsl : n->expression);
                if (n->kind == NAME_TEMP) p->temp_used[n->slot] = true;
                else if (n->kind == NAME_PARAM) count_key(&p->param_keys, &p->param_count, n->key);
                else count_key(&p->attrib_keys, &p->attrib_count, n->key);
            } else if (n->kind == NAME_OUTPUT) {
                fail(p, "result registers cannot be read");
                return;
            } else {
                fail(p, "'%s' cannot be read", ident);
                return;
            }
        }
    } else {
        fail(p, "undefined source identifier"); /* Apple's message */
        return;
    }
swizzle:;
    char swz[8];
    source_swizzle(p, swz);
    if (absolute) {
        expect(p, "|");
        snprintf(out, size, "%sabs(%s%s)", negate ? "-" : "", base, swz);
        return;
    }
    snprintf(out, size, "%s(%s%s)", negate ? "-" : "", base, swz);
}

/* NV condition-code test: EQ NE LT LE GT GE TR FL, a register digit (0 or
   1, NV_vertex_program3 only) and a swizzle. Returns a bvec4 expression
   over glm_cc0 / glm_cc1, or false, consuming nothing, if the token is not
   a test. A code of an unordered (NaN) result passes NE only. */
static bool condition_test(struct parser *p, char *out, size_t size)
{
    static const char *const tests[] = {"EQ", "NE", "LT", "LE", "GT", "GE", "TR", "FL"};
    static const char *const functions[] = {"equal", "notEqual", "lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual"};
    if (p->token.kind != TOK_IDENT) return false;
    char word[16];
    snprintf(word, sizeof word, "%s", p->token.text);
    size_t n = strlen(word);
    int reg = 0;
    if (n == 3 && p->vertex && p->nv >= 3 && (word[2] == '0' || word[2] == '1')) {
        reg = word[2] - '0';
        word[--n] = 0;
    }
    int which = -1;
    for (int i = 0; i < 8; ++i)
        if (!strcmp(word, tests[i])) which = i;
    if (which < 0) return false;
    next(p);
    char swz[8];
    source_swizzle(p, swz);
    if (!swz[0]) snprintf(swz, sizeof swz, ".xyzw");
    if (which == 6) snprintf(out, size, "bvec4(true)");
    else if (which == 7) snprintf(out, size, "bvec4(false)");
    else {
        p->cc_used |= (uint8_t)(1u << reg);
        snprintf(out, size, "%s(glm_cc%d%s, vec4(0.0))", functions[which], reg, swz);
    }
    return true;
}

/* An optional "(test)" (after a destination, CAL, BRA, RET or BRK): the
   bvec4 test, or "" when there is none. */
static void condition_mask(struct parser *p, char *out, size_t size)
{
    out[0] = 0;
    if (!accept(p, "(")) return;
    if (!condition_test(p, out, size)) {
        fail(p, "invalid condition mask");
        return;
    }
    expect(p, ")");
}

/* Destination: variable and write mask ("xyzw" subset). */
static void destination(struct parser *p, char *variable, size_t size, char *mask, int *slot)
{
    *slot = -1;
    p->dest_address = false;
    if (p->token.kind != TOK_IDENT) {
        fail(p, "undefined destination identifier");
        return;
    }
    if (is(p, "result")) {
        *slot = result_binding(p);
        snprintf(variable, size, "%s", result_variable(*slot));
    } else {
        char ident[64];
        expect_ident(p, ident, sizeof ident);
        struct name *n = find_name(p, ident);
        if (!n) {
            fail(p, "undefined destination identifier");
            undefined_hint(p, ident);
            return;
        }
        if (n->kind == NAME_OUTPUT) {
            *slot = atoi(n->expression);
            snprintf(variable, size, "%s", result_variable(*slot));
        } else if (n->kind == NAME_ADDRESS && n->alias) {
            fail(p, "address register alias '%s' cannot be written", ident); /* as Apple's */
            return;
        } else if (n->kind == NAME_TEMP || n->kind == NAME_ADDRESS) {
            snprintf(variable, size, "%s", n->glsl);
            if (n->kind == NAME_TEMP) p->temp_used[n->slot] = true;
            p->dest_address = n->kind == NAME_ADDRESS;
        } else {
            fail(p, "'%s' cannot be written", ident);
            return;
        }
    }
    strcpy(mask, "xyzw");
    if (accept(p, ".")) {
        char m[16];
        expect_ident(p, m, sizeof m);
        size_t n = 0;
        int last = -1;
        for (size_t i = 0; m[i]; ++i) {
            int c = swizzle_component(m[i]);
            if (c <= last) {
                fail(p, "bad write mask '%s'", m);
                return;
            }
            last = c;
            mask[n++] = "xyzw"[c];
        }
        mask[n] = 0;
    }
    if (*slot >= 0) {
        p->results_written |= 1u << *slot;
        if (*slot >= RESULT_TEX0 && *slot < RESULT_TEX0 + 8) p->texcoords_written |= (uint16_t)(1 << (*slot - RESULT_TEX0));
        if (*slot >= RESULT_DRAW0 && *slot - RESULT_DRAW0 + 1 > p->draw_buffers_written)
            p->draw_buffers_written = *slot - RESULT_DRAW0 + 1;
    }
}

/* texture[n], TARGET */
static void texture_operand(struct parser *p, int *unit, char *target, size_t size)
{
    char word[16];
    expect_ident(p, word, sizeof word);
    if (strcmp(word, "texture")) fail(p, "expected texture[n]");
    *unit = optional_index(p, 0);
    if (*unit < 0 || *unit >= 16) fail(p, "texture unit out of range");
    expect(p, ",");
    /* "2D" scans as the number 2 and the identifier D. */
    if (p->token.kind == TOK_NUMBER) {
        int dims = expect_integer(p);
        char rest[8];
        expect_ident(p, rest, sizeof rest);
        snprintf(target, size, "%d%s", dims, rest);
    } else {
        expect_ident(p, target, size);
    }
}

enum { TARGET_1D, TARGET_2D, TARGET_3D, TARGET_CUBE, TARGET_RECT, TARGET_SHADOW1D, TARGET_SHADOW2D, TARGET_SHADOWRECT };

static int target_code(struct parser *p, const char *t)
{
    static const char *const names[] = {"1D", "2D", "3D", "CUBE", "RECT", "SHADOW1D", "SHADOW2D", "SHADOWRECT"};
    for (int i = 0; i < 8; ++i) {
        if (strcmp(t, names[i])) continue;
        /* Shadow targets need OPTION ARB_fragment_program_shadow; vertex
           programs (NV_vertex_program3) have none. */
        if (i >= TARGET_SHADOW1D && (p->vertex || !p->shadow_option)) break;
        return i;
    }
    fail(p, "malformed source texture");
    snprintf(p->error_hint, sizeof p->error_hint, "%s", t);
    return TARGET_2D;
}

/* TEX, TXP, TXB, TXL (level of detail coord.w) and TXD (derivatives ddx,
   ddy). Vertex programs sample level 0 (NV_vertex_program3: TXB biases
   it). One-dimensional textures are 2D textures one texel high. */
static void texture_sample(struct parser *p, const char *op, const char *coord, const char *ddx, const char *ddy,
                           int unit, int target, char *out, size_t size)
{
    static const char *const sampler_types[] = {"sampler2D", "sampler2D", "sampler3D", "samplerCube", "sampler2D",
                                                "sampler2DShadow", "sampler2DShadow", "sampler2DShadow"};
    char sampler[40];
    snprintf(sampler, sizeof sampler, "glm_arb_tex%d_%d", unit, target);
    if (!(p->sampler_declared[unit] & (1u << target))) {
        p->sampler_declared[unit] |= 1u << target;
        emit(&p->declarations, "uniform %s %s;\n", sampler_types[target], sampler);
    }
    bool project = !strcmp(op, "TXP"), bias = !strcmp(op, "TXB"), lod = !strcmp(op, "TXL"), grad = !strcmp(op, "TXD");
    if (p->vertex) {
        lod |= bias;
        bias = false;
    }
    /* Rectangle textures have one level, and their unnormalized samplers
       take no level of detail. */
    if (target == TARGET_RECT || target == TARGET_SHADOWRECT) bias = lod = grad = false;
    char c[300];
    /* Projection divides by q for every target but cube maps. */
    if (project && target != TARGET_CUBE) snprintf(c, sizeof c, "(%s / %s.w)", coord, coord);
    else snprintf(c, sizeof c, "%s", coord);
    char where[400], gx[300] = "", gy[300] = "";
    switch (target) {
    case TARGET_1D:
        snprintf(where, sizeof where, "vec2(%s.x, 0.5)", c);
        snprintf(gx, sizeof gx, "vec2(%s.x, 0.0)", ddx);
        snprintf(gy, sizeof gy, "vec2(%s.x, 0.0)", ddy);
        break;
    case TARGET_2D: case TARGET_RECT:
        snprintf(where, sizeof where, "%s.xy", c);
        snprintf(gx, sizeof gx, "%s.xy", ddx);
        snprintf(gy, sizeof gy, "%s.xy", ddy);
        break;
    case TARGET_3D: case TARGET_CUBE:
        snprintf(where, sizeof where, "%s.xyz", c);
        snprintf(gx, sizeof gx, "%s.xyz", ddx);
        snprintf(gy, sizeof gy, "%s.xyz", ddy);
        break;
    case TARGET_SHADOW1D:
        snprintf(where, sizeof where, "vec3(%s.x, 0.5, %s.z)", c, c);
        snprintf(gx, sizeof gx, "vec2(%s.x, 0.0)", ddx);
        snprintf(gy, sizeof gy, "vec2(%s.x, 0.0)", ddy);
        break;
    default: /* SHADOW2D, SHADOWRECT */
        snprintf(where, sizeof where, "%s.xyz", c);
        snprintf(gx, sizeof gx, "%s.xy", ddx);
        snprintf(gy, sizeof gy, "%s.xy", ddy);
        break;
    }
    char call[1100];
    if (grad) snprintf(call, sizeof call, "textureGrad(%s, %s, %s, %s)", sampler, where, gx, gy);
    else if (lod) snprintf(call, sizeof call, "textureLod(%s, %s, %s.w)", sampler, where, coord);
    else if (p->vertex && target != TARGET_RECT) snprintf(call, sizeof call, "textureLod(%s, %s, 0.0)", sampler, where);
    else if (bias) snprintf(call, sizeof call, "texture(%s, %s, %s.w)", sampler, where, coord);
    else snprintf(call, sizeof call, "texture(%s, %s)", sampler, where);
    if (target >= TARGET_SHADOW1D) snprintf(out, size, "vec4(vec3(%s), 1.0)", call);
    else snprintf(out, size, "%s", call);
}

/* ---- statements ---------------------------------------------------------- */

/* vertex, fragment: the lowest option level (struct parser.nv) that has
   the opcode in that stage; -1 if none does. */
struct opcode {
    const char *name;
    int sources;
    signed char vertex, fragment;
};

static const struct opcode opcodes[] = {
    {"ABS", 1, 0, 0}, {"ADD", 2, 0, 0}, {"ARL", 1, 0, -1}, {"CMP", 3, -1, 0}, {"COS", 1, 2, 0}, {"DP3", 2, 0, 0},
    {"DP4", 2, 0, 0}, {"DPH", 2, 0, 0}, {"DST", 2, 0, 0}, {"EX2", 1, 0, 0}, {"EXP", 1, 0, -1}, {"FLR", 1, 0, 0},
    {"FRC", 1, 0, 0}, {"KIL", 1, -1, 0}, {"LG2", 1, 0, 0}, {"LIT", 1, 0, 0}, {"LOG", 1, 0, -1}, {"LRP", 3, -1, 0},
    {"MAD", 3, 0, 0}, {"MAX", 2, 0, 0}, {"MIN", 2, 0, 0}, {"MOV", 1, 0, 0}, {"MUL", 2, 0, 0}, {"POW", 2, 0, 0},
    {"RCP", 1, 0, 0}, {"RSQ", 1, 0, 0}, {"SCS", 1, -1, 0}, {"SGE", 2, 0, 0}, {"SIN", 1, 2, 0}, {"SLT", 2, 0, 0},
    {"SUB", 2, 0, 0}, {"SWZ", 1, 0, 0}, {"TEX", 1, 3, 0}, {"TXB", 1, 3, 0}, {"TXP", 1, 3, 0}, {"XPD", 2, 0, 0},
    /* NV_fragment_program_option, NV_fragment_program2,
       NV_vertex_program2_option, NV_vertex_program3 */
    {"SEQ", 2, 2, 1}, {"SNE", 2, 2, 1}, {"SGT", 2, 2, 1}, {"SLE", 2, 2, 1}, {"SFL", 2, 2, 1}, {"STR", 2, 2, 1},
    {"DDX", 1, -1, 1}, {"DDY", 1, -1, 1}, {"RFL", 2, -1, 1}, {"X2D", 3, -1, 1}, {"TXD", 3, -1, 1},
    {"PK2H", 1, -1, 1}, {"PK2US", 1, -1, 1}, {"PK4B", 1, -1, 1}, {"PK4UB", 1, -1, 1}, {"UP2H", 1, -1, 1},
    {"UP2US", 1, -1, 1}, {"UP4B", 1, -1, 1}, {"UP4UB", 1, -1, 1}, {"DIV", 2, -1, 2}, {"NRM", 1, -1, 2},
    {"DP2", 2, -1, 2}, {"DP2A", 3, -1, 2}, {"TXL", 1, 3, 2}, {"SSG", 1, 2, -1}, {"RCC", 1, 2, -1},
    {"ARA", 1, 2, -1}, {"ARR", 1, 2, -1},
};

static const struct opcode *lookup_opcode(const struct parser *p, const char *base)
{
    for (size_t i = 0; i < sizeof opcodes / sizeof *opcodes; ++i) {
        if (strcmp(opcodes[i].name, base)) continue;
        int level = p->vertex ? opcodes[i].vertex : opcodes[i].fragment;
        return level >= 0 && p->nv >= level ? &opcodes[i] : NULL;
    }
    return NULL;
}

/* saturate: 0 none, 1 [0, 1] (_SAT), 2 [-1, 1] (_SSAT, NV_fragment_program2).
   NV names are OP[R|H|X][C][_SAT]: the precision (fragment programs) is
   ignored; C updates the condition code, C0 or C1 (NV_vertex_program3)
   picks the register. */
static const struct opcode *find_opcode(const struct parser *p, const char *word, int *saturate, bool *update_cc,
                                        int *cc_reg)
{
    char base[16];
    snprintf(base, sizeof base, "%s", word);
    *saturate = 0;
    *update_cc = false;
    *cc_reg = 0;
    size_t n = strlen(base);
    if (n > 4 && !strcmp(base + n - 4, "_SAT")) {
        base[n -= 4] = 0;
        *saturate = 1;
    } else if (n > 5 && !strcmp(base + n - 5, "_SSAT")) {
        base[n -= 5] = 0;
        *saturate = 2;
    }
    const struct opcode *op = lookup_opcode(p, base);
    if (!op && p->nv) {
        if (p->vertex && p->nv >= 3 && n > 4 && base[n - 2] == 'C' && (base[n - 1] == '0' || base[n - 1] == '1')) {
            *cc_reg = base[n - 1] - '0';
            base[n -= 2] = 0;
            *update_cc = true;
        } else if (n > 3 && base[n - 1] == 'C') {
            base[--n] = 0;
            *update_cc = true;
        }
        if (*update_cc) op = lookup_opcode(p, base);
        if (!op && !p->vertex && n > 3 && strchr("RHX", base[n - 1])) {
            base[--n] = 0;
            op = lookup_opcode(p, base);
        }
    }
    /* Vertex programs saturate from NV_vertex_program3 on, and never
       signed. */
    if (op && *saturate == 2 && (p->vertex || p->nv < 2)) op = NULL;
    if (op && *saturate == 1 && p->vertex && p->nv < 3) op = NULL;
    return op;
}

/* SWZ's extended swizzle: four of 0, 1, x, y, z, w (or r g b a), each
   optionally negated. */
static void extended_swizzle(struct parser *p, const char *source, char *out, size_t size)
{
    char comps[4][96];
    for (int i = 0; i < 4; ++i) {
        if (i) expect(p, ",");
        bool negate = accept(p, "-");
        if (!negate) accept(p, "+");
        if (p->token.kind == TOK_NUMBER) {
            snprintf(comps[i], sizeof comps[i], "%s%s", negate ? "-" : "", p->token.number != 0 ? "1.0" : "0.0");
            next(p);
        } else {
            char word[8];
            expect_ident(p, word, sizeof word);
            int c = swizzle_component(word[0]);
            if (c < 0 || word[1]) fail(p, "bad extended swizzle '%s'", word);
            snprintf(comps[i], sizeof comps[i], "%s%s.%c", negate ? "-" : "", source, "xyzw"[c < 0 ? 0 : c]);
        }
    }
    snprintf(out, size, "vec4(%s, %s, %s, %s)", comps[0], comps[1], comps[2], comps[3]);
}

/* An address register operand (ARA, PUSHA): the register, no swizzle. */
static const char *address_operand(struct parser *p)
{
    char ident[64];
    expect_ident(p, ident, sizeof ident);
    if (p->failed) return "";
    struct name *a = find_name(p, ident);
    if (!a || a->kind != NAME_ADDRESS) {
        fail(p, "invalid address register");
        undefined_hint(p, ident);
        return "";
    }
    return a->glsl;
}

/* Writes the instruction result glm_<prefix><id> (vec4, or ivec4 for
   address registers) to `variable` under the write mask and the condition
   test, and copies the written components to condition code register
   cc_reg when the opcode has the C suffix. */
static void store(struct parser *p, const char *variable, const char *mask, const char *prefix, int id,
                  const char *test, bool update_cc, int cc_reg)
{
    static const char *const vectors[] = {"", "float", "vec2", "vec3", "vec4"};
    if (update_cc) p->cc_used |= (uint8_t)(1u << cc_reg);
    bool integer = prefix[4] == 'a';
    if (test[0]) {
        emit(&p->body, "  bvec4 glm_w%d = %s;\n", id, test);
        for (const char *c = mask; *c; ++c) {
            emit(&p->body, "  if (glm_w%d.%c) %s.%c = %s%d.%c;\n", id, *c, variable, *c, prefix, id, *c);
            if (update_cc)
                emit(&p->body, "  if (glm_w%d.%c) glm_cc%d.%c = float(%s%d.%c);\n", id, *c, cc_reg, *c, prefix, id, *c);
        }
        return;
    }
    if (!strcmp(mask, "xyzw")) emit(&p->body, "  %s = %s%d;\n", variable, prefix, id);
    else emit(&p->body, "  %s.%s = %s%d.%s;\n", variable, mask, prefix, id, mask);
    if (update_cc)
        emit(&p->body, "  glm_cc%d.%s = %s(%s%d.%s);\n", cc_reg, mask, integer ? vectors[strlen(mask)] : "", prefix, id,
             mask);
}

static bool is_texture_op(const char *n)
{
    return !strcmp(n, "TEX") || !strcmp(n, "TXP") || !strcmp(n, "TXB") || !strcmp(n, "TXL") || !strcmp(n, "TXD");
}

static void instruction(struct parser *p, const struct opcode *op, int saturate, bool update_cc, int cc_reg)
{
    char variable[64], mask[8];
    int slot;
    char s[3][400] = {"", "", ""};
    const char *n = op->name;
    ++p->instructions;
    if (is_texture_op(n)) ++p->tex_instructions;
    else ++p->alu_instructions;
    if (!strcmp(n, "KIL")) {
        char test[160];
        if (p->nv && condition_test(p, test, sizeof test)) {
            emit(&p->body, "  if (any(%s)) discard;\n", test);
            return;
        }
        source_operand(p, s[0], sizeof s[0]);
        emit(&p->body, "  if (any(lessThan(%s, vec4(0.0)))) discard;\n", s[0]);
        return;
    }
    destination(p, variable, sizeof variable, mask, &slot);
    bool address = !strcmp(n, "ARL") || !strcmp(n, "ARR") || !strcmp(n, "ARA");
    if (!p->failed && address != p->dest_address) {
        fail(p, "invalid address register");
        return;
    }
    /* NV conditional write: "dst (GT.x), ..." writes components passing. */
    char write_test[160] = "";
    if (p->nv) condition_mask(p, write_test, sizeof write_test);
    if (!strcmp(n, "ARA")) {
        expect(p, ",");
        snprintf(s[0], sizeof s[0], "%s", address_operand(p));
    } else {
        for (int i = 0; i < op->sources; ++i) {
            expect(p, ",");
            source_operand(p, s[i], sizeof s[i]);
        }
    }
    if (p->failed) return;
    char value[1400];
    if (address) {
        /* ARL floors, ARR rounds to even, ARA adds the halves of a
           register. ARB_vertex_program's ARL loads A0.x only. */
        if (!strcmp(n, "ARL")) snprintf(value, sizeof value, "ivec4(floor(%s))", s[0]);
        else if (!strcmp(n, "ARR")) snprintf(value, sizeof value, "ivec4(roundEven(%s))", s[0]);
        else snprintf(value, sizeof value, "(%s.xyxy + %s.zwzw)", s[0], s[0]);
        if (!p->nv) strcpy(mask, "x");
        int id = p->temp_counter++;
        emit(&p->body, "  ivec4 glm_a%d = %s;\n", id, value);
        store(p, variable, mask, "glm_a", id, write_test, update_cc, cc_reg);
        return;
    }
    if (!strcmp(n, "SWZ")) {
        /* The source was parsed without a swizzle; the rest is the mask. */
        expect(p, ",");
        extended_swizzle(p, s[0], value, sizeof value);
    } else if (is_texture_op(n)) {
        int unit;
        char target[16];
        expect(p, ",");
        texture_operand(p, &unit, target, sizeof target);
        if (p->failed) return;
        texture_sample(p, n, s[0], s[1], s[2], unit, target_code(p, target), value, sizeof value);
    } else if (!strcmp(n, "ABS")) snprintf(value, sizeof value, "abs(%s)", s[0]);
    else if (!strcmp(n, "SEQ")) snprintf(value, sizeof value, "vec4(equal(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "SNE")) snprintf(value, sizeof value, "vec4(notEqual(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "SGT")) snprintf(value, sizeof value, "vec4(greaterThan(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "SLE")) snprintf(value, sizeof value, "vec4(lessThanEqual(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "SFL")) snprintf(value, sizeof value, "vec4(0.0)");
    else if (!strcmp(n, "STR")) snprintf(value, sizeof value, "vec4(1.0)");
    else if (!strcmp(n, "SSG")) snprintf(value, sizeof value, "sign(%s)", s[0]);
    else if (!strcmp(n, "DIV")) snprintf(value, sizeof value, "(%s / %s.x)", s[0], s[1]);
    else if (!strcmp(n, "NRM")) snprintf(value, sizeof value, "vec4(normalize(%s.xyz), %s.w)", s[0], s[0]);
    else if (!strcmp(n, "DDX")) snprintf(value, sizeof value, "dFdx(%s)", s[0]);
    else if (!strcmp(n, "DDY")) snprintf(value, sizeof value, "dFdy(%s)", s[0]);
    else if (!strcmp(n, "RFL"))
        snprintf(value, sizeof value, "vec4(2.0 * dot(%s.xyz, %s.xyz) / dot(%s.xyz, %s.xyz) * %s.xyz - %s.xyz, 0.0)",
                 s[0], s[1], s[0], s[0], s[0], s[1]);
    else if (!strcmp(n, "DP2")) snprintf(value, sizeof value, "vec4(dot(%s.xy, %s.xy))", s[0], s[1]);
    else if (!strcmp(n, "DP2A")) snprintf(value, sizeof value, "vec4(dot(%s.xy, %s.xy) + %s.x)", s[0], s[1], s[2]);
    else if (!strcmp(n, "X2D"))
        snprintf(value, sizeof value, "(%s.xy + %s.x * %s.xz + %s.y * %s.yw).xyxy", s[0], s[1], s[2], s[1], s[2]);
    else if (!strncmp(n, "PK", 2) || !strncmp(n, "UP", 2)) {
        /* Packed values are float bit patterns; unpacking reads one
           component. */
        p->nv_helpers |= HELPER_PACK;
        char lower[8];
        for (int i = 0; i < 6; ++i) lower[i] = (char)tolower((unsigned char)n[i]);
        snprintf(value, sizeof value, "glm_%s(%s%s)", lower, s[0], n[0] == 'U' ? ".x" : "");
    } else if (!strcmp(n, "RCC")) {
        p->nv_helpers |= HELPER_RCC;
        snprintf(value, sizeof value, "vec4(glm_rcc(%s.x))", s[0]);
    }
    else if (!strcmp(n, "ADD")) snprintf(value, sizeof value, "(%s + %s)", s[0], s[1]);
    else if (!strcmp(n, "SUB")) snprintf(value, sizeof value, "(%s - %s)", s[0], s[1]);
    else if (!strcmp(n, "MUL")) snprintf(value, sizeof value, "(%s * %s)", s[0], s[1]);
    else if (!strcmp(n, "MAD")) snprintf(value, sizeof value, "(%s * %s + %s)", s[0], s[1], s[2]);
    else if (!strcmp(n, "MOV")) snprintf(value, sizeof value, "%s", s[0]);
    else if (!strcmp(n, "MAX")) snprintf(value, sizeof value, "max(%s, %s)", s[0], s[1]);
    else if (!strcmp(n, "MIN")) snprintf(value, sizeof value, "min(%s, %s)", s[0], s[1]);
    else if (!strcmp(n, "DP3")) snprintf(value, sizeof value, "vec4(dot(%s.xyz, %s.xyz))", s[0], s[1]);
    else if (!strcmp(n, "DP4")) snprintf(value, sizeof value, "vec4(dot(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "DPH")) snprintf(value, sizeof value, "vec4(dot(%s.xyz, %s.xyz) + %s.w)", s[0], s[1], s[1]);
    else if (!strcmp(n, "DST")) snprintf(value, sizeof value, "vec4(1.0, %s.y * %s.y, %s.z, %s.w)", s[0], s[1], s[0], s[1]);
    else if (!strcmp(n, "EX2")) snprintf(value, sizeof value, "vec4(exp2(%s.x))", s[0]);
    else if (!strcmp(n, "LG2")) snprintf(value, sizeof value, "vec4(log2(%s.x))", s[0]);
    else if (!strcmp(n, "EXP")) snprintf(value, sizeof value, "glm_arb_exp(%s.x)", s[0]);
    else if (!strcmp(n, "LOG")) snprintf(value, sizeof value, "glm_arb_log(%s.x)", s[0]);
    else if (!strcmp(n, "FLR")) snprintf(value, sizeof value, "floor(%s)", s[0]);
    else if (!strcmp(n, "FRC")) snprintf(value, sizeof value, "fract(%s)", s[0]);
    else if (!strcmp(n, "LIT")) snprintf(value, sizeof value, "glm_arb_lit(%s)", s[0]);
    else if (!strcmp(n, "POW")) snprintf(value, sizeof value, "vec4(pow(%s.x, %s.x))", s[0], s[1]);
    else if (!strcmp(n, "RCP")) snprintf(value, sizeof value, "vec4(1.0 / %s.x)", s[0]);
    else if (!strcmp(n, "RSQ")) snprintf(value, sizeof value, "vec4(inversesqrt(abs(%s.x)))", s[0]);
    else if (!strcmp(n, "SGE")) snprintf(value, sizeof value, "vec4(greaterThanEqual(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "SLT")) snprintf(value, sizeof value, "vec4(lessThan(%s, %s))", s[0], s[1]);
    else if (!strcmp(n, "XPD")) snprintf(value, sizeof value, "vec4(cross(%s.xyz, %s.xyz), 1.0)", s[0], s[1]);
    else if (!strcmp(n, "CMP")) snprintf(value, sizeof value, "mix(%s, %s, lessThan(%s, vec4(0.0)))", s[2], s[1], s[0]);
    else if (!strcmp(n, "LRP")) snprintf(value, sizeof value, "mix(%s, %s, %s)", s[2], s[1], s[0]);
    else if (!strcmp(n, "COS")) snprintf(value, sizeof value, "vec4(cos(%s.x))", s[0]);
    else if (!strcmp(n, "SIN")) snprintf(value, sizeof value, "vec4(sin(%s.x))", s[0]);
    /* z and w are undefined; Apple's are 0. */
    else if (!strcmp(n, "SCS")) snprintf(value, sizeof value, "vec4(cos(%s.x), sin(%s.x), 0.0, 0.0)", s[0], s[0]);
    else {
        fail(p, "unsupported instruction %s", n);
        return;
    }
    if (p->failed) return;
    /* Evaluate fully before writing: a source may alias the destination. */
    int id = p->temp_counter++;
    emit(&p->body, "  vec4 glm_v%d = %s%s%s;\n", id, saturate ? "clamp(" : "", value,
         saturate == 2 ? ", -1.0, 1.0)" : saturate ? ", 0.0, 1.0)" : "");
    store(p, variable, mask, "glm_v", id, write_test, update_cc, cc_reg);
    /* DP4 result.clip[n].x, V, state.clip[n].plane: clip against the
       enabled user planes with gl_ClipVertex = V, as fixed function does. */
    if (slot >= RESULT_CLIP0 && slot < RESULT_CLIP0 + 8) {
        char plane[40];
        snprintf(plane, sizeof plane, "((gl_ClipPlane[%d]))", slot - RESULT_CLIP0);
        bool matched = !strcmp(n, "DP4") && !strcmp(s[1], plane) && !write_test[0];
        if (matched) emit(&p->body, "  glm_clip_vertex = %s;\n", s[0]);
        if (!p->clips_written) p->clip_vertex_ok = true;
        p->clip_vertex_ok &= matched;
        p->clips_written |= (uint8_t)(1u << (slot - RESULT_CLIP0));
    }
}

/* ---- blocks and branches ------------------------------------------------- */

enum { BLOCK_IF, BLOCK_REP, BLOCK_LOOP };

/* A new state machine segment id (0 is the program's first segment). */
static int new_segment(struct parser *p) { return ++p->segments; }

/* Ends the current segment, continuing at `pc` (a GLSL int expression;
   -1 ends the program), and opens segment `open`. */
static void next_segment(struct parser *p, const char *pc, int open)
{
    emit(&p->body, "  glm_pc = %s;\n  }\n  if (glm_pc == %d) {\n", pc, open);
}

static struct label *find_label(struct parser *p, const char *name)
{
    for (int i = 0; i < p->label_count; ++i)
        if (!strcmp(p->labels[i].name, name)) return &p->labels[i];
    if (p->label_count % 16 == 0) p->labels = realloc(p->labels, (size_t)(p->label_count + 16) * sizeof *p->labels);
    struct label *l = &p->labels[p->label_count++];
    snprintf(l->name, sizeof l->name, "%s", name);
    l->id = new_segment(p);
    l->defined = false;
    return l;
}

/* NV flow control. Fragment programs (NV_fragment_program2): IF/ELSE/ENDIF
   on a condition code test, REP n/ENDREP, LOOP {n, start, step}/ENDLOOP
   (the loop index A0.x is not readable), BRK [(test)], CAL label [(test)]
   and RET [(test)]. Vertex programs (NV_vertex_program2): BRA label
   [(test)], CAL, RET, and PUSHA/POPA (NV_vertex_program3). Returns false
   for other statements. */
static bool control_statement(struct parser *p, const char *word)
{
    bool blocks = !p->vertex && p->nv >= 2, calls = p->nv >= 2, branches = p->vertex && p->nv >= 2;
    bool stack = p->vertex && p->nv >= 3;
    struct block *top = p->block_depth ? &p->blocks[p->block_depth - 1] : NULL;
    char test[160], pc[240];
    if (blocks && !strcmp(word, "IF")) {
        if (!condition_test(p, test, sizeof test)) {
            fail(p, "invalid condition mask");
            return true;
        }
        if (p->block_depth == (int)(sizeof p->blocks / sizeof *p->blocks)) {
            fail(p, "blocks nested too deeply");
            return true;
        }
        struct block *b = &p->blocks[p->block_depth++];
        *b = (struct block){.kind = BLOCK_IF};
        if (p->flow) {
            int then = new_segment(p);
            b->open = new_segment(p); /* the ELSE part, or after ENDIF */
            snprintf(pc, sizeof pc, "any(%s) ? %d : %d", test, then, b->open);
            next_segment(p, pc, then);
        } else {
            emit(&p->body, "  if (any(%s)) {\n", test);
        }
    } else if (blocks && !strcmp(word, "ELSE")) {
        if (!top || top->kind != BLOCK_IF || top->has_else) {
            fail(p, "ELSE outside an IF block");
            return true;
        }
        top->has_else = true;
        if (p->flow) {
            top->close = new_segment(p);
            snprintf(pc, sizeof pc, "%d", top->close);
            next_segment(p, pc, top->open);
        } else {
            emit(&p->body, "  } else {\n");
        }
    } else if (blocks && (!strcmp(word, "ENDIF") || !strcmp(word, "ENDREP") || !strcmp(word, "ENDLOOP"))) {
        int kind = word[3] == 'I' ? BLOCK_IF : word[3] == 'R' ? BLOCK_REP : BLOCK_LOOP;
        if (!top || top->kind != kind) {
            fail(p, "%s outside its block", word);
            return true;
        }
        if (p->flow && kind == BLOCK_IF) {
            int after = top->has_else ? top->close : top->open;
            snprintf(pc, sizeof pc, "%d", after);
            next_segment(p, pc, after);
        } else if (p->flow) {
            emit(&p->body, "  glm_lc%d -= 1;\n", top->counter);
            snprintf(pc, sizeof pc, "glm_lc%d > 0 ? %d : %d", top->counter, top->open, top->close);
            next_segment(p, pc, top->close);
        } else {
            emit(&p->body, "  }\n");
        }
        --p->block_depth;
    } else if (blocks && (!strcmp(word, "REP") || !strcmp(word, "LOOP"))) {
        /* The count is x ({count, start, step} for LOOP), floored; blocks
           with no positive count are skipped. */
        if (is(p, ";")) {
            fail(p, "undefined source identifier");
            return true;
        }
        char count[400];
        source_operand(p, count, sizeof count);
        if (p->failed) return true;
        if (p->block_depth == (int)(sizeof p->blocks / sizeof *p->blocks)) {
            fail(p, "blocks nested too deeply");
            return true;
        }
        struct block *b = &p->blocks[p->block_depth++];
        *b = (struct block){.kind = word[0] == 'R' ? BLOCK_REP : BLOCK_LOOP};
        if (p->flow) {
            b->counter = p->loop_counters++;
            b->open = new_segment(p);
            b->close = new_segment(p);
            emit(&p->body, "  glm_lc%d = int(clamp(floor(%s.x), 0.0, 65536.0));\n", b->counter, count);
            snprintf(pc, sizeof pc, "glm_lc%d > 0 ? %d : %d", b->counter, b->open, b->close);
            next_segment(p, pc, b->open);
        } else {
            int id = p->temp_counter++;
            emit(&p->body, "  for (int glm_rep%d = 0; glm_rep%d < int(%s.x); ++glm_rep%d) {\n", id, id, count, id);
        }
    } else if (blocks && !strcmp(word, "BRK")) {
        condition_mask(p, test, sizeof test);
        if (p->failed) return true;
        struct block *loop = NULL;
        for (int i = p->block_depth - 1; i >= 0 && !loop; --i)
            if (p->blocks[i].kind != BLOCK_IF) loop = &p->blocks[i];
        if (!loop) {
            fail(p, "BRK outside a loop");
            return true;
        }
        if (p->flow) {
            int next = new_segment(p);
            if (test[0]) snprintf(pc, sizeof pc, "any(%s) ? %d : %d", test, loop->close, next);
            else snprintf(pc, sizeof pc, "%d", loop->close);
            next_segment(p, pc, next);
        } else if (test[0]) {
            emit(&p->body, "  if (any(%s)) break;\n", test);
        } else {
            emit(&p->body, "  break;\n");
        }
    } else if ((calls && !strcmp(word, "CAL")) || (branches && !strcmp(word, "BRA"))) {
        char name[64];
        expect_ident(p, name, sizeof name);
        if (p->failed) return true;
        int target = find_label(p, name)->id;
        condition_mask(p, test, sizeof test);
        if (p->failed) return true;
        int next = new_segment(p);
        const char *taken = test[0] ? test : "bvec4(true)";
        if (word[0] == 'B') {
            snprintf(pc, sizeof pc, "any(%s) ? %d : %d", taken, target, next);
            next_segment(p, pc, next);
        } else {
            /* Calling with a full stack ends the program. */
            emit(&p->body,
                 "  if (any(%s)) {\n  if (glm_sp < 4) { glm_ret[glm_sp] = %d; glm_sp += 1; glm_pc = %d; } else glm_pc = -1;\n"
                 "  } else glm_pc = %d;\n  }\n  if (glm_pc == %d) {\n",
                 taken, next, target, next, next);
        }
    } else if (calls && !strcmp(word, "RET")) {
        condition_mask(p, test, sizeof test);
        if (p->failed) return true;
        int next = new_segment(p);
        /* RET with an empty stack ends the program, as does one popping a
           PUSHA entry (glm_ret -1). */
        emit(&p->body,
             "  if (any(%s)) {\n  if (glm_sp > 0) { glm_sp -= 1; glm_pc = glm_ret[glm_sp]; } else glm_pc = -1;\n"
             "  } else glm_pc = %d;\n  }\n  if (glm_pc == %d) {\n",
             test[0] ? test : "bvec4(true)", next, next);
    } else if (stack && !strcmp(word, "PUSHA")) {
        const char *a = address_operand(p);
        if (p->failed) return true;
        int next = new_segment(p);
        emit(&p->body,
             "  if (glm_sp < 4) { glm_ret[glm_sp] = -1; glm_astack[glm_sp] = %s; glm_sp += 1; glm_pc = %d; } else glm_pc = -1;\n"
             "  }\n  if (glm_pc == %d) {\n",
             a, next, next);
    } else if (stack && !strcmp(word, "POPA")) {
        /* The whole register: no write mask or condition. */
        const char *variable = address_operand(p);
        if (p->failed) return true;
        int next = new_segment(p);
        emit(&p->body,
             "  if (glm_sp > 0 && glm_ret[glm_sp - 1] == -1) { glm_sp -= 1; %s = glm_astack[glm_sp]; glm_pc = %d; }"
             " else glm_pc = -1;\n  }\n  if (glm_pc == %d) {\n",
             variable, next, next);
    } else {
        return false;
    }
    /* Apple counts flow control as ALU instructions. */
    ++p->instructions;
    ++p->alu_instructions;
    return true;
}

static void declaration_list(struct parser *p, enum name_kind kind)
{
    do {
        char ident[64];
        expect_ident(p, ident, sizeof ident);
        if (p->failed) return;
        struct name *n = add_name(p, ident, kind);
        if (!n) return;
        if (kind == NAME_ADDRESS) ++p->address_declared;
        if (kind == NAME_TEMP) emit(&p->prologue, "  vec4 %s = vec4(0.0);\n", n->glsl);
        else emit(&p->prologue, "  ivec4 %s = ivec4(0);\n", n->glsl);
    } while (accept(p, ","));
}

static void param_statement(struct parser *p)
{
    char ident[64];
    expect_ident(p, ident, sizeof ident);
    if (p->failed) return;
    bool array = false;
    int declared = -1;
    if (accept(p, "[")) {
        array = true;
        if (!is(p, "]")) declared = expect_integer(p);
        expect(p, "]");
    }
    expect(p, "=");
    if (p->failed) return;
    if (!array) {
        struct name *n = add_name(p, ident, NAME_PARAM);
        if (!n) return;
        char items[4][160];
        int count = parameter_item(p, items, 4, true, NULL, NULL);
        if (count != 1) fail(p, "'%s' needs a single vector binding", ident);
        snprintf(n->expression, sizeof n->expression, "%s", items[0]);
        snprintf(n->key, sizeof n->key, "%s", items[0]);
        /* Constant vectors are hoisted into locals. */
        if (!strncmp(items[0], "vec4(", 5)) {
            emit(&p->prologue, "  vec4 %s = %s;\n", n->glsl, items[0]);
            snprintf(n->expression, sizeof n->expression, "%s", n->glsl);
        } else {
            snprintf(n->expression, sizeof n->expression, "(%s)", items[0]);
        }
        return;
    }
    struct name *n = add_name(p, ident, NAME_PARAM_ARRAY);
    if (!n) return;
    expect(p, "{");
    /* Up to GL_MAX_PROGRAM_PARAMETERS_ARB (1024) elements. */
    enum { MAX_ELEMENTS = 1024 };
    char (*items)[160] = calloc(MAX_ELEMENTS, sizeof *items);
    int count = 0;
    const char *alias = NULL;
    int alias_base = 0, groups = 0;
    do {
        const char *a;
        int base;
        int got = parameter_item(p, items + count, MAX_ELEMENTS - count, false, &a, &base);
        if (groups == 0 && a) { alias = a; alias_base = base; }
        else alias = NULL;
        count += got;
        ++groups;
    } while (!p->failed && accept(p, ",") && count < MAX_ELEMENTS);
    expect(p, "}");
    if (groups != 1) alias = NULL;
    if (declared >= 0 && declared != count) fail(p, "'%s' declared with %d elements but has %d", ident, declared, count);
    n->array_size = count;
    n->element_keys = calloc((size_t)(count ? count : 1), sizeof *n->element_keys);
    for (int i = 0; i < count; ++i) snprintf(n->element_keys[i], 160, "%s", items[i]);
    if (alias) {
        n->alias_array = alias;
        n->alias_base = alias_base;
    } else if (!p->failed) {
        emit(&p->prologue, "  vec4 %s[%d] = vec4[%d](", n->glsl, count, count);
        for (int i = 0; i < count; ++i) emit(&p->prologue, "%s%s", i ? ", " : "", items[i]);
        emit(&p->prologue, ");\n");
    }
    free(items);
}

static void statement(struct parser *p)
{
    char word[64];
    if (p->token.kind != TOK_IDENT) {
        fail(p, "unrecognised statement");
        return;
    }
    expect_ident(p, word, sizeof word);
    if (p->failed) return;
    int saturate, cc_reg;
    bool update_cc;
    /* Options come first; branching needs NV_fragment_program2 or
       NV_vertex_program2. */
    if (strcmp(word, "OPTION") && !p->flow_decided) {
        p->flow_decided = true;
        p->flow = p->flow_candidate && p->nv >= 2;
    }
    if (p->flow && is(p, ":") && !find_opcode(p, word, &saturate, &update_cc, &cc_reg)) {
        /* A label (its own namespace; labels inside blocks work too). */
        struct label *l = find_label(p, word);
        if (l->defined) {
            fail(p, "duplicate branch label");
            return;
        }
        l->defined = true;
        next(p);
        char pc[16];
        snprintf(pc, sizeof pc, "%d", l->id);
        next_segment(p, pc, l->id);
        return;
    }
    /* NV_fragment_program_option storage precision (ignored). */
    if (!p->vertex && p->nv && (!strcmp(word, "SHORT") || !strcmp(word, "LONG")) && (is(p, "TEMP") || is(p, "OUTPUT")))
        expect_ident(p, word, sizeof word);
    if (!strcmp(word, "OPTION")) {
        char option[64];
        expect_ident(p, option, sizeof option);
        int level = 0;
        if (!p->vertex && (!strcmp(option, "NV_fragment_program") || !strcmp(option, "NV_fragment_program2")))
            level = option[19] ? 2 : 1;
        else if (p->vertex && (!strcmp(option, "NV_vertex_program2") || !strcmp(option, "NV_vertex_program3")))
            level = option[17] - '0';
        if (level) {
            if (level > p->nv) p->nv = level;
        } else if (!strcmp(option, "ARB_position_invariant") && p->vertex) p->position_invariant = true;
        else if (!strcmp(option, "ARB_fog_linear") && !p->vertex) p->fog_option = 1;
        else if (!strcmp(option, "ARB_fog_exp") && !p->vertex) p->fog_option = 2;
        else if (!strcmp(option, "ARB_fog_exp2") && !p->vertex) p->fog_option = 3;
        else if (!strcmp(option, "ARB_fragment_program_shadow") && !p->vertex) p->shadow_option = true;
        else if (p->vertex || (strncmp(option, "ARB_precision_hint_", 19) && strcmp(option, "ARB_draw_buffers") &&
                               strcmp(option, "ATI_draw_buffers"))) {
            fail(p, "unrecognised option");
            snprintf(p->error_hint, sizeof p->error_hint, "%s", option);
        }
    } else if (!strcmp(word, "TEMP")) {
        declaration_list(p, NAME_TEMP);
    } else if (!strcmp(word, "ADDRESS")) {
        declaration_list(p, NAME_ADDRESS);
    } else if (!strcmp(word, "ATTRIB")) {
        char ident[64];
        expect_ident(p, ident, sizeof ident);
        expect(p, "=");
        struct name *n = add_name(p, ident, NAME_ATTRIB);
        if (!n) return;
        char e[160];
        attribute_binding(p, e);
        snprintf(n->expression, sizeof n->expression, "(%s)", e);
        snprintf(n->key, sizeof n->key, "%s", e);
    } else if (!strcmp(word, "PARAM")) {
        param_statement(p);
    } else if (!strcmp(word, "OUTPUT")) {
        char ident[64];
        expect_ident(p, ident, sizeof ident);
        expect(p, "=");
        struct name *n = add_name(p, ident, NAME_OUTPUT);
        if (!n) return;
        int slot = result_binding(p);
        snprintf(n->expression, sizeof n->expression, "%d", slot);
    } else if (!strcmp(word, "ALIAS")) {
        char ident[64], target[64];
        expect_ident(p, ident, sizeof ident);
        expect(p, "=");
        expect_ident(p, target, sizeof target);
        struct name *t = find_name(p, target);
        if (!t) {
            fail(p, "undefined identifier");
            undefined_hint(p, target);
            return;
        }
        struct name copy = *t;
        struct name *n = add_name(p, ident, t->kind);
        if (!n) return;
        *n = copy; /* the same variable (glsl, slot), under another name */
        snprintf(n->ident, sizeof n->ident, "%s", ident);
        n->alias = true;
    } else if (p->nv && control_statement(p, word)) {
    } else {
        const struct opcode *op = find_opcode(p, word, &saturate, &update_cc, &cc_reg);
        if (!op) {
            fail(p, "unrecognised statement");
            snprintf(p->error_hint, sizeof p->error_hint, "%s", word);
            return;
        }
        instruction(p, op, saturate, update_cc, cc_reg);
    }
    if (!p->failed) expect(p, ";");
}

/* ---- program ------------------------------------------------------------- */

static const char helpers[] =
    "vec4 glm_arb_lit(vec4 a) {\n"
    "  float d = max(a.x, 0.0), s = max(a.y, 0.0), e = clamp(a.w, -128.0, 128.0);\n"
    "  return vec4(1.0, d, a.x > 0.0 ? (s == 0.0 && e == 0.0 ? 1.0 : pow(s, e)) : 0.0, 1.0);\n}\n"
    "vec4 glm_arb_exp(float a) { float f = floor(a); return vec4(exp2(f), a - f, exp2(a), 1.0); }\n"
    "vec4 glm_arb_log(float a) {\n"
    "  float v = abs(a), e = floor(log2(v));\n"
    "  return vec4(e, v / exp2(e), log2(v), 1.0);\n}\n";

/* NV pack/unpack: half conversions round to nearest even and keep
   infinities, NaNs and denormals; the 8- and 16-bit forms round half up. */
static const char pack_helpers[] =
    "uint glm_f2h(float f) {\n"
    "  uint x = floatBitsToUint(f), sign = (x >> 16) & 0x8000u, m = x & 0x7fffffu;\n"
    "  int e = int((x >> 23) & 0xffu) - 112;\n"
    "  if (e == 143) return sign | 0x7c00u | (m != 0u ? 0x200u | (m >> 13) : 0u);\n"
    "  if (e >= 31) return sign | 0x7c00u;\n"
    "  if (e <= 0) {\n"
    "    if (e < -10) return sign;\n"
    "    m |= 0x800000u;\n"
    "    uint shift = uint(14 - e), h = m >> shift, rest = m & ((1u << shift) - 1u), mid = 1u << (shift - 1u);\n"
    "    return sign | (h + uint(rest > mid || (rest == mid && (h & 1u) != 0u)));\n"
    "  }\n"
    "  uint h = (uint(e) << 10) | (m >> 13), rest = m & 0x1fffu;\n"
    "  return sign | (h + uint(rest > 0x1000u || (rest == 0x1000u && (h & 1u) != 0u)));\n}\n"
    "float glm_h2f(uint h) {\n"
    "  uint sign = (h & 0x8000u) << 16, e = (h >> 10) & 0x1fu, m = h & 0x3ffu;\n"
    "  if (e == 0u) return (sign != 0u ? -1.0 : 1.0) * float(m) * 5.9604645e-8;\n"
    "  if (e == 31u) return uintBitsToFloat(sign | 0x7f800000u | (m << 13));\n"
    "  return uintBitsToFloat(sign | ((e + 112u) << 23) | (m << 13));\n}\n"
    "vec4 glm_pk2h(vec4 a) { return vec4(uintBitsToFloat(glm_f2h(a.x) | (glm_f2h(a.y) << 16))); }\n"
    "vec4 glm_pk2us(vec4 a) {\n"
    "  uvec2 u = uvec2(floor(clamp(a.xy, 0.0, 1.0) * 65535.0 + 0.5));\n"
    "  return vec4(uintBitsToFloat(u.x | (u.y << 16)));\n}\n"
    "vec4 glm_pk4b(vec4 a) {\n"
    "  uvec4 u = uvec4(floor(clamp(a, -128.0 / 127.0, 1.0) * 127.0 + 128.5));\n"
    "  return vec4(uintBitsToFloat(u.x | (u.y << 8) | (u.z << 16) | (u.w << 24)));\n}\n"
    "vec4 glm_pk4ub(vec4 a) {\n"
    "  uvec4 u = uvec4(floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5));\n"
    "  return vec4(uintBitsToFloat(u.x | (u.y << 8) | (u.z << 16) | (u.w << 24)));\n}\n"
    "vec4 glm_up2h(float a) { uint u = floatBitsToUint(a); return vec2(glm_h2f(u & 0xffffu), glm_h2f(u >> 16)).xyxy; }\n"
    "vec4 glm_up2us(float a) { uint u = floatBitsToUint(a); return (vec2(uvec2(u & 0xffffu, u >> 16)) / 65535.0).xyxy; }\n"
    "vec4 glm_up4b(float a) {\n"
    "  uvec4 u = (uvec4(floatBitsToUint(a)) >> uvec4(0u, 8u, 16u, 24u)) & 0xffu;\n"
    "  return (vec4(u) - 128.0) / 127.0;\n}\n"
    "vec4 glm_up4ub(float a) {\n"
    "  uvec4 u = (uvec4(floatBitsToUint(a)) >> uvec4(0u, 8u, 16u, 24u)) & 0xffu;\n"
    "  return vec4(u) / 255.0;\n}\n";

/* NV_vertex_program2_option RCC: the reciprocal clamped to
   +-[2^-64, 2^64]. */
static const char rcc_helper[] =
    "float glm_rcc(float a) {\n  float r = 1.0 / a;\n"
    "  return r > 0.0 ? clamp(r, 5.42101086e-20, 1.84467441e19) : clamp(r, -1.84467441e19, -5.42101086e-20);\n}\n";

/* Whether the program has labels or CAL, RET, BRA, PUSHA or POPA
   statements (scanned before parsing: they make the body a state
   machine). */
static bool scan_flow(const char *source, size_t length, size_t start)
{
    static const char *const words[] = {"CAL", "RET", "BRA", "PUSHA", "POPA"};
    struct parser *q = calloc(1, sizeof *q);
    q->source = source;
    q->length = length;
    q->at = start;
    next(q);
    bool found = false, statement_start = true;
    while (q->token.kind != TOK_END && !found) {
        bool word = q->token.kind == TOK_IDENT && statement_start;
        for (size_t i = 0; word && i < sizeof words / sizeof *words; ++i) found |= !strcmp(q->token.text, words[i]);
        statement_start = is(q, ";");
        next(q);
        found |= word && is(q, ":");
    }
    free(q);
    return found;
}

/* The GLSL expression parameter_binding gives `text` (state.* or program.*). */
static void binding_key(bool vertex, const char *text, char *out)
{
    struct parser *q = calloc(1, sizeof *q);
    q->source = text;
    q->length = strlen(text);
    q->vertex = vertex;
    q->env_max = q->local_max = -1;
    next(q);
    char e[4][160];
    if (parameter_binding(q, e, 1, NULL, NULL) >= 1 && !q->failed) snprintf(out, 160, "%s", e[0]);
    else snprintf(out, 160, "%s", text);
    free(q->declarations.data);
    free(q->body.data);
    free(q->prologue.data);
    free(q);
}

/* The counters glGetProgramivARB reports. Options add what Apple's
   implementation counts for them: position invariance 8 instructions and
   8 parameters (the MVP and modelview rows), fog 4 (linear) or 5
   instructions with 2 temporaries, its colour and parameters (linear: one
   more constant) and the fog coordinate; reading fragment.fogcoord costs
   one instruction and temporary. */
static void count_program(struct parser *p, struct glm_arb_translation *out)
{
    int extra_instructions = 0, extra_temporaries = 0;
    char key[160];
    if (p->vertex && p->position_invariant) {
        extra_instructions += 8;
        const char *const matrices[] = {"mvp", "modelview"};
        for (int m = 0; m < 2; ++m)
            for (int r = 0; r < 4; ++r) {
                char text[64];
                snprintf(text, sizeof text, "state.matrix.%s.row[%d]", matrices[m], r);
                binding_key(true, text, key);
                count_key(&p->param_keys, &p->param_count, key);
            }
        count_key(&p->attrib_keys, &p->attrib_count, "gl_Vertex");
    }
    if (!p->vertex && p->fog_option) {
        extra_instructions += p->fog_option == 1 ? 4 : 5;
        extra_temporaries += 2;
        binding_key(false, "state.fog.color", key);
        count_key(&p->param_keys, &p->param_count, key);
        binding_key(false, "state.fog.params", key);
        count_key(&p->param_keys, &p->param_count, key);
        if (p->fog_option == 1) count_key(&p->param_keys, &p->param_count, "glm fog linear");
        count_key(&p->attrib_keys, &p->attrib_count, "vec4(gl_FogFragCoord, 0.0, 0.0, 1.0)");
    } else if (!p->vertex && p->fogcoord_read) {
        extra_instructions += 1;
        extra_temporaries += 1;
    }
    int temporaries = 0;
    for (int i = 0; i < p->name_count; ++i) temporaries += p->temp_used[i];
    out->instructions = p->instructions + extra_instructions;
    out->alu_instructions = p->vertex ? 0 : p->alu_instructions + extra_instructions;
    out->tex_instructions = p->vertex ? 0 : p->tex_instructions;
    out->tex_indirections = p->vertex ? 0 : 1;
    out->temporaries = temporaries + extra_temporaries;
    out->parameters = p->param_count;
    out->attribs = p->attrib_count ? p->attrib_count : 1; /* Apple counts at least one */
    out->address_registers = p->address_declared;
}

static void free_counting(struct parser *p)
{
    for (int i = 0; i < p->name_count; ++i) {
        if (!p->names[i].element_keys) continue;
        bool shared = false; /* ALIAS copies the pointer */
        for (int j = 0; j < i; ++j) shared |= p->names[j].element_keys == p->names[i].element_keys;
        if (!shared) free(p->names[i].element_keys);
    }
    for (int i = 0; i < p->name_count; ++i) p->names[i].element_keys = NULL;
    free(p->param_keys);
    free(p->attrib_keys);
    p->param_keys = p->attrib_keys = NULL;
}

bool glm_arb_translate(const char *source, size_t length, bool vertex, struct glm_arb_translation *out)
{
    memset(out, 0, sizeof *out);
    struct parser *p = calloc(1, sizeof *p);
    p->source = source;
    p->length = length;
    p->vertex = vertex;
    p->env_max = p->local_max = -1;
    const char *header = vertex ? "!!ARBvp1.0" : "!!ARBfp1.0";
    size_t header_length = strlen(header);
    if (length < header_length || strncmp(source, header, header_length)) {
        p->failed = true;
        snprintf(p->error, sizeof p->error, "missing or invalid version");
        p->error_position = 0;
        size_t n = 0;
        while (n < length && n < sizeof p->error_hint - 1 && !isspace((unsigned char)source[n])) ++n;
        snprintf(p->error_hint, sizeof p->error_hint, "%.*s", (int)n, source);
    } else {
        p->flow_candidate = scan_flow(source, length, header_length);
        p->at = header_length;
        next(p);
        while (!p->failed && !is(p, "END")) {
            if (p->token.kind == TOK_END) {
                fail(p, "unexpected end of program");
                /* Quoted at the last token, as Apple's message is. */
                p->error_position = p->previous.position;
                snprintf(p->error_hint, sizeof p->error_hint, "%s", p->previous.text);
                break;
            }
            statement(p);
        }
        /* Blocks must be closed and branch labels defined (quoted at END,
           as Apple's message is). */
        for (int i = 0; !p->failed && i < p->label_count; ++i)
            if (!p->labels[i].defined) fail(p, "undefined branch label");
        if (!p->failed && p->block_depth) fail(p, "unterminated block");
        /* Only whitespace and comments may follow END, as Apple's parser
           requires. */
        if (!p->failed) {
            next(p);
            if (p->token.kind != TOK_END) fail(p, "text after END");
        }
    }
    uint32_t conflict = vertex ? p->conventional_bound & p->generic_bound : 0;
    if (!p->failed && conflict)
        fail(p, "vertex.attrib[%d] and the conventional attribute it aliases are both bound", __builtin_ctz(conflict));
    if (p->failed) {
        free_counting(p);
        /* Apple's form: "Error on line N: message (hint: 'token')". */
        int line = 1;
        for (size_t i = 0; i < p->error_position && i < length; ++i) line += source[i] == '\n';
        snprintf(out->error, sizeof out->error, "Error on line %d: %s (hint: '%s')", line, p->error, p->error_hint);
        out->error_position = (int)p->error_position;
        free(p->labels);
        free(p->declarations.data);
        free(p->body.data);
        free(p->prologue.data);
        free(p);
        return false;
    }

    struct text t = {0};
    emit(&t, "#version 120\n");
    int env_count = p->env_dynamic ? (vertex ? GLM_ARB_ENV_PARAMETERS : GLM_ARB_FRAGMENT_ENV_PARAMETERS) : p->env_max + 1;
    int local_count = p->local_dynamic ? GLM_ARB_LOCAL_PARAMETERS : p->local_max + 1;
    if (env_count < 1) env_count = 1;
    if (local_count < 1) local_count = 1;
    emit(&t, "layout(std140, set = 1, binding = 13) uniform GLMARB { vec4 glm_arb_env[%d]; vec4 glm_arb_local[%d];%s };\n",
         env_count, local_count, p->program_matrices ? " vec4 glm_arb_matrices[64];" : "");
    out->program_matrices = p->program_matrices;
    out->env_count = env_count;
    out->local_count = local_count;
    for (int i = 0; i < 16; ++i)
        if (p->attribs_used & (1u << i)) emit(&t, "attribute vec4 arb_attrib%d;\n", i);
    out->attribs_used = p->attribs_used;
    count_program(p, out);
    free_counting(p);
    emit(&t, "%s", helpers);
    if (p->nv_helpers & HELPER_PACK) emit(&t, "%s", pack_helpers);
    if (p->nv_helpers & HELPER_RCC) emit(&t, "%s", rcc_helper);
    if (p->declarations.data) emit(&t, "%s", p->declarations.data);
    for (int slot = 0; slot < RESULT_COUNT; ++slot) {
        bool written = (p->results_written & (1u << slot)) != 0;
        /* Components a fragment program leaves unwritten are 0, as on
           Apple's implementation. */
        if (written)
            emit(&t, "vec4 %s = vec4(0.0, 0.0, 0.0, %s);\n", result_variable(slot), vertex ? "1.0" : "0.0");
    }
    if (p->clips_written) emit(&t, "vec4 glm_clip_vertex = vec4(0.0);\n");
    emit(&t, "void main() {\n");
    for (int r = 0; r < 2; ++r)
        if (p->cc_used & (1u << r)) emit(&t, "  vec4 glm_cc%d = vec4(0.0);\n", r);
    if (p->prologue.data) emit(&t, "%s", p->prologue.data);
    if (p->flow) {
        /* Execution starts at the label "main" if there is one; the loop
           ends at glm_pc -1, or after MAX_PROGRAM_EXEC_INSTRUCTIONS_NV
           passes. */
        int start = 0;
        for (int i = 0; i < p->label_count; ++i)
            if (!strcmp(p->labels[i].name, "main")) start = p->labels[i].id;
        emit(&t, "  int glm_pc = %d, glm_sp = 0;\n  int glm_ret[4] = int[4](0, 0, 0, 0);\n", start);
        emit(&t, "  ivec4 glm_astack[4] = ivec4[4](ivec4(0), ivec4(0), ivec4(0), ivec4(0));\n");
        for (int i = 0; i < p->loop_counters; ++i) emit(&t, "  int glm_lc%d = 0;\n", i);
        emit(&t, "  for (int glm_step = 0; glm_step < 65536 && glm_pc >= 0; ++glm_step) {\n  if (glm_pc == 0) {\n");
        if (p->body.data) emit(&t, "%s", p->body.data);
        emit(&t, "  glm_pc = -1;\n  }\n  }\n");
    } else if (p->body.data) {
        emit(&t, "%s", p->body.data);
    }
    uint32_t w = p->results_written;
    if (vertex) {
        /* Position-invariant programs get user clip planes as fixed
           function does (ARB_vertex_program issue: "user-defined clipping
           ... for programs that use the position invariant option"). */
        if (p->position_invariant) emit(&t, "  gl_Position = ftransform();\n  gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n");
        else if (w & (1u << RESULT_POSITION)) emit(&t, "  gl_Position = %s;\n", result_variable(RESULT_POSITION));
        if (w & (1u << RESULT_COLOR0)) emit(&t, "  gl_FrontColor = %s;\n", result_variable(RESULT_COLOR0));
        if (w & (1u << RESULT_COLOR1)) emit(&t, "  gl_FrontSecondaryColor = %s;\n", result_variable(RESULT_COLOR1));
        if (w & (1u << RESULT_BACK0)) emit(&t, "  gl_BackColor = %s;\n", result_variable(RESULT_BACK0));
        if (w & (1u << RESULT_BACK1)) emit(&t, "  gl_BackSecondaryColor = %s;\n", result_variable(RESULT_BACK1));
        if (w & (1u << RESULT_FOG)) emit(&t, "  gl_FogFragCoord = %s.x;\n", result_variable(RESULT_FOG));
        if (w & (1u << RESULT_POINT)) emit(&t, "  gl_PointSize = %s.x;\n", result_variable(RESULT_POINT));
        if (p->clips_written && p->clip_vertex_ok) emit(&t, "  gl_ClipVertex = glm_clip_vertex;\n");
        for (int u = 0; u < 8; ++u)
            if (p->texcoords_written & (1 << u)) emit(&t, "  gl_TexCoord[%d] = %s;\n", u, result_variable(RESULT_TEX0 + u));
    } else {
        if (p->fog_option && p->draw_buffers_written) {
            const char *r = result_variable(RESULT_DRAW0);
            switch (p->fog_option) {
            case 1: emit(&t, "  float glm_fog = (gl_Fog.end - abs(gl_FogFragCoord)) * gl_Fog.scale;\n"); break;
            case 2: emit(&t, "  float glm_fog = exp(-gl_Fog.density * abs(gl_FogFragCoord));\n"); break;
            case 3: emit(&t, "  float glm_fog = exp(-(gl_Fog.density * gl_FogFragCoord) * (gl_Fog.density * gl_FogFragCoord));\n"); break;
            }
            emit(&t, "  %s.rgb = mix(gl_Fog.color.rgb, %s.rgb, clamp(glm_fog, 0.0, 1.0));\n", r, r);
        }
        if (p->draw_buffers_written == 1) emit(&t, "  gl_FragColor = %s;\n", result_variable(RESULT_DRAW0));
        else
            for (int i = 0; i < p->draw_buffers_written; ++i)
                emit(&t, "  gl_FragData[%d] = %s;\n", i, result_variable(RESULT_DRAW0 + i));
        if (w & (1u << RESULT_DEPTH)) emit(&t, "  gl_FragDepth = %s.z;\n", result_variable(RESULT_DEPTH));
    }
    emit(&t, "}\n");
    out->glsl = t.data;
    free(p->labels);
    free(p->declarations.data);
    free(p->body.data);
    free(p->prologue.data);
    free(p);
    return true;
}
