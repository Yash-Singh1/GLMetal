#!/usr/bin/env python3
"""Run production framebuffer setters against a CPU invalidation counter."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "src/objects.c").read_text()

def region(begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]

production = "\n\n".join([
    region("struct glm_framebuffer *glm_framebuffer_get(", "GLM_HIDDEN GLenum glm_framebuffer_status("),
    region("GLM_HIDDEN GLenum glm_color_buffer_error(", "GLM_EXPORT GLenum glCheckFramebufferStatus("),
    region("GLM_EXPORT void glDrawBuffers(", "GLM_EXPORT void glBlitFramebuffer("),
])
fixture = (root / "tests/probes/framebuffer_state_cpu.c").read_text()
marker = "/* PRODUCTION_FUNCTIONS */"
resume_line = fixture[:fixture.index(marker)].count("\n") + 2
with tempfile.TemporaryDirectory(prefix="glmetal-framebuffer-cpu-") as work:
    probe = pathlib.Path(work) / "probe.c"
    probe.write_text(fixture.replace(marker, '#line 1 "objects.c"\n' + production +
                                    f'\n#line {resume_line} "framebuffer_state_cpu.c"'))
    for arch in ["arm64", "x86_64"]:
        binary = pathlib.Path(work) / arch
        subprocess.run([
            "xcrun", "clang", "-arch", arch, "-O2", "-g", "-fsanitize=undefined",
            "-I" + str(root / "src"), "-I" + str(root / "build/gen"),
            str(probe), "-o", str(binary),
        ], check=True)
        subprocess.run(["arch", "-" + arch, str(binary)], check=True)
