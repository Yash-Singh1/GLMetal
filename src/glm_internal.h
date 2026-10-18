/* GLMetal internals: context state and the interfaces between the GL front
 * end (C) and the Metal backend (Objective-C). */
#ifndef GLM_INTERNAL_H
#define GLM_INTERNAL_H

#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

#include "glm_core_enums.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLM_EXPORT __attribute__((visibility("default")))
#define GLM_HIDDEN __attribute__((visibility("hidden")))

enum { GLM_MAX_ATTRIBS = 16, GLM_MAX_TEXTURE_UNITS = 16, GLM_MAX_CLIP_PLANES = 8,
       GLM_MAX_LIGHTS = 8, GLM_MAX_DRAW_BUFFERS = 8, GLM_MATRIX_STACK_DEPTH = 32, GLM_MAX_PATCH_VERTICES = 32, GLM_MAX_VIEWPORTS = 16,
       GLM_MAX_AUX_BUFFERS = 2 };

/* Fixed attribute slots for legacy arrays; generic attributes share them the
   way NVIDIA-style aliasing does on Apple (0 = position, 3 = color...). */
enum glm_attrib {
    GLM_ATTR_POSITION = 0, GLM_ATTR_WEIGHT = 1, GLM_ATTR_NORMAL = 2, GLM_ATTR_COLOR = 3,
    GLM_ATTR_SECONDARY_COLOR = 4, GLM_ATTR_FOG = 5, GLM_ATTR_POINT_SIZE = 7, GLM_ATTR_TEX0 = 8,
};

/* ---- objects ------------------------------------------------------------- */

struct glm_object_table {
    void **items;
    GLuint capacity;
    GLuint next_name;
};

struct glm_buffer {
    GLuint name;
    bool bound;          /* bound once: glIsBuffer is true from then on */
    GLsizeiptr size;
    GLenum usage;
    void *backend;       /* id<MTLBuffer>, retained */
    uint64_t gpu_serial; /* last command buffer that read it */
    /* Byte ranges read by GPU work not known to be complete, per command
       buffer, for writes that would race them (see
       glm_backend_buffer_prepare_write). */
    struct { uint64_t serial; GLintptr low, high; } gpu_reads[8];
    int gpu_read_count;
    /* UINT64_MAX ranges have unknown completion and survive a NULL reader
       until storage changes. The raw backend pointer still requires callers
       to serialize shared-object access against context destruction. */
    void *gpu_reader;    /* struct glm_backend_context of known reads */
    uint64_t gpu_write_serial; /* transform feedback: CPU reads wait for it */
    void *gpu_writer;
    void *mapped;
    GLenum map_access;
    GLintptr map_offset;
    GLsizeiptr map_length;
    bool apple_flushing_unmap, apple_serialized_modify;
    uint64_t content_generation; /* bumped by every write: memoized index ranges compare it */
};

struct glm_texture_level {
    GLsizei width, height, depth;
    GLenum internal_format;
    bool defined;
};

enum { GLM_MAX_LEVELS = 16 };

struct glm_sampler_state {
    GLenum min_filter, mag_filter, wrap_s, wrap_t, wrap_r;
    GLenum compare_mode, compare_func;
    float border_color[4];
    float min_lod, max_lod, lod_bias, max_anisotropy;
    bool skip_srgb_decode;      /* GL_TEXTURE_SRGB_DECODE_EXT = GL_SKIP_DECODE_EXT (texture or sampler object) */
};

struct glm_texture {
    GLuint name;
    GLenum target;              /* 0 until first bound */
    struct glm_texture_level levels[6][GLM_MAX_LEVELS]; /* [face][level] */
    struct glm_sampler_state sampler;
    GLint base_level, max_level;
    bool generate_mipmap;
    GLenum storage_hint;    /* GL_TEXTURE_STORAGE_HINT_APPLE; 0 = GL_STORAGE_PRIVATE_APPLE */
    bool minimize_storage; /* GL_TEXTURE_MINIMIZE_STORAGE_APPLE */
    GLenum depth_texture_mode;
    float priority;             /* GL_TEXTURE_PRIORITY (legacy) */
    float compare_fail_value;   /* GL_TEXTURE_COMPARE_FAIL_VALUE_ARB (legacy) */
    GLint apple_private[2];     /* 0x8A4B and 0x8A7F: Apple takes and answers them */
    uint32_t metal_format;      /* MTLPixelFormat of the storage */
    void *backend;              /* id<MTLTexture> */
    uint64_t storage_generation;
    /* CPU validation caches. Definitions change independently of texel data;
       uploads/FBO writes keep the same storage and sampling decisions. */
    uint64_t definition_generation;
    struct glm_texture_storage_key {
        GLenum target, internal_format;
        GLsizei width, height, depth, samples;
        GLint base;
    } storage_validation_key;
    bool storage_validation_valid;
    struct glm_texture_sampling_key {
        uint64_t definitions, storage;
        GLint base, max, immutable_levels;
        GLenum min_filter, depth_mode;
        uint8_t swizzle[4];
        bool immutable, core, skip_decode;
    } sampling_validation_key;
    bool sampling_validation_valid, sampling_validation_mipmapped;
    void *sampling_validation_result; /* borrowed from backend or view */
    uint64_t gpu_serial;
    bool storage_dirty;         /* level layout changed: storage must be rebuilt */
    uint8_t swizzle[4];         /* GL_TEXTURE_SWIZZLE_*: 0..3 r g b a, 4 zero, 5 one */
    GLsizei samples;            /* multisample textures */
    bool fixed_sample_locations;
    bool immutable;             /* glTexStorage */
    GLint immutable_levels;
    /* GL_TEXTURE_BUFFER: the buffer object and format (glTexBuffer). */
    GLuint buffer;
    GLenum buffer_format;
    /* The last sampling view and what it was made for. */
    void *view;
    void *view_storage;
    uint32_t view_key;
    /* Parallel 2D-array view used by nonseamless cube shadow sampling. */
    void *cube_array_view;
    void *cube_array_source; /* identity only; the array view retains its source */
    /* Whole-level images the storage could not hold yet (levels defined
       before the base level, out of chain order): uploaded once it can. */
    struct glm_pending_level *pending;
};

