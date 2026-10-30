"""Jump tables the tracer may have cut short: the words after each automatic table, read as more
entries, that lead to bytes it did not trace.

Usage: tablecheck.py ROM work/map.json

A hit is a lead, not a verdict: the word may be the next table, or data. Look at what the hit leads
to, and if it is a real entry add the table to analysis/hints.json. A cut table loses its code with
no complaint from the trace, and code the port never saw stops the native build when it is reached
(docs/PROCESS.md). Game-independent.
"""
import json
import sys


def sword(rom, a):
    v = int.from_bytes(rom[a:a + 2], 'big')
    return v - 0x10000 if v >= 0x8000 else v


def main():
    rom = open(sys.argv[1], 'rb').read()
    m = json.load(open(sys.argv[2]))
    kind = lambda a: int(m['kind'][a * 2:a * 2 + 2], 16)
    end = max(int(a, 16) for a in m['insn'])
    hits = 0
    for key, (fmt, targets, _) in sorted(m['auto_tables'].items()):
        if fmt != 'w' or not targets: continue
        table = int(key, 16)
        base = targets[0] - sword(rom, table)
        p = table + 2 * len(targets)
        leads = []
        while p < end and kind(p) in (0, 3):
            t = base + sword(rom, p)
            if t < 0x200 or t >= end or t & 1: break
            if kind(t) == 0: leads.append('$%06X -> $%06X' % (p, t))
            p += 2
        if leads:
            hits += 1
            print('$%06X, %d entries read; after them: %s' % (table, len(targets), ', '.join(leads)))
    print('%d tables to look at' % hits)


if __name__ == '__main__':
    main()
