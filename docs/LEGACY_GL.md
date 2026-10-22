# Legacy OpenGL features

What GLMetal implements of the OpenGL 2.1 compatibility features that have
no Metal counterpart, how Apple's OpenGL behaves where the specifications
leave room, and how each feature is tested against Apple's implementation.

## References

| Topic | Specification |
|---|---|
| Draw buffers, AUX*i*, FRONT_AND_BACK | OpenGL 2.1 specification, 4.2.1 |
| Accumulation buffer | OpenGL 2.1, 4.2.4 (`glAccum`), 4.2.3 (`glClearAccum`) |
| Pixel formats (aux, accumulation, stereo) | CGL Reference (`kCGLPFAAuxBuffers`, `kCGLPFAAuxDepthStencil`, `kCGLPFAAccumSize`); AGL Reference |
| Line and polygon stipple | OpenGL 2.1, 3.4.2 and 3.5.2 |
| Edge flags | OpenGL 2.1, 2.6.2 (`glEdgeFlag`), 2.8 (edge flag arrays), 3.5.4 |
| Selection and feedback | OpenGL 2.1, 5.2 and 5.3 |
| Display lists | OpenGL 2.1, 5.4 (what is compiled, and when data is read) |
| Evaluators | OpenGL 2.1, 5.1 |
| Materials between `glBegin`/`glEnd` | OpenGL 2.1, 2.14.3 |
| Pixel rectangles and bitmaps | OpenGL 2.1, 3.6 to 3.7 and 4.3.3 (`glCopyPixels`) |
| ARB assembly programs | `ARB_vertex_program`, `ARB_fragment_program` |
| Imaging subset | OpenGL 2.1, 3.6.3 and 3.6.5; `SGI_color_table`, `EXT_convolution`, `HP_convolution_border_modes`, `SGI_color_matrix`, `EXT_histogram` |
| Attribute evaluators | `APPLE_vertex_program_evaluators` |
| Vertex blending, per-vertex point sizes | `ARB_vertex_blend`; `APPLE_vertex_point_size`; OpenGL 2.1, 3.4 (point size, attenuation) |
| Bindable uniforms | `EXT_bindable_uniform` |
| Element arrays | `APPLE_element_array` |

The OpenGL specifications are at <https://registry.khronos.org/OpenGL/>.
Mesa's implementation (`src/mesa/main/`, `src/mesa/vbo/`) is a useful
second reading. Where Apple differs from both, GLMetal follows Apple.

## Testing

Each feature has tests that run the same calls against Apple's OpenGL and
GLMetal and compare the results:

- **glcompare cases** (`tests/glcompare/cases_*.c`) compare rendered pixels.
  `python3 tests/glcompare/compare.py --filter NAME`; add
  `GLMETAL_THREAD_ALL=1` to run GLMetal on its command stream.
- **Probes** (`tests/probes/`) print API answers: errors, queries, feedback
  and selection buffers. `make test-*` runs each probe on Apple's OpenGL and
  on GLMetal, direct and threaded, then diffs the outputs.
  `tests/probes/numdiff.py` allows 0.0015 for floating-point values.
- **Reference sweep** (`make check-reference`) compares every `glGet`
  pname and capability with `data/apple-get-reference.txt`.
- **piglit** runs headless through the shim and compares results with
  Apple's run of the same tests.