struct glm_pending_level {
    struct glm_pending_level *next;
    int face, level;
    GLsizei width, height, depth;
    size_t row_bytes, image_bytes;
    void *data;
};

struct glm_renderbuffer {
    GLuint name;
    GLenum internal_format;   /* as Apple reports it (sized) */
    uint8_t bits[6];          /* red green blue alpha depth stencil, as Apple reports them */
    GLsizei width, height, samples;
    uint32_t metal_format;
    void *backend; /* id<MTLTexture> */
};

struct glm_attachment {
    GLenum type; /* GL_NONE, GL_TEXTURE, GL_RENDERBUFFER */
    GLuint name;
    GLint level, layer;
    GLenum face;
    bool layered; /* glFramebufferTexture of an array, cube or 3D texture: every layer */
};

struct glm_framebuffer {
    GLuint name;
    struct glm_attachment color[GLM_MAX_DRAW_BUFFERS], depth, stencil;
    GLenum draw_buffers[GLM_MAX_DRAW_BUFFERS];
    GLenum read_buffer;
};

/* ---- vertex arrays ------------------------------------------------------- */

struct glm_array {
    bool enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    bool normalized, integer;
    const void *pointer; /* offset when buffer != 0 */
    GLuint buffer;
    GLuint divisor;
};

/* Client arrays outside the attribute slots: edge flags, colour indices,
   ARB_vertex_blend weights and APPLE_vertex_point_size sizes. */
enum { GLM_LEGACY_EDGE_FLAG, GLM_LEGACY_INDEX, GLM_LEGACY_WEIGHT, GLM_LEGACY_POINT_SIZE, GLM_LEGACY_ARRAYS };

struct glm_vertex_array {
    GLuint name;
    uint64_t serial; /* glm_vao_changed: bumped by every change of the arrays */
    struct glm_array arrays[GLM_MAX_ATTRIBS];
    struct glm_array legacy[GLM_LEGACY_ARRAYS];
    GLuint element_buffer;
    GLenum client_active_texture;
};

/* ---- fixed function ------------------------------------------------------ */

struct glm_light {
    float ambient[4], diffuse[4], specular[4], position[4], spot_direction[3];
    float spot_exponent, spot_cutoff, attenuation[3];
    bool enabled;
};

/* Evaluators (evaluators.c): per dimension GL_MAP*_COLOR_4 ... VERTEX_4. */
enum { GLM_EVAL_MAPS = 9, GLM_MAX_EVAL_ORDER = 10 };
struct glm_eval_map {
    GLint size;      /* components (APPLE_vertex_program_evaluators' attribute maps) */
    GLint order[2];  /* u, v (1 for one-dimensional maps) */
    float domain[4]; /* u1, u2, v1, v2 */
    float *points;   /* order[0] * order[1] points of the target's components, v fastest */
};

struct glm_material {
    float ambient[4], diffuse[4], specular[4], emission[4], shininess;
};
/* A vertex's materials: per face emission, ambient, diffuse, specular, then
   the front and back shininess. */
enum { GLM_VERTEX_MATERIAL_FLOAT4S = 9, GLM_ATTR_MATERIAL_INDEX = 6 };
struct glm_context;
GLM_HIDDEN void glm_immediate_material(struct glm_context *ctx);
GLM_HIDDEN bool glm_ff_vertex_extras(struct glm_context *ctx);
/* EXT_bindable_uniform: draws fail while a bindable uniform of the current
   program has no buffer, too small a buffer, or one another shares. */
GLM_HIDDEN bool glm_bindable_draw_invalid(struct glm_context *ctx);
GLM_HIDDEN void glm_eval_init(struct glm_context *ctx);
GLM_HIDDEN void glm_eval_destroy(struct glm_context *ctx);
GLM_HIDDEN bool *glm_eval_capability(struct glm_context *ctx, GLenum cap);
GLM_HIDDEN int glm_eval_query(struct glm_context *ctx, GLenum pname, double *v);
GLM_HIDDEN int glm_eval_components(GLenum target);
/* glGetVertexAttrib*v of the attribute maps: the count of values, -1 for other pnames. */
GLM_HIDDEN int glm_eval_attrib_query(struct glm_context *ctx, GLuint index, GLenum pname, double *v);
struct glm_texture;
GLM_HIDDEN GLboolean glm_texture_resident(const struct glm_texture *t);

/* The imaging subset (imaging.c; ARB_imaging, GL 2.1 3.6.3, 3.6.5): the
   colour tables, convolution filters, histogram and minmax enables in this
   order, and their parameters (the pixel attribute group). Tables, filter
   images and counters live in glm_context.imaging. */
enum {
    GLM_IMAGING_COLOR_TABLE, GLM_IMAGING_POST_CONVOLUTION_TABLE, GLM_IMAGING_POST_COLOR_MATRIX_TABLE,
    GLM_IMAGING_CONVOLUTION_1D, GLM_IMAGING_CONVOLUTION_2D, GLM_IMAGING_SEPARABLE_2D, GLM_IMAGING_HISTOGRAM,
    GLM_IMAGING_MINMAX, GLM_IMAGING_ENABLES
};
struct glm_imaging_state {
    bool enabled[GLM_IMAGING_ENABLES];
    float table_scale[3][4], table_bias[3][4];
    GLenum border_mode[3]; /* 1D, 2D, separable */
    float border_color[3][4], filter_scale[3][4], filter_bias[3][4];
    float post_convolution_scale[4], post_convolution_bias[4];
    float post_color_matrix_scale[4], post_color_matrix_bias[4];
};
struct glm_imaging_objects;
GLM_HIDDEN void glm_imaging_init(struct glm_context *ctx);
GLM_HIDDEN void glm_imaging_destroy(struct glm_context *ctx);
GLM_HIDDEN bool *glm_imaging_capability(struct glm_context *ctx, GLenum cap);
GLM_HIDDEN int glm_imaging_query(struct glm_context *ctx, GLenum pname, double *v);
GLM_HIDDEN bool glm_imaging_transfer(struct glm_context *ctx, GLenum pname, GLfloat value);
/* Whether colour images of `dims` dimensions (1: 1D textures; 2: images;
   3: 3D texture slices, never convolved) go through the imaging operations. */
