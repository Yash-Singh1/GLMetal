# Texture validation CPU probe

Run `python3 native/glmetal/tests/probes/run_texture_validation_cpu.py` from the repository root. It extracts the real storage validation, sampling view, level definition, completeness, and mipmap bookkeeping functions. The probe runs on arm64 and x86_64 under UBSan with CPU mock Metal objects. It creates no Metal device or GL context and submits no GPU work.

The probe verifies that repeated complete texture bindings avoid all Metal property calls and format lookups. Mutation cases cover base and maximum levels, mip filters, changed level dimensions, mip generation, missing cube faces, arrays, 3D depth, multisample counts, rectangle levels, immutable level limits, swizzles, sRGB decode overrides, depth modes, context profiles, dirty storage, allocation retry, failed view replacement, pending upload callbacks, and object reuse.

The benchmark compares 500,000 complete mipmapped bindings with validation caches forcibly missed against the same production functions with warm caches. It counts the calls removed and prints elapsed CPU time. This measures texture selection overhead with mock objects. It does not measure frame rate or GPU work.

Storage validation compares the current base image definition, target, and sample count before reading Metal properties. Sampling validation also compares definition and storage generations, base and maximum levels, the texture minification filter, immutable level limits, swizzle, depth mode, context profile, and effective sRGB decode selection. Changed definitions and every mipmap generator advance the definition generation. Storage rebuilds advance the storage generation. Allocation and view failures remain retryable.

These caches reuse the CPU decision about an existing storage or view. Uploads, pending upload flushes, framebuffer writes, per-draw texture and sampler binding, and live sampler metadata still execute through their existing paths. The existing serialized shared-object access rules apply.
