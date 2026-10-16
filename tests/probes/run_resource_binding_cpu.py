#!/usr/bin/env python3
"""Run production program sampling reuse logic with CPU binding counters."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "src/metal_backend.m").read_text()
def region(text, begin, end):
    start = text.index(begin)
    return text[start:text.index(end, start)]
key = region(source, "struct program_sampling_key {", "struct glm_backend_context {")
functions = region(source, "static void program_sampling_key(", "static int color_attachment_count(")
functions += region(source, "static size_t uniform_bytes(", "/* Geometry stage emulation")
textures = (root / "src/textures.m").read_text()
binding = region(textures, "int glm_sampler_binding(", "/* Program samplers:")
fixture = (root / "tests/probes/resource_binding_cpu.m").read_text()
with tempfile.TemporaryDirectory(prefix="glmetal-resource-binding-cpu-") as work:
    probe = pathlib.Path(work) / "probe.m"
    probe.write_text(fixture.replace("/* PRODUCTION_KEY */", key)
                     .replace("/* PRODUCTION_FUNCTIONS */", binding + functions))
    for arch in ["arm64", "x86_64"]:
        binary = pathlib.Path(work) / arch
        subprocess.run(["xcrun", "clang", "-arch", arch, "-O2", "-g", "-fsanitize=undefined",
                        "-I" + str(root / "src"), "-I" + str(root / "build/gen"),
                        str(probe), "-o", str(binary)], check=True)
        subprocess.run(["arch", "-" + arch, str(binary)], check=True)
