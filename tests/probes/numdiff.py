#!/usr/bin/env python3
"""numdiff.py EXPECTED ACTUAL [TOLERANCE]: diff two probe outputs line by
line, numbers equal within TOLERANCE (default 0.0015), everything else
exactly. Prints the differing lines; exits 1 if any."""
import sys
def same(a, b, tol):
    if a == b:
        return True
    try:
        return abs(float(a) - float(b)) <= tol
    except ValueError:
        return False
expected = open(sys.argv[1]).read().splitlines()
actual = open(sys.argv[2]).read().splitlines()
tol = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0015
bad = 0
for i in range(max(len(expected), len(actual))):
    e = expected[i] if i < len(expected) else '<missing>'
    a = actual[i] if i < len(actual) else '<missing>'
    ew, aw = e.split(), a.split()
    if len(ew) != len(aw) or not all(same(x, y, tol) for x, y in zip(ew, aw)):
        bad += 1
        if bad <= 20:
            print(f'{i + 1}< {e}\n{i + 1}> {a}')
sys.exit(1 if bad else 0)
