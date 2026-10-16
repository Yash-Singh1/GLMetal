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

`tests/probes/run_shader_prepare_cpu.sh` compares the matrix-token fast path
against its prior declaration scan without creating a GL context. It checks
prepared GLSL and complete serialized compiler results for generated shader
pairs, and can accept a tab-separated vertex/fragment source manifest. Set
`GLM_PROBE_ARCH=arm64` or `x86_64` to select the compiler architecture.

Compiler cache identities normally hash all listed compiler inputs. The exact
verified equivalent fast-path build retains its predecessor's identity through
`tools/compiler_stamp.py`; any further source or dependency edit invalidates
that exception. This avoids discarding installed caches for an optimization
that produces the same results.

## Sampling bindings

Programmable draws reuse their texture, sampler and LOD bindings while the
program, sampler mappings and sampling resources remain unchanged on the same
encoder. Shared texture or sampler metadata changes invalidate this reuse.
Repeated raster changes and ordinary uniform updates can reuse sampling bindings.
Program switches start with a short snapshot; repeated programs can promote
to a full sampling dependency key.
Buffer textures stay on the live binding path, and failed texture selections
remain retryable. Set `GLMETAL_NO_SAMPLING_BINDING_CACHE=1` to disable reuse for
comparisons. `GLMETAL_STREAM_STATS=1` reports sampling calls and cache reuse.
Sampler-state lookups use a two-way thread-local cache with full-key equality
checks before a hit.

The CPU probes use the production metadata setters and binding decisions with
UBSan on arm64 and x86_64, without a GPU workload:

```sh
python3 tests/probes/run_sampling_state_cpu.py
python3 tests/probes/run_resource_binding_cpu.py
python3 tests/probes/run_inline_bytes_cpu.py
```

`make sampling-bindings-bench` builds a separate glcompare case library at
`build/libsampling_bindings_bench.dylib`. It measures completed draw batches
with changing raster state and stable sampling in a one-pixel viewport.
Its timings measure that workload, not gameplay FPS. The pixel suite includes
`core_shared_sampling_state` for shared sampler and texture metadata edits,
and program sampler-unit remapping while the consumer keeps its bindings.

## Offline shader preparation

Set `GLMETAL_DUMP_REQUESTS=/absolute/existing/parent/requests` while running an
application to record exact vertex/fragment link requests, including bindings
and variant fields. Recording is opt-in and includes cache hits. Request files
contain application shader source and belong in local, ignored build storage.

```sh
make build/glmetal-precompile
build/glmetal-precompile --request-dir /path/to/requests --metal-libraries
```

The native arm64 utility validates each stage and prepares the production
`.ok` and `.prog` caches under `~/Library/Caches/GLMetal`. It processes one
request at a time with a 25 ms pause. `--metal-libraries` also compiles the
generated Metal libraries with the driver's language and math options to warm
the system's device cache. It does not submit GPU work or store standalone
`.metallib` files. A second invocation reports exact program-cache hits.
`--dry-run` checks inputs without compilation; `--limit N` bounds the workload.
A JSON manifest can specify source files and every request field instead of
using recorded binary requests. Geometry, tessellation and compute requests
are not supported by this utility.

Shader libraries alone cannot precompile every render pipeline. Pipeline
creation also needs the draw's attachment formats, sample count, blending,
vertex inputs and function specialization. New shader or state variants can
still compile during gameplay. See Apple's documentation on
[binary archives](https://developer.apple.com/documentation/metal/metal-binary-archives)
for the distinction between Metal IR and GPU-specific pipeline binaries.

`python3 tests/probes/precompile_cli.py` checks cold/hot cache reuse, binding
and feedback variants, exact JSON/binary request identity, and malformed input
rejection without a GL context or Metal compilation.

## Layout

- `src/` the implementation; `src/marshal/` the threaded command stream
- `tools/` code generators, dependency build, `apple_dump.c` (dumps Apple's
  implementation-dependent values into `data/apple-gl-reference.txt`)
- `data/` Apple's OpenGL/CGL export lists and reference values
- `tests/glcompare/` pixel suite; `tests/bench/` draw benchmark;
  `tests/window/` windowed smoke test