GLM_HIDDEN bool glm_imaging_active(const struct glm_context *ctx, int dims);
/* Runs the imaging operations on a float RGBA image (replaced when its size
   changes). False when the histogram or minmax sink discards it. */
GLM_HIDDEN bool glm_imaging_apply(struct glm_context *ctx, int dims, float **rgba, GLsizei *width, GLsizei *height);
/* The size of a `dims`-dimensional image after convolution. */
GLM_HIDDEN void glm_imaging_size(const struct glm_context *ctx, int dims, GLsizei *width, GLsizei *height);
/* glReadPixels' colour data: glPixelTransfer, then the imaging operations
   (false: nothing is returned). */
GLM_HIDDEN bool glm_imaging_read(struct glm_context *ctx, float **rgba, GLsizei *width, GLsizei *height);
/* Pixel data passed on without pixel transfer or imaging, tightly packed
   and from client memory: around calls that re-enter the GL with images the
   imaging operations already processed. */
struct glm_pixels_raw {
    GLint unpack[8];
    bool swap_bytes, lsb_first, client_storage, raw;
    GLuint unpack_buffer;
};
GLM_HIDDEN void glm_pixels_raw_begin(struct glm_context *ctx, struct glm_pixels_raw *saved);
GLM_HIDDEN void glm_pixels_raw_end(struct glm_context *ctx, const struct glm_pixels_raw *saved);
/* pixels.c: client (or unpack buffer) pixels as float RGBA, through
   glPixelTransfer when `transfer`; NULL if nothing can be read. */
GLM_HIDDEN float *glm_unpack_rgba(struct glm_context *ctx, GLsizei width, GLsizei height, GLsizei depth, GLenum format,
                                  GLenum type, const void *pixels, bool transfer);
/* pixels.c: glPixelTransfer's RGBA scale, bias and colour maps on `count` pixels. */
GLM_HIDDEN void glm_transfer_rgba(struct glm_context *ctx, float *rgba, size_t count);
GLM_HIDDEN bool glm_transfer_identity(const struct glm_context *ctx);
/* raster.c: the read buffer's pixels as float RGBA (default pack state). */
GLM_HIDDEN float *glm_read_rgba(struct glm_context *ctx, GLint x, GLint y, GLsizei width, GLsizei height);

struct glm_texenv {
    GLenum mode;
    float color[4];
    GLenum combine_rgb, combine_alpha;
    GLenum source_rgb[3], source_alpha[3], operand_rgb[3], operand_alpha[3];
    float rgb_scale, alpha_scale;
    float lod_bias;
};

struct glm_texgen {
    GLenum mode;
    float object_plane[4], eye_plane[4];
    bool enabled;
};

struct glm_texture_unit {
    GLuint bound[16]; /* by enum glm_binding_slot */
    GLuint sampler;
    bool enabled_1d, enabled_2d, enabled_3d, enabled_cube, enabled_rect;
    struct glm_texenv env;
    struct glm_texgen gen[4]; /* s t r q */
};

enum glm_binding_slot { GLM_TEX_1D, GLM_TEX_2D, GLM_TEX_3D, GLM_TEX_CUBE, GLM_TEX_RECT, GLM_TEX_2D_ARRAY, GLM_TEX_BUFFER,
                        GLM_TEX_2D_MULTISAMPLE, GLM_TEX_2D_MULTISAMPLE_ARRAY, GLM_TEX_1D_ARRAY, GLM_TEX_CUBE_ARRAY,
                        GLM_TEX_SLOTS };

struct glm_matrix_stack {
    float m[GLM_MATRIX_STACK_DEPTH][16];
    int depth;
};

/* ---- per-fragment state -------------------------------------------------- */

struct glm_blend_state {
    bool enabled;
    GLenum src_rgb, dst_rgb, src_alpha, dst_alpha, eq_rgb, eq_alpha;
};

struct glm_stencil_face {
    GLenum func, fail, zfail, zpass;
    GLint ref;
    GLuint value_mask, write_mask;
};

/* Everything a draw's pipeline and fixed-function shaders depend on lives
   in ordinary fields; the backend hashes what it needs. */
struct glm_state {
    GLint viewport[4];
    double depth_range[2];
    GLint scissor[4];
    /* ARB_viewport_array: every index (0 mirrors the fields above). */
    float viewports[GLM_MAX_VIEWPORTS][4];
    double depth_ranges[GLM_MAX_VIEWPORTS][2];
    GLint scissors[GLM_MAX_VIEWPORTS][4];
    bool scissor_tests[GLM_MAX_VIEWPORTS];
    bool sample_mask_enabled, sample_shading;
    GLbitfield sample_mask;           /* GL_SAMPLE_MASK_VALUE word 0 */
    float min_sample_shading;
    float sample_coverage;            /* glSampleCoverage */
    bool sample_coverage_invert;
    bool scissor_test, depth_test, depth_mask, stencil_test, cull_face, alpha_test, blend_any, dither,
         color_logic_op, polygon_offset_fill, polygon_offset_line, polygon_offset_point, depth_clamp,
         rasterizer_discard, primitive_restart, program_point_size, multisample, sample_alpha_to_coverage,
         framebuffer_srgb, lighting, color_material, normalize, rescale_normal, fog, point_sprite,
         line_smooth, polygon_smooth, polygon_stipple, line_stipple, vertex_program, fragment_program, vertex_program_two_side, stencil_two_side,
         stencil_back_ext, texture_cube_seamless;
    bool clip_plane_enabled[GLM_MAX_CLIP_PLANES];
    double clip_planes[GLM_MAX_CLIP_PLANES][4]; /* eye space */
    GLenum provoking_vertex, active_stencil_face;
    GLint patch_vertices; /* GL_PATCH_VERTICES */
    float patch_default_outer[4], patch_default_inner[2];
    GLenum depth_func, cull_mode, front_face, shade_model, polygon_mode[2], logic_op, alpha_func;
    float alpha_ref, polygon_offset_factor, polygon_offset_units, point_size, line_width;
    GLuint primitive_restart_index;
    struct glm_blend_state blend[GLM_MAX_DRAW_BUFFERS];
    float blend_color[4];
    bool color_mask[GLM_MAX_DRAW_BUFFERS][4];
    /* front, back (GL 2.0 separate stencil), back of EXT_stencil_two_side
       (glActiveStencilFaceEXT(GL_BACK); drawn with while
       GL_STENCIL_TEST_TWO_SIDE_EXT is enabled). glm_stencil_back picks. */
    struct glm_stencil_face stencil[3];
    float clear_color[4];
    float clear_accum[4];   /* glClearAccum, clamped to [-1, 1] */
    uint8_t polygon_stipple_pattern[128]; /* glPolygonStipple: 32 rows of 4 bytes, MSB first */
    bool edge_flag;         /* glEdgeFlag */
    GLint line_stipple_factor;
    GLushort line_stipple_pattern;
    float current_index, clear_index; /* colour-index state, kept in RGBA mode (legacy_misc.c) */
    GLuint index_mask;
    GLint clear_color_integer[4];     /* glClearColorIiEXT / glClearColorIuiEXT */
    float current_weight[4];          /* ARB_vertex_blend: GL_CURRENT_WEIGHT_ARB */
    GLint active_vertex_units;
    float current_point_size;         /* APPLE_vertex_point_size */
    bool vertex_blend, weight_sum_unity, vertex_point_size; /* GL_VERTEX_BLEND_ARB, ..._SUM_UNITY_ARB, GL_VERTEX_POINT_SIZE_APPLE */
    bool map_enabled[2][GLM_EVAL_MAPS]; /* GL_MAP1_*, GL_MAP2_* */
    uint16_t attrib_maps_enabled[2]; /* GL_VERTEX_ATTRIB_MAP1/2_APPLE per attribute */
    bool auto_normal;
    GLint eval_grid_segments[2][2]; /* glMapGrid1: [0][0]; glMapGrid2: [1][0..1] */
    float eval_grid_domain[2][4];
    double clear_depth;
    GLint clear_stencil;

