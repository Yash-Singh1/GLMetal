/* EXT_bindable_uniform: uniforms whose values come from buffer objects
 * (glUniformBufferEXT). The compiler sees them as plain uniforms
 * (shader_compiler.cpp drops `bindable`); at link the declarations are read
 * from the sources to lay each one out as Apple does in its buffer, a vec4
 * slot per scalar, vector and matrix column in declaration order (members,
 * array elements and all, used or not); before each draw the bound
 * buffers' contents are loaded into the uniforms (tests/probes/bindable_uniform.c). */
#include "programs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_MEMBERS = 64, MAX_TYPES = 64, MAX_DECLS = 64 };

/* A declared type: a basic type (`slots` per element) or a struct. */
struct member {
    char name[64];
    int type; /* index in `types` */
    int array; /* elements, 0 when not an array */
};
struct type {
    char name[64];
    int slots;          /* basic types; structs: the sum of their members */
    int member_count;
    struct member members[MAX_MEMBERS];
};
struct parse {
    struct type types[MAX_TYPES];
    int type_count;
    struct member decls[MAX_DECLS]; /* the bindable uniforms */
    int decl_count;
};

/* The source without comments or preprocessor lines. */
static char *strip(const char *source)
{
    size_t n = strlen(source);
    char *out = malloc(n + 1), *o = out;
    bool line_start = true;
    for (size_t i = 0; i < n;) {
        if (source[i] == '/' && source[i + 1] == '/') {
            while (i < n && source[i] != '\n') ++i;
        } else if (source[i] == '/' && source[i + 1] == '*') {
            i += 2;
            while (i < n && !(source[i] == '*' && source[i + 1] == '/')) ++i;
            i += 2;
            *o++ = ' ';
        } else if (line_start && source[i] == '#') {
            while (i < n && source[i] != '\n') ++i;
        } else {
            if (source[i] == '\n') line_start = true;
            else if (!isspace((unsigned char)source[i])) line_start = false;
            *o++ = source[i++];
        }
    }
    *o = 0;
    return out;
}

/* The next token: an identifier, a number or one punctuation character. */
static const char *token(const char *s, char *out, size_t room)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    size_t n = 0;
    if (isalnum((unsigned char)*s) || *s == '_') {
        while ((isalnum((unsigned char)*s) || *s == '_') && n + 1 < room) out[n++] = *s++;
        while (isalnum((unsigned char)*s) || *s == '_') ++s;
    } else if (*s) {
        out[n++] = *s++;
    }
    out[n] = 0;
    return s;
}

static int basic_slots(const char *name)
{
    static const char *const scalars[] = {"float", "int", "uint", "bool"};
    for (size_t i = 0; i < 4; ++i)
        if (!strcmp(name, scalars[i])) return 1;
    if ((!strncmp(name, "vec", 3) || !strncmp(name, "ivec", 4) || !strncmp(name, "uvec", 4) || !strncmp(name, "bvec", 4)) &&
        isdigit((unsigned char)name[strlen(name) - 1]))
        return 1;
    if (!strncmp(name, "mat", 3) && isdigit((unsigned char)name[3])) return name[3] - '0'; /* matN, matNxM: N columns */
    return 0;
}

static int find_type(struct parse *p, const char *name)
{
    for (int i = 0; i < p->type_count; ++i)
        if (!strcmp(p->types[i].name, name)) return i;
    int slots = basic_slots(name);
    if (!slots || p->type_count == MAX_TYPES) return -1;
    struct type *t = &p->types[p->type_count];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    t->slots = slots;
    return p->type_count++;
}

static bool qualifier(const char *word)
{
    static const char *const words[] = {"lowp", "mediump", "highp", "const", "uniform", "flat", "smooth", "invariant"};
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        if (!strcmp(word, words[i])) return true;
    return false;
}

/* Declarators after a type up to ';': names with optional [N]. */
static const char *declarators(const char *s, int type, struct member *out, int *count, int room)
{
    char word[64];
    for (;;) {
        s = token(s, word, sizeof word);
        if (!*word || !strcmp(word, ";")) return s;
        if (!strcmp(word, ",")) continue;
        if (!(isalpha((unsigned char)word[0]) || word[0] == '_')) continue;
        struct member m = {.type = type};
        snprintf(m.name, sizeof m.name, "%s", word);
        const char *after = token(s, word, sizeof word);
        if (!strcmp(word, "[")) {
            after = token(after, word, sizeof word);
            m.array = atoi(word);
            after = token(after, word, sizeof word); /* ] */
            s = after;
        }
        if (*count < room) out[(*count)++] = m;
    }
}

static int type_slots(const struct parse *p, int type)
{
    return type >= 0 ? p->types[type].slots : 0;
}

