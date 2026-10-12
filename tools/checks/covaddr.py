"""covaddr.py RANGES FILES...: which instructions of the original in RANGES (hex, end exclusive,
comma separated, e.g. 12A70-133B2,1090C-10A1E) never ran inside a shadow check, over the
harness's coverage FILES (--coverage). Counts by address, so code shared by several routines
is counted once; use files from the current build only."""
import sys, json, os

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    m = json.load(open(os.path.join(here, '..', '..', 'work', 'map.json')))
    insn = {int(a, 16): v for a, v in m['insn'].items()}
    seen = set()
    for path in sys.argv[2:]:
        for line in open(path):
            seen.update(int(x, 16) for x in line.split()[3:])
    for r in sys.argv[1].split(','):
        lo, hi = (int(x, 16) for x in r.split('-'))
        here_insn = [a for a in sorted(insn) if lo <= a < hi]
        missed = [a for a in here_insn if a not in seen]
        print('%s: %d instructions, %d never ran' % (r, len(here_insn), len(missed)))
        for a in missed:
            print('    %06X %s %s' % (a, insn[a][1], insn[a][2]))

if __name__ == '__main__':
    main()