    /* current vertex attributes (immediate mode) */
    float current[GLM_MAX_ATTRIBS][4];
    float current_fog_coord;
    float raster_pos[4];
    bool raster_valid;
    float raster_color[4];
    float raster_texcoord[8][4]; /* GL_CURRENT_RASTER_TEXTURE_COORDS per unit */
    float raster_distance;  /* eye depth (or the fog coordinate) fogging pixel rectangles */
    float pixel_scale[4], pixel_bias[4]; /* glPixelTransfer RGBA scale and bias */
    /* glPixelMap tables, GL_PIXEL_MAP_I_TO_I (0) .. GL_PIXEL_MAP_A_TO_A (9),
       and the rest of glPixelTransfer. */
    float pixel_map[10][256];
    GLint pixel_map_size[10];
    bool map_color, map_stencil;
    GLint index_shift, index_offset;
    float depth_scale, depth_bias;
    float pixel_zoom[2];
    struct glm_imaging_state imaging;

    /* transforms */
    GLenum matrix_mode;
    struct glm_matrix_stack modelview, projection, texture[GLM_MAX_TEXTURE_UNITS], color;
    struct glm_matrix_stack vertex_units[3]; /* ARB_vertex_blend: GL_MODELVIEW1_ARB .. GL_MODELVIEW3_ARB */

    /* lighting */
    struct glm_light lights[GLM_MAX_LIGHTS];
    struct glm_material material[2];
    float light_model_ambient[4];
    bool light_model_local_viewer, light_model_two_side, light_model_separate_specular;
    GLenum color_material_face, color_material_mode;

    /* fog */
    GLenum fog_mode, fog_coord_source;
    float fog_color[4], fog_density, fog_start, fog_end;

    /* textures */
    GLenum active_texture;
    struct glm_texture_unit units[GLM_MAX_TEXTURE_UNITS];

    /* pixel store */
    GLint unpack_alignment, unpack_row_length, unpack_skip_rows, unpack_skip_pixels, unpack_image_height,
          unpack_skip_images, pack_alignment, pack_row_length, pack_skip_rows, pack_skip_pixels, pack_image_height,
          pack_skip_images;
    bool unpack_swap_bytes, unpack_lsb_first, unpack_client_storage, pack_swap_bytes;
    GLint unpack_row_bytes, unpack_image_bytes, pack_row_bytes, pack_image_bytes; /* APPLE_row_bytes: 0 = unset */
};

/* ---- context ------------------------------------------------------------- */

struct glm_share_group {
    int refcount;
    pthread_mutex_t lock;
    struct glm_object_table buffers, textures, renderbuffers, programs, shaders, arb_programs, samplers;
    struct glm_object_table lists, list_names; /* display lists (marshal_custom.c), names from glGenLists */
    void *queue; /* id<MTLCommandQueue>: one per share group keeps cross-context order */
    volatile uint64_t program_generation; /* bumped when program/shader objects are freed */
    volatile uint64_t framebuffer_resource_generation; /* image definitions/storage shared by contexts */
    volatile uint64_t sampling_generation; /* shared texture and sampler metadata/storage */
    void *app_objects;  /* marshal_custom.c: shaders and programs as the application's thread sees them */
};

/* Ordered shared-context image changes must refresh an already open pass.
   This does not replace the GL application's synchronization of mutations. */
static inline void glm_framebuffer_resource_changed(struct glm_share_group *share)
{
    __atomic_add_fetch(&share->framebuffer_resource_generation, 1, __ATOMIC_RELEASE);
}

static inline void glm_sampling_resource_changed(struct glm_share_group *share)
{
    __atomic_add_fetch(&share->sampling_generation, 1, __ATOMIC_RELEASE);
}

enum glm_profile { GLM_PROFILE_LEGACY, GLM_PROFILE_CORE };

struct glm_begin_end;

/* GL_SELECT / GL_FEEDBACK render modes (render_modes.c). */
enum { GLM_MAX_NAME_STACK = 128 };
struct glm_render_modes {
    GLenum mode;                   /* GL_RENDER, GL_SELECT, GL_FEEDBACK */
    GLuint *select_buffer;
    GLsizei select_size, select_used;
    GLint hits;
    bool hit, select_overflow;
    float hit_min, hit_max;        /* window depths of the pending hit */
    GLuint names[GLM_MAX_NAME_STACK];
    int name_depth;
    GLfloat *feedback_buffer;
    GLsizei feedback_size, feedback_used;
    GLenum feedback_type;
    bool feedback_overflow;
    float eye_planes[GLM_MAX_CLIP_PLANES][4]; /* this draw's enabled user clip planes */
    int eye_plane_count;
};
struct glm_backend_context;

