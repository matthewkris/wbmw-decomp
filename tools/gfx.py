"""Decode the graphics table at $41000 and dump the tiles.

Usage: gfx.py ROM [--png out.png] [--blocks FIRST:LAST] [--scale N] [--palette N] [--swatches out.png]

Each of the 768 table entries covers 32 tiles (1,024 bytes of VRAM data). The top byte of the
entry selects the format; the low 24 bits point at the data. The decoders below transcribe the
68000 routine at $6BC4 (the formats are described in docs/NOTES.md). Every chunk is checked to
decode to exactly 1,024 bytes and to end exactly where the next chunk begins.
"""
import sys, struct, zlib, argparse

TABLE, COUNT, BLOCK = 0x41000, 768, 1024
PALETTES, NPAL = 0x599C, 160                  # packed palette records, read by $135C/$1370

class Bits:
    """MSB-first bit reader with the 68000 code's refill behaviour ($6DB0, $6E4E)."""
    def __init__(self, d, pos):
        self.d, self.pos, self.cur, self.left = d, pos, 0, 0
    def bit(self):
        if self.left == 0:
            self.cur = self.d[self.pos]; self.pos += 1; self.left = 8
        self.left -= 1
        return (self.cur >> self.left) & 1
    def bits(self, n):
        v = 0
        for _ in range(n): v = v << 1 | self.bit()
        return v
    def leaf(self):
        """$6E4E: 0 + 8 bits is a byte value, 1 + 5 bits is $100 + n"""
        return self.bits(8) if not self.bit() else 0x100 | self.bits(5)

def decode_lz(d, pos):
    """flag $02 ($6D9C): Huffman tree in preorder, then LZ77 over 1,024 bytes"""
    br = Bits(d, pos)
    br.bit()                                   # the root is always a node; its bit is skipped
    def tree():
        return (sub(), sub())
    def sub():
        return tree() if br.bit() else br.leaf()
    root = tree()
    def sym():
        n = root
        while True:
            n = n[br.bit()]
            if not isinstance(n, tuple):
                return br.leaf() if n == 0x11F else n
    out = bytearray()
    while len(out) < BLOCK:
        s = sym()
        if s < 0x100:
            out.append(s); continue
        length = s - 0xFD
        dist = sym() + 1
        for _ in range(length): out.append(out[-dist])
    if len(out) != BLOCK: raise ValueError('LZ overran: %d bytes' % len(out))
    return bytes(out), br.pos

def decode_tiles(d, pos):
    """flag $00 ($6BFC): 32 tiles, each with its own control byte"""
    out = bytearray()
    for _ in range(32):
        c = d[pos]; pos += 1
        if c == 0:
            out += d[pos:pos + 32]; pos += 32
        elif c < 0x80:                          # n (value, 32-bit position mask) fills, then literals
            t = bytearray(32); cover = 0
            for _ in range(c):
                v = d[pos]; m = struct.unpack_from('>I', d, pos + 1)[0]; pos += 5
                cover |= m
                for i in range(32):
                    if m >> (31 - i) & 1: t[i] = v
            for i in range(32):
                if not cover >> (31 - i) & 1: t[i] = d[pos]; pos += 1
            out += t
        else:                                   # four bitplanes, low two control bits per plane
            ctl = d[pos]; pos += 1
            planes = []
            for p in range(4):
                fill = 0xFF if ctl & 1 else 0; ctl >>= 1
                if ctl & 1:
                    rows = [fill] * 8
                else:
                    mask = d[pos]; pos += 1; rows = []
                    for r in range(8):
                        if mask >> r & 1: rows.append(d[pos]); pos += 1
                        else: rows.append(fill)
                ctl >>= 1
                if c >> p & 1:                  # XOR delta down the rows
                    for r in range(1, 8): rows[r] ^= rows[r - 1]
                planes.append(rows)
            for r in range(8):
                for x in range(0, 8, 2):
                    px = [sum(((planes[p][r] >> (7 - xx)) & 1) << p for p in range(4)) for xx in (x, x + 1)]
                    out.append(px[0] << 4 | px[1])
    return bytes(out), pos

def decode(d, entry):
    flag, addr = entry >> 24, entry & 0xFFFFFF
    if entry & 0x80000000: return bytes(BLOCK), addr
    if flag & 1: return d[addr:addr + BLOCK], addr + BLOCK
    if flag & 2: return decode_lz(d, addr)
    return decode_tiles(d, addr)

