"""Decode area and room data and render rooms to PNG.

Usage: rooms.py ROM [--list] [--all DIR] [--area N --room N --png out.png [--layer a|b|both]]

Transcribes the room loader: the room script at $93C4 (commands in tbl_9442), the graphics slot
list writer SetGfxSlots ($234C), the block definition unpacker at $9F6A, and the chunk unpacker
at $A068. Formats are described in docs/NOTES.md under "Rooms".
"""
import sys, struct, argparse
sys.path.insert(0, __import__('os').path.dirname(__file__))
import gfx

DIR = 0x1CC00
be16 = lambda d, o: struct.unpack_from('>H', d, o)[0]
be32 = lambda d, o: struct.unpack_from('>I', d, o)[0]
s8 = lambda v: v - 256 if v & 0x80 else v
sw = lambda d, o: struct.unpack_from('>h', d, o)[0]      # offsets go through (aN,dN.w), so they are signed

# room script: command -> argument layout. 'P' is a pointer: flag byte, then a long (flag set)
# or a word offset from the area block (flag clear).
CMDS = {0x00: 'layer', 0x01: 'layer', 0x10: 'layerL', 0x11: 'layerL',
        0x02: 'P', 0x03: 'bw', 0x04: 'P', 0x05: 'P', 0x06: 'b', 0x07: 'bwbb', 0x08: 'P',
        0x09: 'b', 0x0A: 'b', 0x0B: 'Pbb', 0x0C: 'b', 0x0D: 'b', 0x0E: 'b', 0x0F: 'bw',
        0x12: 'b', 0x13: 'x9', 0x14: 'x9'}
NAMES = {0x02: 'gfx_slots', 0x03: 'palette', 0x04: 'gfx_stream', 0x05: 'ptr_996C', 0x06: 'flags_966B',
         0x07: 'object', 0x08: 'enemies', 0x09: 'b_A0E0', 0x0A: 'palette3', 0x0B: 'tiles_2974',
         0x0C: 'call_5658', 0x0D: 'b_9682', 0x0E: 'b_9685', 0x0F: 'flag_door', 0x12: 'b_968A',
         0x13: 'cmd13', 0x14: 'cmd14'}

def area_base(d, area):
    return be32(d, DIR + 4 * (8 + area))

def room_count(d, area):
    a = area_base(d, area)
    return be16(d, a) // 2

def room_script(d, area, room):
    """parse a room script into a list of (cmd, args)"""
    a1 = area_base(d, area)
    p = a1 + sw(d, a1 + 2 * room)
    out = []
    while True:
        c = d[p]; p += 1
        if c == 0xFF: return out
        lay = CMDS.get(c)
        if lay is None: raise ValueError('area %d room %d: unknown command %02X at %06X' % (area, room, c, p - 1))
        if lay in ('layer', 'layerL'):
            kind, w, h, z = d[p], d[p + 1], d[p + 2], be16(d, p + 3); p += 5
            if lay == 'layer': src = a1 + sw(d, p); p += 2
            else: src = be32(d, p) & 0xFFFFFF; p += 4
            out.append(('layer', dict(plane='B' if c & 1 else 'A', kind=kind, w=w, h=h, z=z, src=src)))
            continue
        args = []
        for f in (lay if not lay.startswith('x') else ''):
            if f == 'P':
                if d[p]: args.append(be32(d, p + 1) & 0xFFFFFF); p += 5
                else: args.append(a1 + sw(d, p + 1)); p += 3
            elif f == 'b': args.append(d[p]); p += 1
            elif f == 'w': args.append(be16(d, p)); p += 2
        if lay.startswith('x'): args.append(d[p:p + int(lay[1:])].hex()); p += int(lay[1:])
        out.append((NAMES[c], args))

