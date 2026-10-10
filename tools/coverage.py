"""Which instructions of each ported routine the shadow checks have exercised.

Usage: coverage.py work/map.json COVERAGE_FILE [COVERAGE_FILE ...]

COVERAGE_FILE is written by `harness --port shadow --coverage FILE`; several runs are merged.
For each ported routine it prints how many of its instructions ran inside a shadow check, and
lists the ones that never did: those paths of the C version are unchecked.

A hand-ported routine is every instruction in its address range. A translated routine is the
instructions tools/m68k2c.py put in it, read from gen/bodies.json next to the map: its range can
span neighbouring routines that are not part of it.
"""
import sys, json, os

def main():
    m = json.load(open(sys.argv[1]))
    insn = {int(a, 16): v for a, v in m['insn'].items()}
    bodies_path = os.path.join(os.path.dirname(sys.argv[1]), 'gen', 'bodies.json')
    bodies = {}
    if os.path.exists(bodies_path):
        bodies = {k: [int(a, 16) for a in v] for k, v in json.load(open(bodies_path)).items()}
    seen, ranges = {}, {}
    for path in sys.argv[2:]:
        for line in open(path):
            f = line.split()
            name, lo, hi = f[0], int(f[1], 16), int(f[2], 16)
            ranges[name] = (lo, hi)
            seen.setdefault(name, set()).update(int(x, 16) for x in f[3:])
    full = 0
    for name, (lo, hi) in ranges.items():
        starts = bodies[name] if name in bodies else [a for a in sorted(insn) if lo <= a < hi]
        missed = [a for a in starts if a not in seen[name]]
        full += not missed
        print('%-16s %3d/%-3d instructions%s' % (name, len(starts) - len(missed), len(starts),
                                               '' if missed else '  (all)'))
        for a in missed:
            sz, mn, ops = insn[a]
            print('    never ran: %06X  %s %s' % (a, mn, ops))
    print('%d of %d routines fully covered' % (full, len(ranges)))

if __name__ == '__main__':
    main()
