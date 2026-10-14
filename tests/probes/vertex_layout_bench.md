# Vertex layout reuse

Build the separate completed-work benchmark with `make vertex-layout-bench`.
Run it against a driver with:

```sh
arch -x86_64 build/glcompare --provider "$PWD/build/libGLMetal.dylib" \
  --cases "$PWD/build/libvertex_layout_bench.dylib" --out build/vertex-layout-bench
```

The restored variant changes and restores a used input before every draw,
and changes an unused input. The alternating-format variant changes the used
input's storage format while delivering the same values to the shader. It
warms both pipelines before timing. Each round includes 10,000 draws and a
final `glFinish`; neither variant measures shader compilation. The output
pixel must remain green. Run drivers sequentially with identical settings.

On October 14, 2026, one Rosetta cohort measured alternating formats at
1.807 and 1.819 microseconds per draw in its two warm baseline rounds, versus
1.597 and 1.534 with the bounded descriptor cache. This is about 14% less
completed-work time for this synthetic workload. Restored-state measurements
varied between cohorts and do not establish a stable performance gain.
These measurements do not establish a gameplay FPS improvement.

`GLMETAL_STREAM_STATS=1` reports layout rebuilds and restored-state reuses
every 20,000 GPU draws. The restored benchmark rebuilt once and then reused
the layout 19,999 times. Real changes to used formats, offsets, strides,
enabled state, divisors, input masks and resource generations still rebuild.
The descriptor cache compares the complete key after hashing and keeps the
global registry as its authoritative fallback. Descriptors are immutable
and retained by that registry, so the cache does not extend GL object lifetimes.

The four `core_vertex_layout_reuse` comparison cases check restored state,
live constant values, changed fetch formats/offsets, and buffer deletion and
replacement. The full vertex-filtered comparison passed 150 cases on both
arm64 and x86_64 against Apple GL after the change.