struct glm_context {
    /* First: the generated marshal layer (marshal_support.h) reads it
       without this header. NULL: calls execute directly. */
    struct glm_thread *thread;
    /* Second: bumped by the marshal layer for every call that can change
       draw state other than uniforms, current attributes and matrices (the
       backend's fast path compares it). */
    volatile uint64_t state_serial;
    /* Third: the display list being compiled (marshal_custom.c), NULL when none. */
    struct glm_list_build *listing;
    struct glm_shadow *shadow; /* marshal_custom.c: binding state on the application's side */
    CGLContextObj cgl;
    enum glm_profile profile;
    struct glm_share_group *share;
    struct glm_state state;
    GLenum error;

    struct glm_object_table framebuffers, vertex_arrays, queries, fences;
    struct glm_framebuffer default_framebuffer; /* name 0 */
    GLuint draw_framebuffer, read_framebuffer, renderbuffer_binding;
    GLuint array_buffer, element_buffer_legacy, pixel_pack_buffer, pixel_unpack_buffer, uniform_buffer,
           copy_read_buffer, copy_write_buffer, texture_buffer, transform_feedback_buffer, draw_indirect_buffer;
    struct glm_vertex_array default_vao, *vao;
    GLuint current_program;
    GLuint bound_pipeline;                  /* glBindProgramPipeline (pipelines.c) */
    GLuint list_base;                       /* glListBase */
    GLenum apple_element_type;              /* glElementPointerAPPLE (application thread) */
    const void *apple_element_pointer;
    bool apple_element_enabled;             /* GL_ELEMENT_ARRAY_APPLE (application thread) */
    bool conditional_render_active, conditional_discard; /* glBeginConditionalRender: skip draws and clears */
    GLenum clamp_read_color;
    float point_size_min, point_size_max, point_fade_threshold, point_distance[3];
    GLenum point_sprite_origin;
    struct glm_object_table pipelines;
    struct glm_program *program_cache; /* glm_current_program's lookup, valid for: */
    GLuint program_cache_name;
    uint64_t program_cache_generation;
    struct glm_indexed_buffer { GLuint buffer; GLintptr offset; GLsizeiptr size; } uniform_bindings[80],
        feedback_bindings[4];
    /* ARB programs: bindings, default objects (name 0), env parameters. */
    struct glm_query *active_queries[4]; /* by enum glm_query_class, stream 0 */
    struct glm_query *indexed_queries[4][3]; /* streams 1 through 3 */
    /* Transform feedback (feedback.c): the bound object (0: the default),
       and the capture in progress. */
    struct glm_object_table feedback_objects;
    GLuint feedback_object;
    struct glm_feedback {
        bool active, paused;
        GLenum mode;              /* GL_POINTS, GL_LINES or GL_TRIANGLES */
        uint32_t vertices;        /* written in this capture */
        uint32_t capacity;        /* vertices that fit in the smallest buffer */
        uint32_t stream_vertices[4];
    } xfb;
    struct glm_feedback feedback_default_capture; /* saved while object 0 is unbound */
    uint32_t xfb_last_vertices;   /* of the default object's last capture (glDrawTransformFeedback) */
    uint32_t xfb_last_stream_vertices[4]; /* completed per-stream counts of object 0 */
    struct glm_indexed_buffer feedback_default_bindings[4]; /* object 0's, while another is bound */
    bool presents;
    bool polygon_split;     /* vertex.c: drawing one face group of a split polygon draw */
    const uint8_t *edge_flags;     /* vertex.c: the vertices' edge flags while submitting, NULL: all set */
    /* vertex.c: while submitting a glBegin/glEnd whose materials changed
       between vertices, GLM_VERTEX_MATERIAL_FLOAT4S per vertex (attribute 6's
       x holds each vertex's index); NULL otherwise. */
    const float *vertex_materials;
    uint8_t pixel_rect; /* metal_backend.m: drawing a pixel rectangle (1 image, 2 bitmap; ffgen.c) */
    uint32_t vertex_material_count;
    struct glm_render_modes render_modes;
    struct glm_eval_map eval_maps[2][GLM_EVAL_MAPS];
    struct glm_eval_map attrib_maps[2][GLM_MAX_ATTRIBS]; /* APPLE_vertex_program_evaluators */
    struct glm_imaging_objects *imaging; /* imaging.c: colour tables, filters, histogram, minmax */
    /* Pixel data skips glPixelTransfer and the imaging operations
       (glm_pixels_raw_begin); glReadPixels applies them (read_transfer). */
    bool pixels_raw, read_transfer;
    const uint8_t *stipple_resets; /* vertex.c: per line, whether the stipple counter restarts (outlines) */
    bool user_clip_preprocessed; /* linear FF user planes were already clipped on the CPU */
    bool wide_line_expansion; /* vertex.c: fog.yz carry a fixed-function clip-space offset */
    struct glm_matrix_stack invalid_stack; /* texture matrix ops on units past GL_MAX_TEXTURE_COORDS land here */          /* has had a window surface (cgl.m) */
    /* Default framebuffer depth/stencil bits of the pixel format (cgl.m). */
    uint8_t default_depth_bits, default_stencil_bits;
    bool default_bits_known;
    bool default_single_buffered; /* the pixel format has only a front buffer */
    /* GL_AUX0.. colour buffers of the default framebuffer (legacy pixel
       formats offer up to GLM_MAX_AUX_BUFFERS, as Apple's do). */
    uint8_t default_aux_buffers;
    uint8_t default_accum_bits;     /* per component: 32 (float) or 0 */
    bool default_aux_depth_stencil; /* each aux buffer has its own depth/stencil */
    /* Proxy texture targets (textures.m): what the last glTexImage on each
       would have defined. */
    struct glm_texture_level proxy_levels[10][GLM_MAX_LEVELS];
    GLenum hints[11];       /* glHint, by hint_index (context.c); 0 = GL_DONT_CARE */
    /* Capabilities Apple accepts that GLMetal keeps as state only (glEnable). */
    struct { GLenum cap; bool enabled; } extra_caps[48];
    int extra_cap_count;
    GLuint arb_bound[2];
    struct glm_arb_program *arb_default[2];
    float arb_env[2][256][4];
    char *arb_error_string;
    GLint arb_error_position;