| Feature | Tests |
|---|---|
| Aux buffers, pixel formats | `make test-aux-buffers` (`tests/probes/aux_buffers.m`) |
| Default framebuffer: front/back/aux, swap, accumulation | `make test-default-framebuffer` |
| Draw-buffer broadcast | glcompare `fragcolor_broadcast` |
| Line stipple | glcompare `line_stipple`, `line_stipple_outlines`, `line_stipple_piglit` |
| Polygon stipple | glcompare `polygon_stipple` |
| Selection, feedback | `make test-render-modes` |
| Evaluators | `make test-evaluators` |
| Display-list pixel data | glcompare `dlist_pixels` |
| Per-vertex materials | glcompare `vertex_materials` |
| Pixel rectangle fragments | glcompare `pixel_fragments` |
| ARB programs | `make test-arb-queries`; glcompare `arb_*` |
| Colour index, texture residency, NV combiners, `glBindBufferOffsetEXT` | `make test-legacy-misc` |
| Imaging subset | `make test-imaging` (`tests/probes/imaging.c`, quirks in `imaging_quirks.txt`) |
| Attribute evaluators | `make test-attrib-evaluators` |
| Vertex blending, per-vertex point sizes, attenuation | `make test-vertex-blend` |
| Bindable uniforms | `make test-bindable-uniform` |
| Element arrays | `make test-element-array` |
| GLSL built-in functions, variables and qualifiers | `make test-glsl-builtins`; glcompare `glsl_legacy_builtins` |
| `CGLTexImageIOSurface2D` | `make test-iosurface` |
| CGL pbuffers | `make test-pbuffer` |

Where Apple's OpenGL contradicts an extension it advertises (mostly in the
imaging subset, which is half broken on Apple silicon), GLMetal follows the
extension. Each such line is listed in a `*_quirks.txt` file next to the
probe, with Apple's output, GLMetal's output and the reason;
`tests/probes/quirkdiff.py` accepts exactly those differences and fails if
Apple stops showing one.

## Pixel formats and the default framebuffer

**Apple's rules** (from `tests/probes/aux_buffers.m`):

- Aux buffers:
  - Legacy profile only. `kCGLPFAAuxBuffers` 1 or 2 is accepted.
  - More than 2 gives `kCGLBadPixelFormat`.
  - A negative count gives `kCGLBadValue`.
  - Any aux buffers in a core profile give `kCGLBadAttribute`.
- `kCGLPFAAuxDepthStencil` is accepted only together with
  `kCGLPFANoRecovery`.
- Accumulation: any legacy `kCGLPFAAccumSize` gives 128 bits (32-bit
  float channels). A core profile gives `kCGLBadAttribute`.
- AGL sums its four accumulation sizes into `kCGLPFAAccumSize`.

**Default framebuffer.** It holds front, back, up to two aux colour textures
and an accumulation texture (`metal_backend.m`, `default_front`,
`default_aux`, `default_accum`):

- With `kCGLPFAAuxDepthStencil`, each aux buffer has its own depth and
  stencil.
- Swapping exchanges the front and back textures, as Apple does.
- Drawing to `GL_FRONT` presents at `glFlush`/`glFinish`.
- `GL_FRONT_AND_BACK` and `GL_AUXi` are valid draw and read buffers.
- `gl_FragData[n]` with `GL_FRONT_AND_BACK` behaves like the pair (FRONT,
  BACK).
- `gl_FragColor` and ARB `result.color` reach every draw buffer. Metal
  needs this broadcast written explicitly as an output map.

**Accumulation** (`glAccum`, `glClearAccum`, `GL_ACCUM_BUFFER_BIT`) runs as a
compute kernel over the accumulation texture:

- `glClearAccum` values are clamped to [-1, 1].
- An unknown `glAccum` op raises no error (Apple).
- With a framebuffer object bound, or without an accumulation buffer,
  `glAccum` is `GL_INVALID_OPERATION`.
- Clearing the accumulation buffer while an FBO is bound does nothing.

## Stipples and edge flags

**Polygon stipple** is a fragment-function variant
(`glm_polygon_stipple_msl`). It tests the 32x32 pattern against window
coordinates and applies to filled polygons only. `glPolygonStipple` and
`glGetPolygonStipple` honour the pack and unpack state, pixel buffer objects
and `GL_UNPACK_LSB_FIRST`.

**Line stipple** needs each fragment's position along its line. For every
line, the CPU or the GPU vertex capture finds the window-space start and
major axis. The CPU builds a segment table and binds it at fragment buffer
26; the fragment variant computes `base + floor(|pos - start| + 0.5)`.

**Apple's stipple behaviour:**

