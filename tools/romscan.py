"""Check that a file to be published does not carry the ROM: the longest runs of the ROM's bytes in it.

Usage: romscan.py ROM FILE [LIMIT]

Lists runs of 16 bytes or more (ignoring those of fewer than 6 different values, such as padding)
and fails if any reaches LIMIT bytes (default 64). The hand-written C holds small tables of the
game's, so short runs are expected. Game-independent.
"""
import sys


def main():
    rom = open(sys.argv[1], 'rb').read()
    out = open(sys.argv[2], 'rb').read()
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    n = 16
    seen = {}
    for i in range(len(out) - n + 1):
        seen.setdefault(out[i:i + n], i)
    runs = []
    i = 0
    while i < len(rom) - n:
        k = rom[i:i + n]
        if k in seen and len(set(k)) >= 6:
            e = seen[k]
            j = n
            while i + j < len(rom) and e + j < len(out) and rom[i + j] == out[e + j]: j += 1
            runs.append((j, i, e))
            i += j
        else:
            i += 1
    for j, a, e in sorted(runs, reverse=True)[:10]:
        print('%d bytes: ROM $%06X, file offset $%X' % (j, a, e))
    print('%d runs, %d bytes, limit %d' % (len(runs), sum(r[0] for r in runs), limit))
    sys.exit(1 if runs and max(runs)[0] >= limit else 0)


if __name__ == '__main__':
    main()
