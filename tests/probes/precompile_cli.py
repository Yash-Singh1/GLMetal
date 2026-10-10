#!/usr/bin/env python3
"""CPU-only CLI/cache identity regression with unique synthetic shader sources.

Uses production validation/program caches, never Metal libraries or GPU work.
Each test removes only its own cache files, identified by its private dump and
unique source bytes. No application cache entries or environment roots change.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import uuid


def string(value):
    if value is None:
        return struct.pack('<i', -1)
    encoded = value.encode()
    return struct.pack('<i', len(encoded)) + encoded


def payload(vertex, fragment, interleaved):
    return (b''.join(string(s) for s in (vertex, fragment, None, None, None, None))
            + struct.pack('<i', 1) + string('position') + struct.pack('<i', 0)
            + struct.pack('<iiBiII', 0, 0, interleaved, 0, 0, 0) + string(None) * 32)


def validation_filename(build, stage, source):
    # Only used to remove this test's own .ok marker; assertions exercise the
    # real CLI/cache APIs rather than comparing a second cache implementation.
    def key_string(value):
        encoded = value.encode() + b'\0'
        return struct.pack('<I', len(encoded)) + encoded
    value = 1469598103934665603
    for byte in key_string(build) + struct.pack('<i', stage) + key_string(source):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f'{value:016x}.ok'


def run(binary):
    with tempfile.TemporaryDirectory(prefix='glmetal-precompile-cli-') as temporary:
        directory = Path(temporary)
        dump = directory / 'dump'
        dump.mkdir()
        nonce = uuid.uuid4().hex
        vertex = (f'// synthetic cache identity test {nonce}\n#version 150 core\n'
                  'in vec4 position;void main(){gl_Position=position;}\n')
        fragment = (f'// synthetic cache identity test {nonce}\n#version 150 core\n'
                    'out vec4 color;void main(){color=vec4(1);}\n')
        (directory / 'vertex.glsl').write_text(vertex)
        (directory / 'fragment.glsl').write_text(fragment)
        base = {'vertex': 'vertex.glsl', 'fragment': 'fragment.glsl',
                'attributes': [{'name': 'position', 'location': 0}], 'feedback_interleaved': True}
        records = [base, {**base, 'attributes': [{'name': 'position', 'location': 1}]},
                   {**base, 'feedback_interleaved': False}]
        manifest = directory / 'requests.json'
        manifest.write_text(json.dumps(records))
        env = os.environ.copy()
        env.pop('GLMETAL_NO_SHADER_CACHE', None)
        env['GLMETAL_DUMP_SHADERS'] = str(dump)
        cache_root = Path(os.environ.get('HOME', str(Path.home()))) / 'Library/Caches/GLMetal'
        build = None

        def invoke(arguments, expected=0):
            nonlocal build
            result = subprocess.run([str(binary), *map(str, arguments), '--pause-ms', '0'],
                                    env=env, capture_output=True, text=True, timeout=60)
            assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
            rows = [json.loads(line) for line in result.stdout.splitlines()]
            build = rows[0]['compiler_build']
            return [row for row in rows if row['kind'] == 'request']

        try:
            cold = invoke([manifest])
            assert [row['cache_hit'] for row in cold] == [False, False, False], cold
            assert len({p.stem for p in dump.glob('*.vert.glsl')}) == 3
            hot = invoke([manifest])
            assert [row['cache_hit'] for row in hot] == [True, True, True], hot
            # Binary false and true requests must match the corresponding JSON
            # keys, despite having no explicit source-file ownership afterward.
            captures = directory / 'captures'
            captures.mkdir()
            (captures / 'false.request').write_bytes(payload(vertex, fragment, False))
            (captures / 'true.request').write_bytes(payload(vertex, fragment, True))
            replay = invoke(['--request-dir', captures])
            assert [row['cache_hit'] for row in replay] == [True, True], replay
            assert [row['feedback_interleaved'] for row in replay] == [False, True], replay
            malformed = directory / 'malformed'
            malformed.mkdir()
            valid = payload(vertex, fragment, False)
            (malformed / 'truncated.request').write_bytes(valid[:-1])
            (malformed / 'trailing.request').write_bytes(valid + b'x')
            rejected = invoke(['--request-dir', malformed, '--dry-run'], expected=1)
            assert len(rejected) == 2 and all('malformed' in row['error'] for row in rejected), rejected
            invalid = directory / 'invalid.json'
            invalid.write_text(json.dumps([{**base, 'uint_inputs': -1},
                                           {**base, 'feedback_interleaved': 'false'}]))
            rejected = invoke([invalid, '--dry-run'], expected=1)
            assert len(rejected) == 2 and all(not row['ok'] for row in rejected)
        finally:
            for path in dump.glob('*.vert.glsl'):
                (cache_root / (path.name.split('.')[0] + '.prog')).unlink(missing_ok=True)
            if build:
                for stage, source in enumerate((vertex, fragment)):
                    (cache_root / validation_filename(build, stage, source)).unlink(missing_ok=True)
    print('precompile CLI CPU PASS: cold/hot keys, binding and feedback variants, binary replay, malformed payloads')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', nargs='?', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'build/glmetal-precompile')
    arguments = parser.parse_args()
    run(arguments.binary.resolve())