def gfx_slots(d, a0, slots):
    """SetGfxSlots ($234C): fill the 64-entry VRAM slot list (graphics block per 32 tiles).
    Every list in the ROM starts by selecting a slot ($80-$BF)."""
    i = None
    while True:
        b = d[a0]; a0 += 1
        if b < 0x80:
            slots[i] = b << 8 | d[a0]; a0 += 1; i += 1
        elif b < 0xC0: i = b & 0x3F
        elif b == 0xC0: i += 1
        elif b == 0xFF: return
        elif b < 0xF8:
            v = be16(d, a0); a0 += 2
            for k in range(b & 0x3F): slots[i] = (v + k) & 0xFFFF; i += 1
        else:
            for k in range((b & 7) + 1): slots[i] = 0xFFFF; i += 1

def blocks(d, a3, table):
    """$9F6A: block definitions into table (index -> [4 tile words] and attribute)"""
    n = be16(d, a3); a3 += 2
    if n == 0: return
    a2 = a3 + (n + 1) // 2
    for i in range(n):
        code = d[a3 + i // 2] >> (0 if i & 1 else 4) & 0xF
        if code == 0: continue
        m = code ^ 0xF
        attr = 0
        if not m & 8: attr = d[a2]; a2 += 1
        w0 = be16(d, a2); a2 += 2
        k = m & 7
        if k == 0:
            ws = [be16(d, a2), be16(d, a2 + 2), be16(d, a2 + 4)]; a2 += 6
        elif k == 1:
            ws = [w0 + 1, w0 + 0x10, w0 + 0x11]
        elif k == 2:
            b, b2 = d[a2], d[a2 + 1]; a2 += 2
            base = w0 & 0xFFE0
            ws = [base | b & 0x1F, base | b2 & 0x1F, base | (b2 & 0x60) >> 2 | b >> 5]
        elif k == 3:
            ws = [(w0 & 0xFF00) | d[a2 + j] for j in range(3)]; a2 += 3
        else:
            base = w0 & 0xE7E0
            ws = [base | b & 0x1F | (b & 0xC0) << 5 for b in d[a2:a2 + 3]]; a2 += 3
        table[i] = ([w0] + [w & 0xFFFF for w in ws], attr)

def zero_runs(d, p):
    """$A1C4: expand a zero-run stream; returns the bytes"""
    total, nmask = d[p], d[p + 1]; p += 2
    data = p + nmask; out = bytearray()
    for mi in range(nmask):
        m = d[p + mi]
        for _ in range(8):
            if len(out) == total: return bytes(out)
            if m & 0x80: out.append(0)
            else: out.append(d[data]); data += 1
            m = m << 1 & 0xFF
    while len(out) < total: out.append(d[data]); data += 1
    return bytes(out)

def chunk(d, p):
    """$A0F4: one 16x12 chunk of block words"""
    cells = [None] * 192
    left = 192
    src, q = d, p
    while True:
        c = src[q]; q += 1
        if c & 0x80:
            src, q = zero_runs(src, q), 0
            continue
        break
    for _ in range(c):
        v = src[q] << 8 | src[q + 1]; q += 2
        bits, nb = 0, 0
        for i in range(192):
            if cells[i] is not None: continue
            if nb == 0: bits = src[q]; q += 1; nb = 8
            nb -= 1
            if bits & 1: cells[i] = v; left -= 1
            bits >>= 1
    acc, nacc = 0, 0                           # four 10-bit values per five bytes
    for i in range(192):
        if cells[i] is not None: continue
        while nacc < 10: acc = acc << 8 | src[q]; q += 1; nacc += 8
        nacc -= 10
        cells[i] = acc >> nacc & 0x3FF
    return cells

def layer_map(d, src):
    """$A068: chunk map (position -> cells) and the room's own block table"""
    t3 = {}
    blocks(d, src + sw(d, src), t3)
    offs = src + sw(d, src + 2)
    out = {}
    p = src + 4
    while d[p] < 0x80:
        pos, num = d[p], d[p + 1]; p += 2
        out[pos] = chunk(d, src + sw(d, offs + num))
    return out, t3

def render(d, area, room, which='both'):
    script = room_script(d, area, room)
    shared = [{}, {}, {}]
    tl = be32(d, DIR + 4)
    for k in range(3): blocks(d, be32(d, tl + 4 * k) & 0xFFFFFF, shared[k])
    slots = [0xFFFF] * 64
    gfx_slots(d, 0x96BC, slots)
    pals = [0, 1, 0, 0]
    layers = []
    for cmd, a in script:
        if cmd == 'gfx_slots': gfx_slots(d, a[0], slots)
        elif cmd == 'palette': pals[2] = a[0]
        elif cmd == 'palette3': pals[3] = a[0]
        elif cmd == 'layer': layers.append(a)
    tiles = {}
    def tile(n):
        s = slots[n >> 5]
        if s == 0xFFFF or s & 0x8000: return None
        if s not in tiles: tiles[s] = gfx.decode(d, be32(d, gfx.TABLE + 4 * s))[0]
        return tiles[s][(n & 31) * 32:(n & 31) * 32 + 32]
    colours = [gfx.rgb(w) for p in pals for w in gfx.palette(d, p)]
    W = max(L['w'] for L in layers) * 256; H = max(L['h'] for L in layers) * 192
    img = [[(0, 0, 0)] * W for _ in range(H)]
    for L in sorted(layers, key=lambda L: L['plane'] == 'A'):      # B behind A
        if which != 'both' and L['plane'].lower() != which: continue
        cmap, t3 = layer_map(d, L['src'])
        tabs = shared + [t3]
        for pos, cells in cmap.items():
            cx, cy = pos & 7, pos >> 3
            for i, bw in enumerate(cells):
                bdef = tabs[bw >> 8 & 3].get(bw & 0xFF)
                if bdef is None: continue
                for q, tw in enumerate(bdef[0]):
                    t = tile(tw & 0x7FF)
                    if t is None: continue
                    x0 = cx * 256 + (i & 15) * 16 + (q & 1) * 8
                    y0 = cy * 192 + (i >> 4) * 16 + (q >> 1) * 8
                    pal = (tw >> 13 & 3) * 16
                    for y in range(8):
                        yy = 7 - y if tw & 0x1000 else y
                        for x in range(8):
                            xx = 7 - x if tw & 0x800 else x
                            c = t[yy * 4 + xx // 2] >> (0 if xx & 1 else 4) & 15
                            if c and y0 + y < H and x0 + x < W: img[y0 + y][x0 + x] = colours[pal + c]
    return img, script

def write(path, img):
    rows = [b'\0' + b''.join(bytes(p) for p in r) for r in img]
    gfx.write_png(path, len(img[0]), len(img), rows)

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('--list', action='store_true')
    ap.add_argument('--area', type=int); ap.add_argument('--room', type=int); ap.add_argument('--png')
    ap.add_argument('--layer', default='both'); ap.add_argument('--all')
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    if A.list:
        bad = 0
        for area in range(19):
            n = room_count(d, area)
            for room in range(n):
                try:
                    s = room_script(d, area, room)
                    for cmd, a in s:
                        if cmd == 'layer': layer_map(d, a['src'])
                    print('area %2d room %2d: %s' % (area, room, ' '.join(c if c != 'layer' else 'layer%s(%dx%d)' % (a['plane'], a['w'], a['h']) for c, a in s)))
                except Exception as e:
                    print('area %2d room %2d: ERROR %s' % (area, room, e)); bad += 1
        print('problems:', bad); sys.exit(1 if bad else 0)
    if A.all:
        import os; os.makedirs(A.all, exist_ok=True)
        for area in range(19):
            for room in range(room_count(d, area)):
                write(os.path.join(A.all, 'area%02d_room%02d.png' % (area, room)), render(d, area, room)[0])
        sys.exit(0)
    img, script = render(d, A.area, A.room, A.layer)
    for c in script: print(c)
    write(A.png, img)
