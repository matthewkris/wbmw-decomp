"""Compare two harness runs frame by frame and report the first difference.

Usage: cmpruns.py DIR_A DIR_B [--kind ram|vdp|frame] [--all] [--max N]

Each directory holds dumps from `harness --ram-every`, `--vdp-every` or `--png-every`
(ram_NNNNN.bin, vdp_NNNNN.bin, frame_NNNNN.png). Frames present in both are compared in order.
The first frame that differs is reported in detail: differing bytes grouped into runs, each
labelled with what is known about the address (entity field and slot, named RAM, VDP area).
--all also lists every later frame that differs, with a count. Exit status 1 if any differ.
"""
import sys, os, re, glob, zlib, struct, argparse

# ---- what is known about RAM ($FF0000 + offset), from docs/NOTES.md ----
ENTITY_FIELDS = {   # array ($FFxx00) -> field name; each array holds 64 entities, 4 bytes apart
    0xC0: 'flags', 0xC1: 'mapping', 0xC2: 'cam index / tile base', 0xC3: 'anim set/group/num',
    0xC4: 'anim script', 0xC5: 'anim loop', 0xC6: 'anim timer/speed', 0xC7: 'blink',
    0xC8: 'X', 0xC9: 'Y', 0xCA: 'velocity', 0xCB: 'box', 0xCC: 'accel', 0xCD: 'speed limit',
    0xCF: 'collision', 0xD1: 'physics mode', 0xD2: 'previous X/Y', 0xD4: 'enemy state/type',
    0xD5: 'contact flags', 0xD6: 'attack boxes', 0xD7: 'hurt boxes', 0xD9: 'contact damage',
    0xDA: 'health', 0xDE: 'hit pending/damage',
}
RAM_NAMES = [   # (start, end, name)
    (0x2800, 0x2A80, 'sprite table buffer'), (0x2D00, 0x3100, 'graphics block buffer'),
    (0x3300, 0x3380, 'save data buffer'), (0x3E00, 0x5000, 'room block tables'),
    (0x5000, 0x8000, 'chunk RAM'), (0x8002, 0x8006, 'frame counter'), (0x8006, 0x8007, 'frame flags'),
    (0x8048, 0x8248, 'system task slots'), (0x8248, 0x8A48, 'task slots'),
    (0x8A49, 0x8A4A, 'VBlank counter'), (0x8A52, 0x8A56, 'random seed'), (0x8A5A, 0x8A72, 'VDP register mirrors'),
    (0x8A7A, 0x8A86, 'pad state'), (0x8B56, 0x8BD6, 'target palette'), (0x8BD6, 0x8C56, 'palette'),
    (0x8C56, 0x8C57, 'palette flags'), (0x8E08, 0x8F08, 'graphics slots'), (0x8F0A, 0x9300, 'Huffman tree'),
    (0x9668, 0x9669, 'area'), (0x9669, 0x966A, 'room'), (0x966E, 0x9672, 'palette ids'),
    (0x9754, 0x97D4, 'layer A'), (0x97D4, 0x9854, 'layer B'), (0x9992, 0x99C0, 'game flags'),
    (0x9EEE, 0x9EF0, 'player entity'), (0xA11A, 0xA11C, 'first enemy slot'), (0xA11C, 0xA11E, 'enemy target'),
    (0xA122, 0xA142, 'enemy tile bases'), (0xE500, 0x10000, 'shared block tables'),
]

def ram_label(off):
    if 0xC000 <= off < 0xE000:
        arr = off >> 8
        slot = (off & 0xFF) >> 2
        return 'entity %02X %s (+%d)' % (slot * 4, ENTITY_FIELDS.get(arr, 'array $%02X00' % arr), off & 3)
    for a, b, n in RAM_NAMES:
        if a <= off < b: return n + ('' if b - a <= 4 else ' +$%X' % (off - a))
    return ''

