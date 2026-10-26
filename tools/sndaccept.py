"""The sound driver in C checked against the original (port/sounddrv.c, docs/SOUND.md): the
native build run with WBMW_Z80_SHADOW=1 from power-on, from every save state, and playing every
song and effect (--play-sound) from a state, in parallel; the routines' calls and differences
added up.

With --tick, the driver's whole ticks and its start are checked instead (WBMW_Z80_SHADOW=tick:
each tick from the timer seen to the next wait, snd_tick and snd_init against the Z80).

Usage: sndaccept.py [--frames N] [--jobs N] [--state FILE] [--tick]   (defaults: 600, 12, work/castle.state)
"""
import sys, os, re, glob, struct, subprocess, argparse, collections
from concurrent.futures import ThreadPoolExecutor

EXE = os.path.join('work', 'native', 'wbmw.exe' if os.name == 'nt' else 'wbmw')
LINE = re.compile(r'sound shadow: (\S+)\s+\$([0-9A-F]{4})\s+(\d+) calls\s+(\d+) differ')

def frame_of(state):
    return struct.unpack_from('<i', open(state, 'rb').read(), 8)[0]

MODE = '1'

def run(args):
    env = dict(os.environ, WBMW_Z80_SHADOW=MODE)
    r = subprocess.run([EXE] + args + ['--mute'], capture_output=True, text=True, env=env)
    return [l for l in r.stderr.splitlines() if l.startswith('sound shadow')]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=600); ap.add_argument('--jobs', type=int, default=12)
    ap.add_argument('--state', default=os.path.join('work', 'castle.state'))
    ap.add_argument('--tick', action='store_true')
    A = ap.parse_args()
    global MODE
    if A.tick: MODE = 'tick'
    runs = [['--frames', '1500']]
    for s in sorted(glob.glob(os.path.join('work', '*.state'))):
        runs.append(['--load-state', s, '--frames', str(frame_of(s) + A.frames)])
    f0 = frame_of(A.state)
    for n in list(range(0x18)) + list(range(0x18, 0x54)) + [0x54, 0x55, 0x56, 0x57]:
        runs.append(['--load-state', A.state, '--play-sound', '%02X' % n, '--frames', str(f0 + A.frames)])
    calls, bad, first = collections.Counter(), collections.Counter(), {}
    with ThreadPoolExecutor(A.jobs) as pool:
        for args, lines in zip(runs, pool.map(run, runs)):
            for l in lines:
                m = LINE.match(l)
                if m: calls[(m.group(2), m.group(1))] += int(m.group(3)); bad[(m.group(2), m.group(1))] += int(m.group(4))
                elif 'routines called' not in l and len(first) < 20: first.setdefault(l, ' '.join(args))
    for k in sorted(calls): print('%-22s $%s %9d calls  %d differ' % (k[1], k[0], calls[k], bad[k]))
    for l, a in first.items(): print(l, '   [' + a + ']')
    print('%d runs; %d routines called; %d calls differ' % (len(runs), len(calls), sum(bad.values())))
    return 1 if sum(bad.values()) else 0

if __name__ == '__main__':
    sys.exit(main())
