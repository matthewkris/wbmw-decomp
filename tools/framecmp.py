"""Compare two frame logs (harness/framelog.c), by game frame.

Usage: framecmp.py LOG_A LOG_B

Each log line is `COUNTER hash(RAM) hash(VRAM) hash(CRAM,VSRAM)`, written when the game waits
for VBlank. Prints, for each kind of memory, in how many of the game frames both runs reached the
hashes agree, and the first and last frame where they do not. To see what differs at a frame N,
run both programs again with `--dump-at N DIR` and compare the files. Game-independent.
"""
import sys

def load(path):
    d = {}
    for line in open(path):
        f = line.split()
        if len(f) == 4: d[int(f[0])] = f[1:]
    return d

def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    common = sorted(set(a) & set(b))
    print('game frames: %d in the first log, %d in the second, %d in both' % (len(a), len(b), len(common)))
    for i, name in enumerate(('RAM', 'VRAM', 'CRAM+VSRAM')):
        diff = [n for n in common if a[n][i] != b[n][i]]
        if not diff: print('%-10s identical in all %d' % (name, len(common))); continue
        print('%-10s identical in %d of %d; different from game frame %d to %d'
              % (name, len(common) - len(diff), len(common), diff[0], diff[-1]))

if __name__ == '__main__':
    main()