def table(d):
    return [struct.unpack_from('>I', d, TABLE + 4 * i)[0] for i in range(COUNT)]

def check(d):
    ents = table(d)
    starts = sorted(set(e & 0xFFFFFF for e in ents if e != 0xFFFFFFFF))
    nxt = dict(zip(starts, starts[1:] + [None]))
    fmt = {}; bad = 0; blocks = []; extra = {}
    for i, e in enumerate(ents):
        if e == 0xFFFFFFFF: blocks.append(bytes(BLOCK)); continue
        try:
            out, end = decode(d, e)
        except (ValueError, IndexError) as x:
            print('block %3d %08X: %s' % (i, e, x)); bad += 1; blocks.append(bytes(BLOCK)); continue
        n = nxt[e & 0xFFFFFF]
        gap = None if n is None else n - end
        if gap and e >> 24 == 1 and gap % 32 == 0:   # raw sprites run on past the block ($6F58 copies up to 127 tiles)
            extra[i] = gap // 32; gap = 0
        if gap is not None and gap != 0 and not (0 < gap and all(b == 0xFF for b in d[end:n])):
            print('block %3d %08X: ends at %06X, next chunk at %06X' % (i, e, end, n)); bad += 1
        fmt[e >> 24] = fmt.get(e >> 24, 0) + 1
        blocks.append(out)
    print('formats', {('%02X' % k): v for k, v in sorted(fmt.items())}, ' problems:', bad)
    if extra: print('raw blocks followed by extra tiles:', len(extra), 'blocks,', sorted(set(extra.values())), 'tiles each')
    return blocks, bad

def palette(d, n):
    """record n of the packed palette table, unpacked as $1370 does: 16 Genesis colour words.
    Colour 0 is transparent and never loaded (returned as 0). A record is a word of high blue
    bits, LSB first, then 15 bytes BBGGGRRR."""
    a = PALETTES + 17 * n
    hi = d[a] << 8 | d[a + 1]
    out = [0]
    for i in range(15):
        b = d[a + 2 + i]
        out.append((b & 7) << 1 | (b & 0x38) << 2 | (b & 0xC0) << 3 | (0x800 if hi >> i & 1 else 0))
    return out

def rgb(w):
    """Genesis colour word 0000BBB0GGG0RRR0 to 8-bit RGB (linear steps)"""
    return tuple(((w >> s) & 0xE) * 255 // 14 for s in (0, 4, 8))

def write_png(path, W, H, rows, pal=None, depth=8):
    """indexed PNG with palette pal, or 8-bit RGB when pal is None"""
    def chunk(t, x): return struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x))
    hdr = struct.pack('>IIBBBBB', W, H, depth, 3 if pal else 2, 0, 0, 0)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', hdr) + (chunk(b'PLTE', pal) if pal else b'')
                           + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))
    print('wrote', path, '%dx%d' % (W, H))

def swatches(path, d, size=12):
    """all palettes, one row of 16 colours each (RGB)"""
    rows = []
    for n in range(NPAL):
        rows += [b'\0' + b''.join(bytes(rgb(w)) * size for w in palette(d, n))] * size
    write_png(path, 16 * size, NPAL * size, rows)

def png(path, blocks, scale=1, pal=None):
    """one row of 32 tiles per block, in a palette record's colours or 4-bit grey levels"""
    W, H = 32 * 8 * scale, len(blocks) * 8 * scale
    rows = []
    for b in blocks:
        for y in range(8):
            px = []
            for t in range(32):
                for c in b[t * 32 + y * 4: t * 32 + y * 4 + 4]: px += [c >> 4] * scale + [c & 15] * scale
            line = bytes(px[i] << 4 | px[i + 1] for i in range(0, len(px), 2))
            rows += [b'\0' + line] * scale
    colours = [rgb(w) for w in pal] if pal else [(v * 17,) * 3 for v in range(16)]
    write_png(path, W, H, rows, b''.join(bytes(c) for c in colours), 4)

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('--png'); ap.add_argument('--blocks'); ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--palette', type=int); ap.add_argument('--swatches')
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    blocks, bad = check(d)
    if A.png:
        lo, hi = map(int, A.blocks.split(':')) if A.blocks else (0, COUNT)
        png(A.png, blocks[lo:hi], A.scale, palette(d, A.palette) if A.palette is not None else None)
    if A.swatches: swatches(A.swatches, d)
    sys.exit(1 if bad else 0)
