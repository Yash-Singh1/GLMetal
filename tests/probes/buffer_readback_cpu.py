#!/usr/bin/env python3
"""Exercise real buffer readback code with a deterministic fake backend."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/objects.c').read_text()
start = source.index('GLM_EXPORT void glGetBufferSubData(')
code = source[start:source.index('\nstatic void *map_range', start)]
marshal = (root / 'build/gen/marshal.c').read_text()
start = marshal.index('GLM_EXPORT void glGetBufferSubData(')
wrapper = marshal[start:marshal.index('\n}', start) + 2]
code = code.replace('void glGetBufferSubData(', 'void glm_impl_glGetBufferSubData(')
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef unsigned GLenum;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;
typedef void GLvoid;
#define GLM_EXPORT
#define GL_INVALID_VALUE 1
#define GLM_CONTEXT(name) struct glm_context *name = &context
#define GLM_STREAM(ctx) 1
struct glm_context { int thread; };
static struct glm_context context = {1};
struct glm_buffer { GLsizeiptr size; void *gpu_writer; uint64_t gpu_write_serial; };
static struct glm_buffer buffer = {8};
static uint8_t contents[8], queued[8];
static unsigned syncs, fences, writer_waits;
static int pending_upload, gpu_pending;
static struct glm_context *glm_current(void) { return &context; }
static struct glm_buffer *bound_buffer(struct glm_context *ctx, GLenum target) { return &buffer; }
static void glm_error(struct glm_context *ctx, GLenum error) { assert(0); }
static void glm_thread_sync_named(struct glm_context *ctx, const char *name) {
    ++syncs;
    if (pending_upload) { memcpy(contents,queued,8); pending_upload = 0; }
}
static void glm_backend_flush(struct glm_context *ctx, int wait) {
    assert(wait); ++fences;
    if (buffer.gpu_writer && gpu_pending) { memset(contents,77,8); gpu_pending = 0; ++writer_waits; }
}
static const void *glm_backend_buffer_contents(struct glm_context *ctx, struct glm_buffer *b, int write) {
    assert(!write); return contents;
}
'''
main = r'''
int main(void) {
    uint8_t out[8];
    memset(queued,33,8); pending_upload = 1; gpu_pending = 1;
    glGetBufferSubData(1,0,8,out);
    for (unsigned i=0;i<8;++i) assert(out[i] == 33);
    assert(syncs == 1 && fences == 0 && gpu_pending == 1);
    memset(queued,44,8); pending_upload = 1;
    glGetBufferSubData(1,2,4,out);
    for (unsigned i=0;i<4;++i) assert(out[i] == 44);
    assert(syncs == 2 && fences == 0);
    buffer.gpu_writer = &context; buffer.gpu_write_serial = 9;
    glGetBufferSubData(1,0,8,out);
    for (unsigned i=0;i<8;++i) assert(out[i] == 77);
    assert(syncs == 3 && fences == 1 && writer_waits == 1 && gpu_pending == 0);
    puts("CPU readback preserves queued uploads without fencing unrelated GPU work; GPU-written readback fences");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prefix + code + wrapper + main)
    subprocess.run(['clang', '-Wall', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