def vdp_label(off):
    if off < 0x10000: return 'VRAM $%04X (tile $%03X)' % (off, off >> 5)
    off -= 0x10000
    if off < 128: return 'CRAM line %d colour %d' % (off // 32, (off % 32) // 2)
    off -= 128
    if off < 80: return 'VSRAM %d' % (off // 2)
    return 'VDP register %d' % (off - 80)

def runs(a, b):
    """differing byte offsets grouped into runs [(start, end)]"""
    out = []
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            if out and out[-1][1] >= i - 2: out[-1][1] = i + 1
            else: out.append([i, i + 1])
    return out

def read_png(p):
    b = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(b):
        n = struct.unpack('>I', b[i:i + 4])[0]; t = b[i + 4:i + 8]; c = b[i + 8:i + 8 + n]; i += 12 + n
        if t == b'IHDR': w, h = struct.unpack('>II', c[:8])
        if t == b'IDAT': idat += c
    raw = zlib.decompress(idat); s = w * 3 + 1
    return w, h, [raw[y * s + 1:(y + 1) * s] for y in range(h)]

def detail_bytes(a, b, label, base, limit):
    rs = runs(a, b)
    print('  %d bytes differ in %d runs' % (sum(e - s for s, e in rs), len(rs)))
    for s, e in rs[:limit]:
        print('  %s-%s  %-40s A: %s' % (base(s), base(e - 1), label(s), a[s:min(e, s + 16)].hex(' ')))
        print('  %s %s  %-40s B: %s' % (' ' * len(base(s)), ' ' * len(base(e - 1)), '', b[s:min(e, s + 16)].hex(' ')))
    if len(rs) > limit: print('  ... %d more runs' % (len(rs) - limit))

def detail_frame(pa, pb):
    wa, ha, A = read_png(pa); wb, hb, B = read_png(pb)
    if (wa, ha) != (wb, hb): print('  sizes differ: %dx%d vs %dx%d' % (wa, ha, wb, hb)); return
    n = 0; x0 = y0 = 10 ** 9; x1 = y1 = -1
    for y in range(ha):
        if A[y] == B[y]: continue
        for x in range(wa):
            if A[y][3 * x:3 * x + 3] != B[y][3 * x:3 * x + 3]:
                n += 1; x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
    print('  %d pixels differ, within x %d-%d, y %d-%d' % (n, x0, x1, y0, y1))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('a'); ap.add_argument('b')
    ap.add_argument('--kind', default='ram', choices=('ram', 'vdp', 'frame'))
    ap.add_argument('--all', action='store_true'); ap.add_argument('--max', type=int, default=20)
    A = ap.parse_args()
    pat = {'ram': 'ram_*.bin', 'vdp': 'vdp_*.bin', 'frame': 'frame_*.png'}[A.kind]
    fa = {int(re.search(r'(\d+)', os.path.basename(p)).group(1)): p for p in glob.glob(os.path.join(A.a, pat))}
    fb = {int(re.search(r'(\d+)', os.path.basename(p)).group(1)): p for p in glob.glob(os.path.join(A.b, pat))}
    common = sorted(set(fa) & set(fb))
    if not common: print('no %s dumps for the same frame in both directories' % A.kind); return 2
    diff = []
    for fr in common:
        if open(fa[fr], 'rb').read() != open(fb[fr], 'rb').read():
            diff.append(fr)
            if not A.all: break
    print('%d frames with %s dumps in both runs (%d-%d)' % (len(common), A.kind, common[0], common[-1]))
    if not diff: print('identical'); return 0
    fr = diff[0]
    print('first difference at frame %d' % fr)
    if A.kind == 'frame': detail_frame(fa[fr], fb[fr])
    elif A.kind == 'ram':
        detail_bytes(open(fa[fr], 'rb').read(), open(fb[fr], 'rb').read(), ram_label, lambda o: '$FF%04X' % o, A.max)
    else:
        detail_bytes(open(fa[fr], 'rb').read(), open(fb[fr], 'rb').read(), vdp_label, lambda o: '%05X' % o, A.max)
    if A.all: print('frames that differ: %d of %d, first %s' % (len(diff), len(common), ' '.join(map(str, diff[:20]))))
    return 1

if __name__ == '__main__':
    sys.exit(main())