- The stipple counter continues across the segments of strips and loops.
- It restarts for each polygon outline.
- An outline's vertex order starts at the last vertex (GL_POLYGON: at the
  first). For example, a triangle (a, b, c) outlines as (c, a, b).

**Edge flags** (`glEdgeFlag`, `glEdgeFlagPointer`) hide polygon edges in
`GL_LINE` and `GL_POINT` polygon modes. Polygons in those modes are split
on the CPU into Apple's edge order.

## Selection and feedback

`render_modes.c` implements both modes in software:

- Primitives are clipped in homogeneous coordinates, with user clip planes
  applied in eye space.
- Culling, polygon modes and edge flags apply as for rendering.
- Positions and colours come from the GPU's vertex stage, so programs and
  lighting count. `glm_backend_vertex_outputs` captures 12 floats per
  vertex.

**Apple's details:**

- The name stack holds 128 names.
- Feedback vertex order follows the outline order above.
- Feedback colours are the lit per-vertex colours, even with flat shading.

Only the top 24 bits of hit-record depths are compared: Apple's low byte
comes from a scaling that has not been reproduced.

## Display lists

Most commands are recorded by the generated marshal layer
(`tools/gen_marshal.py`). Two kinds need more:

- **Draws** keep their vertices: arrays are fetched when the list is
  compiled.
- **Commands that read client pixels** unpack at compile time (GL 2.1 5.4):
  `glBitmap`, `glDrawPixels`, `glPolygonStipple`, `glTexImage*D` and
  `glTexSubImage*D`.
  - At compile time, GLMetal copies the bytes the unpack state reaches,
    together with that state. The source can be client memory or the bound
    pixel unpack buffer.
  - Replay restores the recorded state around the call
    (`marshal_custom.c`, `list_pixels`).
  - Later changes to client memory, buffer contents or pixel-store state do
    not affect the list.

`glMap1*`/`glMap2*` copy their control points when the list is compiled
(`glm_map_points_bytes`).

## Materials inside glBegin/glEnd

GL lights each vertex with the materials current when that vertex was
specified. When `glMaterial` runs between vertices:

- Immediate mode records a material snapshot per vertex (9 float4s).
- Each vertex carries its index in attribute 6.
- The fixed-function vertex function reads the snapshots from a buffer
  (`GLM_SLOT_FF_MATERIALS`).
- Flat shading copies the provoking vertex's index along with its colours.
- Draws without material changes are unaffected.

## Evaluators

`evaluators.c` evaluates maps on the CPU (de Casteljau) and issues the
vertices through immediate mode.

**Apple's behaviour** (`tests/probes/evaluators.c`):

- `GL_MAX_EVAL_ORDER` is 10.
- `glMap` errors are checked in this order:
  1. Domain and order (`GL_INVALID_VALUE`).
  2. Target (`GL_INVALID_ENUM`).
  3. Strides (`GL_INVALID_VALUE`).
- No error when `GL_ACTIVE_TEXTURE` is not unit 0.
- Apple crashes on a NULL `points`; GLMetal raises `GL_INVALID_VALUE`.
- `glGetMapiv` rounds to the nearest integer.
- Default maps have order 1, domain [0, 1], and the initial current value
  as their only control point.
- Without an enabled vertex map, `glEvalCoord` does nothing.
- `VERTEX_4` takes precedence over `VERTEX_3`, and the highest enabled
  texture-coordinate map wins.
- Evaluation leaves the current colour, normal and texture coordinates
  unchanged.
- `glEvalMesh2(GL_FILL)` draws a triangle strip per row (`GL_LINE` and
  `GL_POINT` as in the specification).
- Automatic normals:
  - They are derivatives in the unit parameter square, normalized, so they
    don't depend on the domain or `GL_NORMALIZE`.
  - **Known difference:** for rational (`VERTEX_4`) patches Apple's normals
    follow neither the projected surface's nor the homogeneous
    derivatives. GLMetal uses the projected surface's, and the probe skips
    that case.

`GL_EVAL_BIT` saves the map enables, `GL_AUTO_NORMAL` and the grids.
`GL_ENABLE_BIT` saves the enables.

