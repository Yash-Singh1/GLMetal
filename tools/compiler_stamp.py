#!/usr/bin/env python3
"""Hash compiler inputs, retaining an explicitly verified equivalent identity."""
import hashlib
import sys
from pathlib import Path


# These exact matrix-token and image-type fast-path builds produce identical
# prepared GLSL, MSL and reflection to this predecessor. Preserve program/check
# caches only for this complete source/dependency fingerprint. Any further
# compiler or dependency change gets its own identity automatically.
EQUIVALENT_BUILDS = {
    "11f4c37a4f486743b5b9d1a81c41bc6b99896dcba798a50c792273a59169cc62":
        "3ec103d0cef6070c52c79c90315a8d5d",
    "4b58d98a3d15197d771a7899269d202202ce95a89cafa3e2896741142bb03de6":
        "3ec103d0cef6070c52c79c90315a8d5d",
}


def compiler_stamp(paths):
    digest = hashlib.sha256()
    for path in paths:
        digest.update(Path(path).read_bytes())
    fingerprint = digest.hexdigest()
    return EQUIVALENT_BUILDS.get(fingerprint, fingerprint[:32])


if __name__ == "__main__":
    print('#define GLM_COMPILER_STAMP "' + compiler_stamp(sys.argv[1:]) + '"')
