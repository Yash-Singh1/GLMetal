#!/usr/bin/env python3
"""Test actual texture validation functions with CPU mock Metal objects."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "src/textures.m").read_text()
signatures = [
    "static bool layers_in_height(", "static bool layers_in_depth(",
    "static bool multisampled(", "static bool layered(",
    "static int face_index(", "static int face_count(", "static int mip_count(",
    "bool glm_backend_texture_ensure_storage(", "static bool metal_mipmappable(",
    "void glm_backend_generate_mipmaps(", "static bool define_level(",
    "static bool mipmapped_filter(", "static bool complete(",
    "static MTLPixelFormat linear_format(", "static id<MTLTexture> sampling_view(",
]

def extract(signature):
    begin = source.index(signature)
    brace = source.index("{", begin)
    # One-line functions have their closing brace on the signature's line.
    newline = source.index("\n", brace)
    if "}" in source[brace:newline]:
        return source[begin:newline]
    return source[begin:source.index("\n}", brace) + 2]

fixture = (root / "tests/probes/texture_validation_cpu.m").read_text()
generated = fixture.replace("/* PRODUCTION_FUNCTIONS */", "\n\n".join(map(extract, signatures)))
with tempfile.TemporaryDirectory(prefix="glmetal-texture-cpu-") as work:
    probe = pathlib.Path(work) / "probe.m"
    probe.write_text(generated)
    for arch in ["arm64", "x86_64"]:
        binary = pathlib.Path(work) / arch
        subprocess.run([
            "xcrun", "clang", "-arch", arch, "-O2", "-g", "-fobjc-arc",
            "-fsanitize=undefined", "-Wno-incompatible-pointer-types",
            "-Wno-incompatible-pointer-types-discards-qualifiers",
            "-I" + str(root / "src"), "-I" + str(root / "build/gen"),
            str(probe), "-framework", "Foundation", "-framework", "Metal", "-o", str(binary),
        ], check=True)
        subprocess.run(["arch", "-" + arch, str(binary)], check=True)