static void parse_source(struct parse *p, const char *source)
{
    char *text = strip(source);
    const char *s = text;
    char word[64];
    bool bindable = false;
    for (;;) {
        s = token(s, word, sizeof word);
        if (!*word) break;
        if (!strcmp(word, "bindable")) {
            bindable = true;
            continue;
        }
        if (!strcmp(word, "struct")) {
            char name[64];
            s = token(s, name, sizeof name);
            s = token(s, word, sizeof word); /* { */
            if (strcmp(word, "{") || p->type_count == MAX_TYPES) continue;
            struct type *t = &p->types[p->type_count];
            memset(t, 0, sizeof *t);
            snprintf(t->name, sizeof t->name, "%s", name);
            int index = p->type_count++;
            for (;;) {
                s = token(s, word, sizeof word);
                if (!*word || !strcmp(word, "}")) break;
                if (qualifier(word)) continue;
                int member_type = find_type(p, word);
                s = declarators(s, member_type, p->types[index].members, &p->types[index].member_count, MAX_MEMBERS);
            }
            for (int m = 0; m < p->types[index].member_count; ++m) {
                const struct member *mm = &p->types[index].members[m];
                p->types[index].slots += type_slots(p, mm->type) * (mm->array ? mm->array : 1);
            }
            /* struct S { ... } name; */
            s = bindable ? declarators(s, index, p->decls, &p->decl_count, MAX_DECLS) : declarators(s, index, NULL, &(int){0}, 0);
            bindable = false;
            continue;
        }
        if (bindable) {
            if (qualifier(word)) continue;
            int type = find_type(p, word);
            s = declarators(s, type, p->decls, &p->decl_count, MAX_DECLS);
            bindable = false;
        }
    }
    free(text);
}

/* The slot of `path` ("s.b", "sa[1].c", "va[1]") within its declaration, or -1. */
static int path_slot(const struct parse *p, const struct member *decl, const char *path)
{
    int type = decl->type, array = decl->array, slot = 0;
    const char *at = path + strlen(decl->name);
    for (;;) {
        if (*at == '[') {
            int index = atoi(at + 1);
            if (array && index < array) slot += index * type_slots(p, type);
            array = 0;
            at = strchr(at, ']');
            if (!at) return -1;
            ++at;
        } else if (*at == '.') {
            ++at;
            if (type < 0) return -1;
            const struct type *t = &p->types[type];
            size_t n = strcspn(at, ".[");
            int found = -1;
            for (int m = 0; m < t->member_count; ++m) {
                if (strlen(t->members[m].name) == n && !strncmp(t->members[m].name, at, n)) {
                    found = m;
                    break;
                }
                slot += type_slots(p, t->members[m].type) * (t->members[m].array ? t->members[m].array : 1);
            }
            if (found < 0) return -1;
            type = t->members[found].type;
            array = t->members[found].array;
            at += n;
        } else {
            return slot;
        }
    }
}

void glm_bindable_free(struct glm_program *p)
{
    for (int i = 0; i < p->bindable_count; ++i) free(p->bindables[i].name);
    free(p->bindables);
    free(p->bindable_locations);
    p->bindables = NULL;
    p->bindable_locations = NULL;
    p->bindable_count = 0;
}

void glm_bindable_link(struct glm_program *p)
{
    glm_bindable_free(p);
    struct parse *parse = NULL;
    for (int s = 0; s < GLM_STAGE_COUNT; ++s) {
        if (!p->linked_sources[s] || !strstr(p->linked_sources[s], "bindable")) continue;
        if (!parse) parse = calloc(1, sizeof *parse);
        if (parse) parse_source(parse, p->linked_sources[s]);
    }
    if (!parse || !parse->decl_count) {
        free(parse);
        return;
    }
    /* One per name (stages may share a declaration). */
    p->bindables = calloc((size_t)parse->decl_count, sizeof *p->bindables);
    int decl_of[MAX_DECLS];
    for (int d = 0; d < parse->decl_count; ++d) {
        const struct member *m = &parse->decls[d];
        int existing = -1;
        for (int b = 0; b < p->bindable_count; ++b)
            if (!strcmp(p->bindables[b].name, m->name)) existing = b;
        if (existing >= 0) continue;
        decl_of[p->bindable_count] = d;
        p->bindables[p->bindable_count].name = strdup(m->name);
        p->bindables[p->bindable_count++].slots = type_slots(parse, m->type) * (m->array ? m->array : 1);
    }
    p->bindable_locations = malloc((size_t)(p->location_count + 1) * sizeof *p->bindable_locations);
    for (int l = 0; l < p->location_count; ++l) {
        p->bindable_locations[l] = (struct glm_bindable_location){-1, 0};
        const struct glm_uniform_info *u = &p->result.uniforms[p->locations[l].uniform];
        char path[256];
        snprintf(path, sizeof path, "%s", u->name);
        if (u->is_array || u->array_size > 1) {
            /* "va[0]" (or "va") with the location's element. */
            size_t n = strlen(path);
            char *bracket = n && path[n - 1] == ']' ? strrchr(path, '[') : path + n;
            snprintf(bracket, sizeof path - (size_t)(bracket - path), "[%d]", p->locations[l].element);
        }
        size_t top = strcspn(path, ".[");
        for (int b = 0; b < p->bindable_count; ++b) {
            if (strlen(p->bindables[b].name) != top || strncmp(p->bindables[b].name, path, top)) continue;
            int slot = path_slot(parse, &parse->decls[decl_of[b]], path);
            if (slot < 0) continue;
            p->bindable_locations[l] = (struct glm_bindable_location){b, slot};
            p->bindables[b].active = true;
        }
    }
    free(parse);
}

