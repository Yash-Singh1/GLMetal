# GLMetal

An OpenGL implementation (Apple's legacy 2.1 and core 4.1 profiles, CGL,
AGL and the NSOpenGL classes) written directly on Metal, tested pixel by
pixel against Apple's OpenGL. See [DESIGN.md](DESIGN.md).

## Build

```sh
tools/build_deps.sh      # glslang + SPIRV-Cross, universal static libs in third_party/
make lib                 # build/libGLMetal.dylib (x86_64 + arm64) and build/glmetal-compiler (arm64)
make check               # pixel comparison suite against Apple's OpenGL
```

`build/glmetal-compiler` must sit next to `libGLMetal.dylib`: processes
running under Rosetta compile shaders in it natively.

Native shader and pipeline prewarming use bounded queues at UserInitiated
priority. A draw can share work that has already started, so that work must
not remain at Utility priority while the application loads more resources.
Slow-frame logs include pipeline prediction and reuse counters alongside
native compiler waits.

`GLMETAL_MSL_SALT=1` tests fresh programmable-function identities by changing
the submitted source comment and entry-point name once per process. A comment
alone does not establish a cold downstream pipeline cache. This diagnostic
does not alter canonical source deduplication or normal compilation. Direct
fixed-function, utility and border-variant compilation paths remain outside
its coverage.

## Layout

- `src/` the implementation; `src/marshal/` the threaded command stream
- `tools/` code generators, dependency build, `apple_dump.c` (dumps Apple's
  implementation-dependent values into `data/apple-gl-reference.txt`)
- `data/` Apple's OpenGL/CGL export lists and reference values
- `tests/glcompare/` pixel suite; `tests/bench/` draw benchmark;
  `tests/window/` windowed smoke test
