#!/usr/bin/env python3
"""keydiff.py APPLE GLMETAL QUIRKS: like quirkdiff.py for outputs whose lines
are keyed by the text before the first ":" (tools/apple_enum_sweep.c): lines
are paired by key, and a line one side lacks reads as "<missing>". A pair may
differ when QUIRKS documents it ("apple:" then "spec:" under a "#" comment);
fails on undocumented differences and on documented ones no longer seen."""
import sys


def keyed(path):
    lines = [l for l in open(path).read().splitlines() if l and not l.startswith('#')]
    return {l.split(':', 1)[0]: l for l in lines}, [l.split(':', 1)[0] for l in lines]


apple, order = keyed(sys.argv[1])
glmetal, extra = keyed(sys.argv[2])
order += [k for k in extra if k not in apple]
quirks, apple_line = [], None
for line in open(sys.argv[3]).read().splitlines():
    if line.startswith('apple: '):
        apple_line = line[7:]
    elif line.startswith('spec: ') and apple_line is not None:
        quirks.append([apple_line, line[6:], False])
        apple_line = None
bad = 0
for key in order:
    a, g = apple.get(key, '<missing>'), glmetal.get(key, '<missing>')
    if a == g:
        continue
    known = [q for q in quirks if q[0] == a and q[1] == g]
    for q in known:
        q[2] = True
    if not known:
        bad += 1
        if bad <= 20:
            print(f'< {a}\n> {g}')
unused = [q for q in quirks if not q[2]]
for q in unused[:10]:
    print(f'documented difference not seen: apple: {q[0][:160]}')
sys.exit(1 if bad or unused else 0)
