#!/usr/bin/env python3
"""Exercise production texture/sampler metadata setters without Metal."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "src/textures.m").read_text()
def region(begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]
production = "\n\n".join([
    region("int glm_custom_borders;", "GLM_EXPORT void glGetTexParameteriv("),
    region("struct glm_sampler_object {", "GLM_EXPORT void glGetSamplerParameterfv("),
])
fixture = (root / "tests/probes/sampling_state_cpu.c").read_text()
with tempfile.TemporaryDirectory(prefix="glmetal-sampling-cpu-") as work:
    probe = pathlib.Path(work) / "probe.c"
    probe.write_text(fixture.replace("/* PRODUCTION_FUNCTIONS */", production))
    for arch in ["arm64", "x86_64"]:
        binary = pathlib.Path(work) / arch
        subprocess.run(["xcrun", "clang", "-arch", arch, "-O2", "-g", "-fsanitize=undefined",
                        "-I" + str(root / "src"), "-I" + str(root / "build/gen"),
                        str(probe), "-o", str(binary)], check=True)
        subprocess.run(["arch", "-" + arch, str(binary)], check=True)
