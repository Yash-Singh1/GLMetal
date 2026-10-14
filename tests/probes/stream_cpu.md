# Command stream and binding CPU probes

`python3 tests/probes/run_inline_bytes_cpu.py` extracts the real `enc_bytes` and `enc_buffer` functions and runs mock encoder tests under UBSan on arm64 and x86_64. It checks copied byte lifetime, repeated 3072-byte bindings, stage independence, changed lengths and contents, encoder replacement, byte-to-buffer transitions including a null buffer, allocation failure retry, disabled caching, oversized data, and zero lengths. No Metal device is created.

`python3 tests/probes/run_framebuffer_state_cpu.py` extracts production framebuffer setters and validation helpers. Its 98 checks cover redundant binding and attachment calls, read-only changes, bound and unbound deletion, layered attachment transitions, separate depth/stencil slots, trailing draw-buffer entries, validation errors, and legacy read-buffer completeness invalidation. Three thousand repeated setter calls cause no render-pass invalidations.

Run the command payload regression from the GLMetal directory:

```sh
xcrun clang -arch arm64 -fobjc-arc -fsanitize=address,undefined -g -O1 -Isrc -Ibuild/gen tests/probes/thread_commands_cpu.m -framework Foundation -o build/thread_commands_cpu
arch -arm64 build/thread_commands_cpu
```

Repeat with `x86_64`. This uses the real worker and allocator with CPU commands. It verifies payload bounds, ordering, a synchronous request at an exact command boundary inside a batch, batch reuse, and shutdown cleanup.

`thread_stream_bench.m` uses the same production queue to measure completed throughput. Build it with `-O2 -fobjc-arc -Isrc -Ibuild/gen -framework Foundation`. `GLM_THREAD_SOURCE` can select an archived `thread.m` for comparison. Each of three rounds enqueues two million small commands, waits for completion, and verifies sequence, recorded count, executed count, and settled state.

On the development M5 Pro, the archived worker took 31 to 36 ns per command on arm64 and 45 to 46 ns on x86_64. Publishing progress at batch completion or a pending request took 19 to 20 ns and 23 to 25 ns respectively. These CPU probes measure individual implementation costs. They do not measure gameplay FPS.

`python3 tests/probes/run_local_state_cpu.py` generates fresh marshal wrappers and combines them with the real local setter implementations. It tests both architectures under UBSan. Seven thousand identical setters leave the draw serial unchanged while pending-state query ordering, shadow hooks, display-list compilation and replay, texture pending counters, indexed state resets, validation, viewport clamping, and boolean normalization still work.