    struct glm_begin_end *immediate; /* non-NULL between glBegin and glEnd */
    struct glm_backend_context *backend;

    /* attribute stacks */
    void *attrib_stack[16];
    GLbitfield attrib_masks[16];
    int attrib_depth;

    struct glm_texture *default_textures[GLM_TEX_SLOTS];

    /* default framebuffer storage (drawable size or pbuffer) */
    GLsizei drawable_width, drawable_height;
};

GLM_HIDDEN struct glm_context *glm_current(void);
GLM_HIDDEN void glm_set_current(struct glm_context *ctx);
GLM_HIDDEN void glm_error(struct glm_context *ctx, GLenum error);
GLM_HIDDEN struct glm_context *glm_context_create(enum glm_profile profile, struct glm_context *share);
GLM_HIDDEN void glm_context_destroy(struct glm_context *ctx);
GLM_HIDDEN void glm_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
GLM_HIDDEN void glm_unimplemented(const char *name);
/* cgl.m: shows front-buffer drawing on glFlush/glFinish. */
GLM_HIDDEN void glm_cgl_present_front(struct glm_context *ctx);
/* render_modes.c: a draw (vertex.c submit) or a raster operation (`token`
   GL_BITMAP_TOKEN..., 0 for glRasterPos) in GL_SELECT/GL_FEEDBACK mode. */
GLM_HIDDEN void glm_render_mode_submit(struct glm_context *ctx, GLenum mode, const float *vertices, uint32_t vertex_count,
                                       const uint32_t *indices, uint32_t index_count, uint32_t instance,
                                       uint32_t vertex_id_base);
GLM_HIDDEN void glm_render_mode_raster(struct glm_context *ctx, GLenum token);
/* metal_backend.m: an accumulation buffer operation over the scissored
   window (GL_ACCUM ... GL_ADD, or 0 to clear it to `clear`). */
GLM_HIDDEN void glm_backend_accum(struct glm_context *ctx, GLenum op, float value, const float clear[4]);

/* Every entry point starts with this: the current context, or return. */
#define GLM_CONTEXT(ctx, ...)                                                  \
    struct glm_context *ctx = glm_current();                                   \
    if (!ctx) return __VA_ARGS__

/* ---- object tables ------------------------------------------------------- */

GLM_HIDDEN void *glm_table_get(struct glm_object_table *table, GLuint name);
GLM_HIDDEN void glm_table_set(struct glm_object_table *table, GLuint name, void *item);
GLM_HIDDEN GLuint glm_table_reserve(struct glm_object_table *table);

GLM_HIDDEN struct glm_buffer *glm_buffer_get(struct glm_context *ctx, GLuint name);
GLM_HIDDEN struct glm_texture *glm_texture_get(struct glm_context *ctx, GLuint name);
GLM_HIDDEN void glm_texture_destroy(struct glm_texture *texture);
GLM_HIDDEN struct glm_texture *glm_texture_bound(struct glm_context *ctx, GLenum target);
GLM_HIDDEN struct glm_renderbuffer *glm_renderbuffer_get(struct glm_context *ctx, GLuint name);
GLM_HIDDEN struct glm_framebuffer *glm_framebuffer_get(struct glm_context *ctx, GLuint name);
GLM_HIDDEN int glm_texture_slot(GLenum target);
GLM_HIDDEN int glm_sampler_binding(uint32_t sampler_type);

/* ---- matrices ------------------------------------------------------------ */

GLM_HIDDEN void glm_mat4_identity(float *m);
GLM_HIDDEN void glm_mat4_multiply(float *out, const float *a, const float *b); /* out = a * b */
GLM_HIDDEN bool glm_mat4_invert(float *out, const float *m);
GLM_HIDDEN float *glm_matrix_top(struct glm_context *ctx);
GLM_HIDDEN struct glm_matrix_stack *glm_matrix_stack(struct glm_context *ctx);

/* ---- drawing ------------------------------------------------------------- */

/* A draw after the front end has resolved arrays: float4 per enabled
   attribute, interleaved, and an index list the backend can draw as-is. */
struct glm_draw {
    GLenum mode;                  /* GL mode before conversion */
    bool capture;                 /* transform feedback: xfb_count vertices written from xfb_first */
    uint32_t xfb_first, xfb_count;
    uint32_t metal_primitive;     /* MTLPrimitiveType */
    const float *vertices;        /* vertex_count * attrib_count float4 */
    uint32_t vertex_count;
    uint32_t attrib_mask;         /* which attributes are present */
    uint32_t attrib_count;
    const uint32_t *indices;      /* NULL: sequential */
    const uint32_t *raster_indices; /* optional permutation of original feedback records */
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t base_instance;       /* gl_InstanceID of the first instance */
    uint32_t vertex_id_base;      /* original ID offset for affine compact streams */
    const uint32_t *original_vertex_ids; /* deindexed user IDs, vertex_count entries */
    /* Line stipple: (factor, pattern) then per line primitive its first
       vertex's window x, y and stipple counter (vertex.c); NULL for none. */
    const float *stipple;
    uint32_t stipple_count;
};

GLM_HIDDEN bool glm_feedback_draw_allowed(struct glm_context *ctx, GLenum mode);
GLM_HIDDEN bool glm_feedback_capturing(struct glm_context *ctx);
GLM_HIDDEN uint32_t glm_feedback_reserve(struct glm_context *ctx, uint32_t vertices, uint32_t *first);
GLM_HIDDEN void glm_draw_arrays_internal(struct glm_context *ctx, GLenum mode, GLint first, GLsizei count,
                                         GLsizei instances);
GLM_HIDDEN void glm_draw_elements_internal(struct glm_context *ctx, GLenum mode, GLsizei count, GLenum type,
                                           const void *indices, GLsizei instances, GLint base_vertex);

/* ---- backend (metal_backend.m) ------------------------------------------- */