void glm_bindable_update(struct glm_context *ctx, struct glm_program *p)
{
    for (int l = 0; l < p->location_count; ++l) {
        struct glm_bindable_location at = p->bindable_locations[l];
        if (at.bindable < 0) continue;
        const struct glm_bindable *b = &p->bindables[at.bindable];
        struct glm_buffer *buffer = b->buffer ? glm_buffer_get(ctx, b->buffer) : NULL;
        if (!buffer) continue;
        const uint8_t *contents = glm_backend_buffer_contents(ctx, buffer, false);
        const struct glm_uniform_info *u = &p->result.uniforms[p->locations[l].uniform];
        size_t end = (size_t)(at.slot + glm_uniform_type_slots(u->type)) * 16;
        if (!contents || end > (size_t)buffer->size) continue;
        glm_program_load_slots(p, l, contents + (size_t)at.slot * 16);
    }
}

/* A bindable uniform's buffer when it can back it: bound, big enough and
   bound to no other bindable uniform of the program. */
static struct glm_buffer *backing(struct glm_context *ctx, const struct glm_program *p, int bindable)
{
    const struct glm_bindable *b = &p->bindables[bindable];
    struct glm_buffer *buffer = b->buffer ? glm_buffer_get(ctx, b->buffer) : NULL;
    if (!buffer || (size_t)buffer->size < (size_t)b->slots * 16) return NULL;
    for (int other = 0; other < p->bindable_count; ++other)
        if (other != bindable && p->bindables[other].active && p->bindables[other].buffer == b->buffer) return NULL;
    return buffer;
}

bool glm_bindable_draw_invalid(struct glm_context *ctx)
{
    struct glm_program *p = glm_current_program(ctx);
    if (!p || !p->bindable_count) return false;
    for (int b = 0; b < p->bindable_count; ++b)
        if (p->bindables[b].active && !backing(ctx, p, b)) return true;
    return false;
}

bool glm_bindable_store(struct glm_context *ctx, struct glm_program *p, GLint location, GLsizei count, int columns,
                        int rows, const void *values)
{
    int bindable = p->bindable_locations[location].bindable;
    struct glm_buffer *buffer = backing(ctx, p, bindable);
    uint8_t *contents = buffer ? glm_backend_buffer_contents(ctx, buffer, true) : NULL;
    if (!contents) return glm_error(ctx, GL_INVALID_OPERATION), false;
    const uint8_t *src = values;
    for (GLsizei e = 0; e < count && location + e < p->location_count; ++e) {
        struct glm_bindable_location at = p->bindable_locations[location + e];
        if (at.bindable != bindable) break;
        for (int c = 0; c < columns; ++c, src += (size_t)rows * 4)
            memcpy(contents + (size_t)(at.slot + c) * 16, src, (size_t)rows * 4);
    }
    return true;
}

/* ---- entry points --------------------------------------------------------------- */

/* The linked program and the bindable uniform at `location`, NULL after an error. */
static struct glm_program *bindable_at(struct glm_context *ctx, GLuint program, GLint location,
                                       struct glm_bindable_location *at)
{
    struct glm_program *p = glm_program_get(ctx, program);
    if (!p) return glm_error(ctx, GL_INVALID_VALUE), NULL;
    if (!p->linked || location < 0 || location >= p->location_count || !p->bindable_locations ||
        p->bindable_locations[location].bindable < 0)
        return glm_error(ctx, GL_INVALID_OPERATION), NULL;
    *at = p->bindable_locations[location];
    return p;
}

GLM_EXPORT void glUniformBufferEXT(GLuint program, GLint location, GLuint buffer)
{
    GLM_CONTEXT(ctx);
    struct glm_bindable_location at;
    struct glm_program *p = bindable_at(ctx, program, location, &at);
    if (!p) return;
    if (buffer && !glm_buffer_get(ctx, buffer)) return glm_error(ctx, GL_INVALID_OPERATION);
    p->bindables[at.bindable].buffer = buffer;
}

GLM_EXPORT GLint glGetUniformBufferSizeEXT(GLuint program, GLint location)
{
    GLM_CONTEXT(ctx, 0);
    struct glm_bindable_location at;
    struct glm_program *p = bindable_at(ctx, program, location, &at);
    return p ? p->bindables[at.bindable].slots * 16 : 0;
}

GLM_EXPORT GLintptr glGetUniformOffsetEXT(GLuint program, GLint location)
{
    GLM_CONTEXT(ctx, 0);
    struct glm_bindable_location at;
    struct glm_program *p = bindable_at(ctx, program, location, &at);
    return p ? (GLintptr)at.slot * 16 : 0;
}
