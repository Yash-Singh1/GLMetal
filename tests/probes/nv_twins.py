#!/usr/bin/env python3
"""nv_twins.py GLSL_OUTPUT NV_OUTPUT: every "glsl NAME" result in GLSL_OUTPUT
(nv_programs on tests/probes/nv_programs_grad.txt, on Apple's OpenGL) must
equal the "fp NAME" result in NV_OUTPUT (nv_programs on nv_programs.txt, on
GLMetal), within numdiff's tolerance: Apple's GL runs TXD as a MOV, so its
GLSL texture*GradARB is the reference for GLMetal's TXD."""
import sys
TOL = 0.0015


def results(path, kind):
    lines = open(path).read().splitlines()
    out = {}
    for head, values in zip(lines[::2], lines[1::2]):
        words = head.split()
        if words[0] == kind:
            out[words[1].rstrip(':')] = values.split()
    return out


def same(a, b):
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if x == y:
            continue
        try:
            if abs(float(x) - float(y)) <= TOL:
                continue
        except ValueError:
            pass
        return False
    return True


glsl, nv = results(sys.argv[1], 'glsl'), results(sys.argv[2], 'fp')
bad = [name for name in glsl if name not in nv or not same(glsl[name], nv[name])]
for name in bad:
    print(f'{name}: GLSL {" ".join(glsl[name])[:200]}\n{name}: NV   {" ".join(nv.get(name, ["<missing>"]))[:200]}')
print(f'{len(glsl) - len(bad)}/{len(glsl)} TXD programs match the GLSL gradient lookups')
sys.exit(1 if bad or not glsl else 0)
