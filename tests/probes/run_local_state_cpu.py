#!/usr/bin/env python3
"""Generate real marshal wrappers and test actual local setter implementations."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
names = [
    "glActiveTexture", "glActiveTextureARB", "glDepthFunc", "glDepthMask",
    "glCullFace", "glFrontFace", "glViewport", "glScissor", "glGetFloatv",
    "glViewportArrayv", "glViewportIndexedf", "glScissorArrayv", "glScissorIndexed",
    "glTexParameteri", "glGenerateMipmap",
]

def extract(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    newline = source.index("\n", brace)
    if "}" in source[brace:newline]:
        return source[start:newline]
    return source[start:source.index("\n}", brace) + 2]

context = (root / "src/context.c").read_text()
legacy = (root / "src/legacy_state.c").read_text()
signatures = [
    "static void set_viewport(", "static void set_scissor(", "static void local_draw_state_changed(",
    "GLM_EXPORT void glViewport(", "GLM_EXPORT void glScissor(",
    "GLM_EXPORT void glViewportArrayv(", "GLM_EXPORT void glViewportIndexedf(",
    "GLM_EXPORT void glScissorArrayv(", "GLM_EXPORT void glScissorIndexed(",
    "int glm_indexed_state_query(", "GLM_EXPORT void glDepthFunc(",
    "GLM_EXPORT void glDepthMask(", "GLM_EXPORT void glCullFace(", "GLM_EXPORT void glFrontFace(",
]
production = "\n\n".join(extract(context, signature) for signature in signatures)
production += "\n\n" + extract(legacy, "GLM_EXPORT void glActiveTexture(")
production += "\n\n" + extract(legacy, "GLM_EXPORT void glActiveTextureARB(")
fixture = (root / "tests/probes/local_state_cpu.c").read_text()
sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
with tempfile.TemporaryDirectory(prefix="glmetal-local-state-") as work:
    work = pathlib.Path(work)
    exports = work / "exports.txt"
    exports.write_text("\n".join(names) + "\n")
    subprocess.run(["python3", str(root / "tools/gen_marshal.py"), sdk, str(work), str(exports)], check=True)
    # This verifies that the intended wrapper was actually emitted by SDK discovery.
    generated = (work / "marshal.c").read_text()
    for name in names:
        assert "GLM_EXPORT void " + name + "(" in generated, name
    fixture = fixture.replace("/* PRODUCTION_IMPLEMENTATIONS */", production)
    fixture = fixture.replace("/* IMPLEMENTATION_UNRENAME */", "\n".join("#undef " + name for name in names))
    probe = work / "probe.c"
    probe.write_text(fixture)
    for arch in ["arm64", "x86_64"]:
        binary = work / arch
        subprocess.run([
            "xcrun", "clang", "-arch", arch, "-O2", "-g", "-fsanitize=undefined",
            "-I" + str(root / "src"), "-I" + str(root / "build/gen"), "-I" + str(work),
            str(probe), "-o", str(binary),
        ], check=True)
        subprocess.run(["arch", "-" + arch, str(binary)], check=True)