## Pixel rectangles

`glDrawPixels`, `glCopyPixels` and `glBitmap` produce fragments that pass
through the whole fragment pipeline (GL 2.1 3.6.4, 3.7).

**Fixed-function fragment stage.** The rectangle is a fixed-function draw
with a `pixel_rect` key (`ffgen.c`):

- The fragment function reads the image pixel whose zoomed square contains
  the fragment centre. Bitmaps discard unset bits.
- Fragments are textured with the raster texture coordinates and fogged by
  the raster distance.
- Alpha test, depth, stencil, blending and logic ops apply.

**With a GLSL or ARB fragment program bound,** that program runs on the
rectangle, with `gl_Color` taken from the image. Each image pixel is drawn as
an instanced quad whose colour is read in the vertex stage.

Rectangles are never culled, outlined, offset or polygon-stippled.

## IOSurfaces and pbuffers

**`CGLTexImageIOSurface2D`** (`textures.m`, `make test-iosurface`) makes
the bound rectangle texture an image held by an IOSurface plane: sampling
reads the surface, and uploads and drawing into the texture write it.
Apple's rules:

- Only `GL_TEXTURE_RECTANGLE_ARB`; other targets give `kCGLBadValue`.
  Texture 0 gives `kCGLBadState` and `GL_INVALID_OPERATION`.
- Each format takes its own type: BGRA with
  `GL_UNSIGNED_INT_8_8_8_8_REV` (RGBA8, RGB8 and the sRGB formats) or
  `GL_UNSIGNED_INT_2_10_10_10_REV`; RED and RG, RGBA and the integer
  formats with bytes, shorts, halves and floats; LUMINANCE, ALPHA,
  INTENSITY and LUMINANCE_ALPHA bytes; 4:2:2 pairs as
  `GL_RGB_RAW_422_APPLE`. The internal format reported is the sized one
  (`GL_RGBA` reads back as `GL_RGBA8`).
- The surface's pixel format and element size are not checked. The width
  must fit the plane's row pitch and the height its height.
- While a surface is attached, `glTexImage2D` gives `GL_INVALID_OPERATION`;
  another `CGLTexImageIOSurface2D` replaces the surface.
- An RGB format keeps the client's alpha bytes in the surface.

`glFinish` waits for everything committed before it, `glFlush` included,
so a surface (or another context) sees the drawing once it returns.

**Pbuffers** (`make test-pbuffer`): Apple's renderer on Apple silicon has
none, arm64 and x86_64 alike. `CGLCreatePBuffer` returns
`kCGLBadDrawable` and a NULL pbuffer for every target, format and size, so
programs fall back to framebuffer objects. The other calls answer for a
NULL pbuffer (`kCGLBadAddress`, or `kCGLBadContext` for a bad context), and
`CGLGetPBuffer` reports none.

**`CGLGetDeviceFromGLRenderer`** returns OpenCL's GPU device for the
accelerated renderer's IDs, as Apple does, and NULL for others (undefined
there).

## Small entry points

`legacy_misc.c` answers as Apple does (`make test-legacy-misc`).

- **Colour-index state:** `glIndex*`, `glClearIndex` and `glIndexMask` are
  kept in RGBA mode and queried back. `GL_CURRENT_BIT` and
  `GL_COLOR_BUFFER_BIT` save them.
- **`glPrioritizeTextures`:** clamps priorities to [0, 1] and skips names
  that were never bound.
- **`glAreTexturesResident`:** a texture is resident when it has a base
  image. When all are resident it returns `GL_TRUE` and leaves the array
  alone. A name that isn't a texture gives `GL_INVALID_VALUE`.
- **`glClearColorIiEXT`/`IuiEXT`:** store the integer clear colour without
  changing `GL_COLOR_CLEAR_VALUE`.
- **Exported but not advertised by Apple** (`glDepthBoundsEXT`, the NV
  register combiner calls and queries, ATI PN triangles, `glSamplePass`):
  accepted with no effect and no error, as Apple does. Queries write
  nothing; enabling `GL_REGISTER_COMBINERS_NV` is `GL_INVALID_ENUM`.
