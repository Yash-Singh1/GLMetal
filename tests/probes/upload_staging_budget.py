#!/usr/bin/env python3
"""CPU check of the allocator's size classes for a tiny loader mip chain."""
from pathlib import Path
import re
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[2] / 'src/metal_backend.m').read_text()
classes = re.search(r'enum \{ POOL_MIN_SHIFT = .*?\};', source).group()
allocator = source[source.index('static int storage_class('):source.index('\nstatic id<MTLBuffer> pooled_storage(')]
program = '#include <assert.h>\n#include <stdio.h>\ntypedef unsigned long NSUInteger;\n' + classes + allocator + r'''
int main(void) {
    NSUInteger payload = 0, reserved = 0;
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned size = 64; size; size >>= 1) {
            NSUInteger bytes = size * size * 4;
            int c = storage_class(bytes); assert(c >= 0);
            NSUInteger allocation = (NSUInteger)1 << (c + POOL_MIN_SHIFT);
            assert(allocation >= bytes);
            payload += bytes; reserved += allocation;
        }
    assert(payload == 131064 && reserved == 245760);
    assert(reserved < 42ul * (4ul << 20) / 500);
    puts("42 tiny loader uploads: 131064 payload bytes, 245760 pooled bytes, 176160768 arena bytes");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(program)
    subprocess.run(['clang', '-Wall', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