GLM_HIDDEN bool glm_backend_initialize(void);
/* The current vertex stage's outputs for `draw`'s vertices: clip position,
   front colour and first texture coordinate, as Metal sees the position
   (y inverted, z in [0, w]); 12 floats each into `out`. Waits for the GPU. */
GLM_HIDDEN bool glm_backend_vertex_outputs(struct glm_context *ctx, const struct glm_draw *draw, float *out);
GLM_HIDDEN void *glm_backend_device(void);
GLM_HIDDEN void *glm_backend_new_queue(void);
GLM_HIDDEN struct glm_backend_context *glm_backend_context_create(struct glm_context *ctx);
GLM_HIDDEN void glm_backend_context_destroy(struct glm_backend_context *backend);
GLM_HIDDEN bool glm_backend_clip_stream(struct glm_context *ctx, GLenum mode);
GLM_HIDDEN void glm_backend_clear(struct glm_context *ctx, GLbitfield mask);
GLM_HIDDEN uint64_t glm_now_ns(void);
GLM_HIDDEN void glm_note_stall(const char *what, uint64_t started_ns);
/* Array state of `vao` changed (memoized GPU vertex layouts compare the serial). */
extern GLM_HIDDEN uint64_t glm_vao_generation;
static inline void glm_vao_changed(struct glm_vertex_array *vao)
{
    vao->serial = __atomic_add_fetch(&glm_vao_generation, 1, __ATOMIC_RELAXED);
}
/* A draw compiled into the display list being built: its vertices fetched
   now, replayed through the immediate path (vertex.c). */
GLM_HIDDEN void glm_list_capture_draw(struct glm_context *ctx, GLenum mode, GLint first, GLsizei count, GLenum type,
                                      const void *indices, GLsizei instances, GLint base_vertex);
GLM_HIDDEN void glm_backend_draw_image(struct glm_context *ctx, const float *rgba, GLsizei width, GLsizei height, float x0,
                                       float y0, float x1, float y1, float depth, bool bitmap, const float *color);
GLM_HIDDEN unsigned glm_backend_draw_samples(struct glm_context *ctx); /* of the draw framebuffer */
/* Indexed viewport, scissor, depth range and sample mask state (glGet*i_v); 0 if `pname` is none of them. */
GLM_HIDDEN int glm_indexed_state_query(struct glm_context *ctx, GLenum pname, GLuint index, double *v);
GLM_HIDDEN void glm_backend_clear_buffer(struct glm_context *ctx, int drawbuffer, int kind, const void *values);
GLM_HIDDEN void glm_backend_draw(struct glm_context *ctx, const struct glm_draw *draw);
/* Draws straight from the bound arrays and element buffer with GPU vertex
   fetch. False when the draw needs the CPU path (glm_backend_draw). */
GLM_HIDDEN bool glm_backend_draw_gpu(struct glm_context *ctx, GLenum mode, GLint first, GLsizei count, GLenum index_type,
                                     const void *indices, GLsizei instances, GLint base_vertex);
/* Makes [offset, offset + size) of `buffer` safe to write from the CPU:
   waits for, or renames the buffer away from, GPU reads of that range.
   `discard` allows dropping the old contents. */
GLM_HIDDEN void glm_backend_buffer_prepare_write(struct glm_context *ctx, struct glm_buffer *buffer, GLintptr offset,
                                                 GLsizeiptr size, bool discard, bool wait);
/* vertex.c: GL primitives as Metal lists; returns the index count. */
struct glm_index_list {
    uint32_t *data;
    uint32_t count, capacity;
};
GLM_HIDDEN uint32_t glm_triangulate(GLenum mode, const uint32_t *seq, uint32_t n, struct glm_index_list *out,
                                    uint32_t *metal_primitive, bool geometry);
GLM_HIDDEN void glm_backend_flush(struct glm_context *ctx, bool wait);
/* Bumped (process-wide) whenever buffer objects or contexts are freed, so
   caches holding their pointers can tell they may be stale. */
extern volatile uint64_t glm_object_generation;
/* After encoding an upload: contexts that share objects but never present
   (loader threads) commit at once, or the upload would wait in a command
   buffer other contexts' draws do not order after. */
GLM_HIDDEN void glm_backend_upload_encoded(struct glm_context *ctx);
/* Copies upload bytes into transient shared storage retained until command
   completion. The unretained buffer's byte offset is aligned to 256 bytes. */
GLM_HIDDEN void *glm_backend_upload_staging(struct glm_context *ctx, size_t length, const void *data, size_t *offset);
GLM_HIDDEN void glm_backend_read_pixels(struct glm_context *ctx, GLint x, GLint y, GLsizei width, GLsizei height,
                                        GLenum format, GLenum type, void *pixels);
GLM_HIDDEN void glm_backend_framebuffer_changed(struct glm_context *ctx);
GLM_HIDDEN void glm_backend_release(void *object);

GLM_HIDDEN void glm_backend_buffer_data(struct glm_context *ctx, struct glm_buffer *buffer, const void *data);
GLM_HIDDEN void glm_backend_buffer_sub_data(struct glm_context *ctx, struct glm_buffer *buffer, GLintptr offset,
                                            GLsizeiptr size, const void *data);
GLM_HIDDEN void *glm_backend_buffer_contents(struct glm_context *ctx, struct glm_buffer *buffer, bool for_write);
GLM_HIDDEN void *glm_backend_new_buffer_storage(size_t size, const void *data);
/* Storage of `buffer` is being replaced or deleted: recycled when the GPU is done with it. */
GLM_HIDDEN void glm_backend_buffer_retire(struct glm_context *ctx, struct glm_buffer *buffer);
GLM_HIDDEN void *glm_backend_storage_contents(void *storage);
GLM_HIDDEN void glm_backend_buffer_adopt(struct glm_context *ctx, struct glm_buffer *buffer, void *storage);
GLM_HIDDEN bool glm_buffer_usage_valid(GLenum usage);
GLM_HIDDEN void glm_buffer_data_with_storage(struct glm_context *ctx, GLenum target, GLsizeiptr size, GLenum usage,
                                             void *storage);
GLM_HIDDEN void glm_texture_query_override(struct glm_context *ctx, GLenum target, GLuint name);
GLM_HIDDEN void glm_texture_query_override_end(void);
GLM_HIDDEN GLsync glm_sync_new_pending(struct glm_context *ctx);
GLM_HIDDEN void glm_sync_arm(GLsync sync);
GLM_HIDDEN bool glm_sync_is_pending(GLsync sync);

