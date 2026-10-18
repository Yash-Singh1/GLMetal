#ifndef GLM_SHADER_DEPTH_H
#define GLM_SHADER_DEPTH_H
#ifdef __cplusplus
extern "C" {
#endif
/* Add a fragment depth output derived from rasterized position. Returns NULL
   for an existing depth output or an unsupported generated function shape. */
/* Select sample interpolation for generated floating user inputs, preserving
   flat/centroid inputs and raster position. NULL for unsupported entry shapes. */
char *glm_sample_shading_msl(const char *source, const char *entry);
char *glm_depth_clamp_msl(const char *source, const char *entry);
/* Discard fragments outside the polygon stipple (pattern rows in buffer 26). */
char *glm_polygon_stipple_msl(const char *source, const char *entry);
/* Store each vertex's clip position, front colour and first texture
   coordinate at its vertex ID (buffer 27, three float4s each). */
char *glm_position_capture_msl(const char *source, const char *entry);
/* Discard fragments of line primitives off the stipple pattern (buffer 26). */
char *glm_line_stipple_msl(const char *source, const char *entry);
/* Mask disabled raster clip distances after the vertex body has performed its
   capture writes. Returns NULL when no output needs masking. */
char *glm_clip_mask_msl(const char *source, const char *entry, unsigned enabled);
#ifdef __cplusplus
}
#endif
#endif
