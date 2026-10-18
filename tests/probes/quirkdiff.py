#!/usr/bin/env python3
"""quirkdiff.py APPLE GLMETAL QUIRKS: like numdiff.py, but a line may differ
when QUIRKS documents it: an "apple:" line (what Apple's OpenGL prints)
followed by a "spec:" line (what the specification, and GLMetal, give),
under a "#" comment saying why. Fails on undocumented differences and
reports documented ones Apple no longer shows."""
import sys
TOL = 0.0015


def same(a, b):
    aw, bw = a.split(), b.split()
    if len(aw) != len(bw):
        return False
    for x, y in zip(aw, bw):
        if x == y:
            continue
        try:
            if abs(float(x) - float(y)) <= TOL:
                continue
        except ValueError:
            pass
        return False
    return True


apple = open(sys.argv[1]).read().splitlines()
glmetal = open(sys.argv[2]).read().splitlines()
quirks, apple_line = [], None
for line in open(sys.argv[3]).read().splitlines():
    if line.startswith('apple: '):
        apple_line = line[7:]
    elif line.startswith('spec: ') and apple_line is not None:
        quirks.append([apple_line, line[6:], False])
        apple_line = None
bad = 0
for i in range(max(len(apple), len(glmetal))):
    a = apple[i] if i < len(apple) else '<missing>'
    g = glmetal[i] if i < len(glmetal) else '<missing>'
    if same(a, g):
        continue
    known = [q for q in quirks if same(q[0], a) and same(q[1], g)]
    for q in known:
        q[2] = True
    if not known:
        bad += 1
        if bad <= 20:
            print(f'{i + 1}< {a}\n{i + 1}> {g}')
unused = [q for q in quirks if not q[2]]
for q in unused[:10]:
    print(f'documented quirk not seen: apple: {q[0]}')
sys.exit(1 if bad or unused else 0)
