"""Survey of the widescreen HUD band: which rooms show black where their map draws nothing beside
the one-row HUD, and what their backdrop would put there instead.

Each room (assets/rooms/rooms.json) is entered from a gameplay save state by the pokes in
analysis/warp.json (a room change as an exit makes it), and the native build draws it a while
later twice: as it is, and with the band's empty pixels the backdrop (WBMW_BAND_COLOUR=64). Where
the two differ, the room has such pixels. Writes OUTDIR/report.json and, per such room, a picture
of the two bands one above the other (the top 100 lines), and OUTDIR/sheet_N.png contact sheets.

    python tools/bandsurvey.py STATE OUTDIR [--rooms A:R,...] [--jobs 12]
"""
import sys, os, json, struct, subprocess, argparse, concurrent.futures as cf
sys.path.insert(0, os.path.dirname(__file__))
import pngio, gif

EXE = os.path.abspath(os.path.join('work', 'native', 'wbmw.exe'))
BAND = 64                                       # the HUD's lines

def state_frame(path):
    out = subprocess.run([EXE, '--load-state', path, '--frames', '0', '--mute'], capture_output=True, text=True)
    for line in (out.stdout + out.stderr).splitlines():
        if 'loaded state' in line and '(frame ' in line:
            return int(line.split('(frame ')[1].rstrip(')'))
    raise SystemExit('cannot read the frame of ' + path)

def shoot(state, f0, area, room, out, colour, warp):
    at = f0 + 2
    shot = at + warp['settle']
    args = [EXE, '--load-state', state, '--frames', str(shot + 1), '--png-at', str(shot), '--out', out, '--mute', '--widescreen']
    for p in warp['pokes']:
        args += ['--poke-at', str(at), p.format(area='%02X' % area, room='%02X' % room)]
    env = dict(os.environ)
    if colour is not None: env['WBMW_BAND_COLOUR'] = str(colour)
    os.makedirs(out, exist_ok=True)
    subprocess.run(args, capture_output=True, env=env, timeout=120)
    path = os.path.join(out, 'frame_%05d.png' % shot)
    return pngio.read(path) if os.path.exists(path) else None

def survey(state, f0, area, room, outdir, warp):
    d = os.path.join(outdir, 'tmp', '%02d_%02d' % (area, room))
    a = shoot(state, f0, area, room, d + '_black', None, warp)
    b = shoot(state, f0, area, room, d + '_backdrop', 64, warp)
    if not a or not b: return {'area': area, 'room': room, 'error': 'no picture'}
    ra, rb = a['rows'], b['rows']
    W = len(ra[0])
    diff = [(x, y) for y in range(BAND) for x in range(W) if ra[y][x][:3] != rb[y][x][:3]]
    res = {'area': area, 'room': room, 'width': W, 'empty': len(diff)}
    if diff:
        x, y = diff[0]
        res['backdrop'] = '#%02X%02X%02X' % tuple(rb[y][x][:3])
        rows = [r[:] for r in ra[:100]] + [[(255, 0, 255)] * W] * 2 + [r[:] for r in rb[:100]]
        gif.write_png(os.path.join(outdir, '%02d_%02d.png' % (area, room)), [[p[:3] for p in r] for r in rows])
    return res

def sheets(outdir, found, per=8):
    """the rooms' pictures in columns of two, labelled by their file names (report.json)"""
    for n in range(0, len(found), per):
        imgs = [pngio.read(os.path.join(outdir, '%02d_%02d.png' % (r['area'], r['room'])))['rows'] for r in found[n:n + per]]
        W = max(len(i[0]) for i in imgs); H = len(imgs[0])
        cols = 2; rows_n = (len(imgs) + 1) // 2
        sheet = [[(40, 40, 40)] * (cols * (W + 8)) for _ in range(rows_n * (H + 8))]
        for k, im in enumerate(imgs):
            ox, oy = (k % cols) * (W + 8), (k // cols) * (H + 8)
            for y, row in enumerate(im):
                for x, p in enumerate(row): sheet[oy + y][ox + x] = p[:3]
        gif.write_png(os.path.join(outdir, 'sheet_%d.png' % (n // per)), sheet)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('state'); ap.add_argument('outdir')
    ap.add_argument('--rooms', help='A:R,... (default: every room in assets/rooms/rooms.json)')
    ap.add_argument('--jobs', type=int, default=12)
    A = ap.parse_args()
    warp = json.load(open('analysis/warp.json'))
    if A.rooms: rooms = [tuple(int(v) for v in s.split(':')) for s in A.rooms.split(',')]
    else: rooms = [(r['area'], r['room']) for r in json.load(open('assets/rooms/rooms.json'))]
    f0 = state_frame(A.state)
    os.makedirs(A.outdir, exist_ok=True)
    with cf.ThreadPoolExecutor(A.jobs) as ex:
        res = list(ex.map(lambda ar: survey(A.state, f0, ar[0], ar[1], A.outdir, warp), rooms))
    json.dump(res, open(os.path.join(A.outdir, 'report.json'), 'w'), indent=1)
    found = [r for r in res if r.get('empty')]
    for r in found: print('area %2d room %2d: %6d empty pixels, backdrop %s' % (r['area'], r['room'], r['empty'], r['backdrop']))
    print('%d of %d rooms; %d without a picture' % (len(found), len(res), sum('error' in r for r in res)))
    sheets(A.outdir, found)

if __name__ == '__main__':
    main()