- **`glBindBufferOffsetEXT`:** binds a transform feedback buffer from the
  offset to its end. Offsets must be multiples of 4. Past the end, the
  reported size is negative, as on Apple.
- An empty `glBegin`/`glEnd` with no drawable raises
  `GL_INVALID_FRAMEBUFFER_OPERATION`, like array draws.
- **Indirect draws** (glcompare `draw_indirect`): the command's last word,
  reserved in GL 4.1, is a base instance on Apple. Instanced arrays start
  that many elements in. `glMultiDrawElementsIndirectAPPLE` and
  `glMultiDrawElementsIndirectBaseInstanceAPPLE` (not in the SDK headers;
  GL 4.3's `glMultiDrawElementsIndirect` arguments) draw `drawcount`
  commands `stride` bytes apart. Apple's single indirect draws also start
  `gl_InstanceID` at the base instance; GLMetal starts it at 0, as GL 4.2
  and Apple's multi-draws do.

## Imaging subset

`imaging.c` implements colour tables, convolution, the colour matrix,
histogram and minmax on the CPU. The pipeline order is GL 2.1 3.6.5:
pixel transfer scale/bias and maps, `GL_COLOR_TABLE`, convolution with its
post-convolution scale/bias, `GL_POST_CONVOLUTION_COLOR_TABLE`, the colour
matrix with its post-matrix scale/bias, `GL_POST_COLOR_MATRIX_COLOR_TABLE`,
histogram, minmax.

- **Paths:** `glDrawPixels`, `glCopyPixels` and `glReadPixels` (2D images),
  `glTexImage1D/2D/3D`, `glTexSubImage*` and `glCopyTex(Sub)Image*`. 1D
  filters apply to 1D textures only; 3D textures skip convolution;
  `glGetTexImage` applies nothing (as Apple). Textures take the processed
  image: the call re-enters the GL with float RGBA data under
  `glm_pixels_raw_begin`, which turns off pixel transfer and imaging and
  resets the unpack state.
- **Convolution:** REDUCE shrinks images (texture sizes included, also for
  `NULL` data and proxies); CONSTANT_BORDER and REPLICATE_BORDER keep the
  size. Components the filter's format lacks pass through. The 2D filter
  wins over the separable one.
- **Sinks:** histogram or minmax sink discards the image: nothing drawn,
  read or uploaded.
- **State:** tables, filters and counters are context objects
  (`glm_context.imaging`); enables and parameters are in the pixel
  attribute group (`GL_PIXEL_MODE_BIT`, enables also `GL_ENABLE_BIT`).
  `GL_PIXEL_MODE_BIT` now also saves the pixel transfer scale, bias and map
  state (as Apple). Table, filter, histogram and minmax queries return what
  Apple's do (base format for tables, 8-bit table and 32-bit counter
  sizes).
- **Read paths:** `glReadPixels` now applies pixel transfer scale, bias and
  maps; `glCopyPixels` and `glCopyTex(Sub)Image*` apply them once.
- **Display lists** compile `glColorTable`, `glColorSubTable`, the filter
  calls (pixel data copied at compile time; proxies run immediately) and
  the rest.

Apple's results that GLMetal does not follow (`imaging_quirks.txt`):

- `glReadPixels` writes nothing while an imaging operation is enabled.
- `glCopyPixels` skips imaging.
- Textures come out zero.
- One-row `glDrawPixels` images are convolved with the 1D filter.
- REPLICATE_BORDER behaves as IGNORE_BORDER.
- Filters sum the components they should pass through.
- The post-colour-matrix scale and bias are ignored.
- Pixel transfer scale/bias are applied after the colour table.
- Sinks draw anyway.
- Luminance histograms and minmax return every component.
- `glPushAttrib` saves no imaging state.
- Proxy histogram and colour-table parameter calls change the real ones.

## Attribute evaluators

`glMapVertexAttrib{1,2}{f,d}APPLE` store a map of 1 to 4 components per
generic attribute, beside the conventional maps.

