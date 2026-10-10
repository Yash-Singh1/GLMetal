#!/bin/sh
# Build compiler probes only. No GL context or offline Metal compilation.
set -eu
probe_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python3 - "$probe_root" "$@" <<'PY'
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])
if len(sys.argv) > 4:
    raise SystemExit('Usage: run_shader_prepare_cpu.sh [OUTPUT_DIRECTORY [PAIR_MANIFEST]]')
if len(sys.argv) > 2:
    work = Path(sys.argv[2]).resolve()
    work.mkdir(parents=True, exist_ok=True)
else:
    (root / 'build').mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='shader-prepare-cpu-', dir=root / 'build'))
manifest = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
iterations = os.environ.get('GLM_PROBE_ITERATIONS', '20')
architecture = os.environ.get('GLM_PROBE_ARCH')
if architecture not in (None, 'arm64', 'x86_64'):
    raise SystemExit('GLM_PROBE_ARCH must be arm64 or x86_64')
source = (root / 'src/shader_compiler.cpp').read_text()
guard = 'if (!has_matrix_type_token(text)) return text;'
assert source.count(guard) == 1, 'Cannot identify the matrix token guard'
baseline = work / 'shader_compiler_baseline.cpp'
baseline_source = source.replace(guard, 'if (text.find("mat") == std::string::npos) return text;')
# Restore every optimized path, including the implementations in headers, so
# the reference measures the complete compiler before these changes.
for name, image_guard in [
    ('shader_texture_bias.h', '    if (!spirv_has_image_type(words)) return words;\n'),
    ('shader_cube_shadow.h', '    if (!spirv_has_image_type(words, spv::DimCube)) return out;\n'),
]:
    header = (root / 'src' / name).read_text()
    assert header.count(image_guard) == 1, 'Cannot identify image guard in ' + name
    reference_header = work / ('baseline_' + name)
    reference_header.write_text(header.replace(image_guard, ''))
    include = '#include "' + name + '"'
    assert baseline_source.count(include) == 1
    baseline_source = baseline_source.replace(include, '#include "' + str(reference_header) + '"')
baseline.write_text(baseline_source)
libs = ['glslang', 'MachineIndependent', 'GenericCodeGen', 'OSDependent', 'SPIRV',
        'glslang-default-resource-limits', 'spirv-cross-msl', 'spirv-cross-glsl', 'spirv-cross-core']
results = {}
for mode in ['baseline', 'updated']:
    executable = work / ('shader_prepare_' + mode)
    compiler_source = baseline if mode == 'baseline' else root / 'src/shader_compiler.cpp'
    command = ['clang++', '-O2', '-std=c++17', '-DSHADER_PREPARE_COMPILER_SOURCE="' + str(compiler_source) + '"',
               '-I' + str(root / 'third_party/install/include'), '-I' + str(root / 'third_party/glslang'),
               '-I' + str(root / 'src'), '-I' + str(root / 'build/gen'),
               str(root / 'tests/probes/shader_prepare_cpu.cpp')]
    if architecture:
        command += ['-arch', architecture]
    command += [str(root / 'third_party/install/lib' / ('lib' + name + '.a')) for name in libs]
    subprocess.run(command + ['-o', str(executable)], check=True)
    outputs = work / mode
    outputs.mkdir(exist_ok=True)
    arguments = [str(executable), str(outputs), iterations]
    if manifest:
        arguments.append(manifest)
    result = subprocess.run(arguments, text=True, capture_output=True, check=True)
    results[mode] = json.loads(result.stdout)
    print(mode, result.stdout.strip(), flush=True)

assert results['baseline']['pairs'] == results['updated']['pairs']
expected = results['baseline']['pairs']
for suffix in ['.prepared', '.result']:
    for index in range(expected):
        filename = 'pair-' + str(index) + suffix
        assert (work / 'baseline' / filename).read_bytes() == (work / 'updated' / filename).read_bytes(), filename + ' changed'
results['identical_pairs'] = expected
results['compile_speedup'] = results['baseline']['compile_ms'] / results['updated']['compile_ms']
(work / 'timings.json').write_text(json.dumps(results, indent=2) + '\n')
print('Identical prepared GLSL and serialized results for', expected, 'pairs')
print('Artifacts:', work)
PY