GLM_HIDDEN void glm_backend_renderbuffer_storage(struct glm_context *ctx, struct glm_renderbuffer *rb);
GLM_HIDDEN void glm_backend_texture_image(struct glm_context *ctx, struct glm_texture *texture, int face, GLint level,
                                          GLint x, GLint y, GLint z, GLsizei width, GLsizei height, GLsizei depth,
                                          const void *rgba_or_native, size_t row_bytes, size_t image_bytes);
GLM_HIDDEN bool glm_backend_texture_ensure_storage(struct glm_context *ctx, struct glm_texture *texture);
GLM_HIDDEN void glm_backend_generate_mipmaps(struct glm_context *ctx, struct glm_texture *texture);
GLM_HIDDEN void glm_backend_copy_framebuffer_to_texture(struct glm_context *ctx, struct glm_texture *texture, int face,
                                                        GLint level, GLint dst_x, GLint dst_y, GLint dst_z, GLint src_x,
                                                        GLint src_y, GLsizei width, GLsizei height);
GLM_HIDDEN void glm_backend_get_tex_image(struct glm_context *ctx, struct glm_texture *t, int face, GLint level,
                                          GLenum format, GLenum type, void *pixels, bool raw);
GLM_HIDDEN void glm_backend_blit_framebuffer(struct glm_context *ctx, GLint src[4], GLint dst[4], GLbitfield mask,
                                             GLenum filter);

/* ---- formats (formats.c) ------------------------------------------------- */

struct glm_format_info {
    uint32_t metal_format;   /* MTLPixelFormat used for storage */
    uint32_t upload_format;  /* how glm_convert_upload hands data to the backend */
    int bytes_per_pixel;     /* of the Metal format (block size for compressed) */
    bool compressed, depth, stencil, integer, srgb;
    /* Swizzle (0..3 = r g b a, 4 = zero, 5 = one) applied when sampling, for
       luminance/alpha/intensity formats stored as R or RG. */
    uint8_t swizzle[4];
};

GLM_HIDDEN bool glm_format_lookup(GLenum internal_format, struct glm_format_info *out);
/* glCheckFramebufferStatus of a framebuffer object, as Apple's implementation judges it. */
GLM_HIDDEN GLenum glm_framebuffer_check(struct glm_context *ctx, struct glm_framebuffer *fb);
GLM_HIDDEN GLenum glm_framebuffer_status_of(struct glm_context *ctx, GLuint name);
enum { GLM_CALL_DRAW_BUFFER, GLM_CALL_DRAW_BUFFERS, GLM_CALL_READ_BUFFER };
GLM_HIDDEN GLenum glm_color_buffer_error(const struct glm_context *ctx, bool fbo, GLenum buffer, int call);
GLM_HIDDEN GLenum glm_draw_buffers_error(const struct glm_context *ctx, bool fbo, GLsizei n, const GLenum *buffers);
GLM_HIDDEN void glm_detach_deleted(struct glm_context *ctx, GLenum type, GLuint name);
/* What Apple reports for a texture of `internal_format` (glm_apple_gets.h),
   or NULL if its glTexImage2D does not take it in this profile. */
struct glm_apple_texture;
GLM_HIDDEN const struct glm_apple_texture *glm_apple_texture_format(const struct glm_context *ctx, GLenum internal_format);
GLM_HIDDEN bool glm_texture_format_allowed(GLenum internal_format, bool core);
GLM_HIDDEN bool glm_texture_pname_in_profile(const struct glm_context *ctx, GLenum pname);
/* Pixel transfer argument errors as Apple reports them (0 if none). */
GLM_HIDDEN GLenum glm_format_type_error(GLenum format, GLenum type, bool core);
GLM_HIDDEN void glm_color_material_sync(struct glm_context *ctx);
/* The stencil state back faces are drawn with. */
static inline const struct glm_stencil_face *glm_stencil_back(const struct glm_state *s)
{
    return &s->stencil[s->stencil_two_side ? 2 : 1];
}
GLM_HIDDEN uint32_t glm_transfer_stencil(const struct glm_state *s, int64_t index);
GLM_HIDDEN GLenum glm_read_error(GLenum internal_format, bool depth, bool stencil, GLenum format, GLenum type, bool core,
                                 bool read_pixels);
/* The read framebuffer's colour read buffer format (0 for none) and whether it has depth and stencil. */
GLM_HIDDEN GLenum glm_read_buffer_format(struct glm_context *ctx, bool *depth, bool *stencil);
GLM_HIDDEN bool glm_unpack_depth_stencil(struct glm_context *ctx, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                         const void *pixels, float *depth, uint32_t *stencil);
GLM_HIDDEN void glm_backend_draw_depth_stencil(struct glm_context *ctx, const float *depth, const uint32_t *stencil,
                                               GLsizei width, GLsizei height, float x0, float y0, float x1, float y1);
GLM_HIDDEN int glm_client_array_slot(struct glm_context *ctx, GLenum array);
GLM_HIDDEN int glm_client_attrib_depth(void);
GLM_HIDDEN GLenum glm_tex_image_error(GLenum internal_format, GLenum format, GLenum type, bool core);
struct glm_compile_result;
GLM_HIDDEN void glm_backend_prewarm_program(const struct glm_compile_result *r);
GLM_HIDDEN void glm_backend_prepare_arb_fragment(struct glm_program *p);
GLM_HIDDEN GLenum glm_base_internal_format(GLenum internal_format, GLenum format);
/* Converts client pixels (format/type, with unpack state applied) into the
   storage layout of `info`. Returns malloc'd data or NULL if unsupported. */
GLM_HIDDEN void *glm_convert_upload(struct glm_context *ctx, const struct glm_format_info *info, GLsizei width,
                                    GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels,
                                    size_t *row_bytes, size_t *image_bytes);
/* Converts RGBA float pixels into the client's format/type with pack state. */
GLM_HIDDEN bool glm_convert_pack(struct glm_context *ctx, const float *rgba, GLsizei width, GLsizei height,
                                 GLenum format, GLenum type, void *out);

#endif