- Errors and `glGetVertexAttrib*vARB` queries are Apple's.
- **Evaluation:** with an ARB vertex program or a GLSL program in use,
  `glEvalCoord`/`glEvalMesh` set the enabled attributes. Attribute 0's map
  provides the vertex, ahead of `GL_MAP*_VERTEX_*`; unused components are
  0, 0, 1. The current attributes are restored afterwards. Fixed function
  ignores attribute maps.
- `GL_EVAL_BIT` and `GL_ENABLE_BIT` save the enables.
- `glMapVertexAttrib*` compiles into display lists.
- Apple's maps of size 1-3 return garbage components; that is a documented
  quirk.
- Feedback mode clamps program colours to [0, 1] (`GL_CLAMP_VERTEX_COLOR`'s
  default), as Apple does.

## Vertex blending and point sizes

- **ARB_vertex_blend:** blends fixed-function vertices across
  `GL_MODELVIEW0_ARB` to `GL_MODELVIEW3_ARB`, each with its own matrix
  stack (`glMatrixMode`, `glGet`).
  - **Weights:** the active units' weights are scaled to sum to 1, as Apple
    does, so all-zero weights give NaN. With `GL_WEIGHT_SUM_UNITY_ARB` the
    last unit's weight is 1 minus the others'.
  - **Normals:** the weighted sum of each unit's transformed normal.
  - **Data:** weights come from `glWeight*ARB` or weight arrays (integer
    types normalized) and reach the shader in attribute 1.
  - Vertex programs ignore blending.
- **APPLE_vertex_point_size:** with `GL_VERTEX_POINT_SIZE_APPLE` enabled,
  each point's size comes from `glVertexPointSizefAPPLE` or the point size
  array (attribute 7).
- **Point sizes:** the fixed-function shader now applies
  `GL_POINT_DISTANCE_ATTENUATION`, the size clamps and the fade threshold,
  and rounds aliased sizes to whole pixels (GL 2.1 3.4.1). This holds
  whether or not per-vertex sizes are in use.
- **Draw path:** draws with blending or per-vertex sizes use the CPU vertex
  path, which fetches the weight and size arrays.

## Bindable uniforms

`bindable.c` implements EXT_bindable_uniform.

- **Compiling:** the compiler sees `bindable uniform` as `uniform`
  (`shader_compiler.cpp` drops the keyword and the `#extension` line).
- **Layout:** at link the declarations are parsed from the sources to lay
  each uniform out as Apple does. Each scalar, vector or matrix column
  takes a 16-byte slot, in declaration order, through arrays and structs.
- **Binding and data:** `glUniformBufferEXT` binds buffers;
  `glBindBuffer(GL_UNIFORM_BUFFER_EXT)` works. Before each draw the bound
  buffers' contents are loaded into the uniforms. `glUniform*` on a
  bindable uniform writes into its buffer.
- **Errors:** a draw raises `GL_INVALID_OPERATION` while a bindable uniform
  has no buffer, too small a buffer, or shares one with another.
  `glGetUniformBufferSizeEXT` and `glGetUniformOffsetEXT` answer as Apple
  does.

## Not implemented

- **ARB_shading_language_include:** a stub raising `GL_INVALID_OPERATION`,
  as Apple's legacy profile does.
- **`glLabelObjectWithResponsibleProcessAPPLE`:** Apple-private, still a
  stub.
- **Apple-private CGL calls:** the dispatch-table calls
  (`CGLSelectDispatch*`, `CGLRestoreDispatch*`, `CGLFrontDispatch`,
  `CGLBackDispatch`), `CGLGetSurface`/`CGLSetSurface`,
  `CGLAreContextsShared`, `CGLGetNextContext`, `CGLOpenCLMuxLockDown` and
  the pbuffer callbacks and volatile state are still stubs. They are not
  in the SDK headers.
- **Imaging limits:** colour tables up to 8192 entries; histograms up to
  65536; filters up to 11 x 11.

The raster colour is the current colour: `glRasterPos` doesn't light it.
