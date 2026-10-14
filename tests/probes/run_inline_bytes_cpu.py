#!/usr/bin/env python3
"""Run the production inline uniform binding functions without a Metal device."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/metal_backend.m').read_text()
functions=[]
for signature in ['static void enc_buffer(', 'static void enc_bytes(']:
    start=source.index(signature)
    functions.append(source[start:source.index('\n}',source.index('{',start))+2])
fixture=(root/'tests/probes/inline_bytes_cpu.m').read_text().replace('/* PRODUCTION_FUNCTIONS */','\n\n'.join(functions))
with tempfile.TemporaryDirectory(prefix='glm-inline-cpu-') as directory:
    work=Path(directory);(work/'test.m').write_text(fixture)
    for architecture in ['arm64','x86_64']:
        binary=work/architecture
        subprocess.run(['xcrun','clang','-arch',architecture,'-O2','-g','-fobjc-arc','-fsanitize=undefined',str(work/'test.m'),'-framework','Foundation','-o',str(binary)],check=True)
        subprocess.run(['arch','-'+architecture,str(binary)],check=True)
