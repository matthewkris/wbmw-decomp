"""PNG files read and written with Python's own zlib, for the tools (no libraries): any common
non-interlaced PNG is read (indexed at 1, 2, 4 or 8 bits, grey, grey and alpha, RGB, RGBA, 8 bits a
channel); indexed and RGB ones are written.

read(path) -> dict(w, h, kind ('indexed' or 'rgb'), rows: lists of indices (indexed) or of (r, g, b, a),
               palette: [(r, g, b, a)] for an indexed file)
"""
import zlib, struct

class PngError(Exception): pass

def _unfilter(raw, w, h, bpp, stride):
    rows, prev, pos = [], bytearray(stride), 0
    for y in range(h):
        f = raw[pos]; line = bytearray(raw[pos + 1:pos + 1 + stride]); pos += 1 + stride
        if len(line) != stride: raise PngError('the picture data ends early')
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f: raise PngError('an unknown row filter (%d)' % f)
        rows.append(line); prev = line
    return rows

def read(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n': raise PngError('not a PNG file')
    pos, idat, plte, trns = 8, b'', None, b''
    while pos + 8 <= len(data):
        n, kind = struct.unpack_from('>I4s', data, pos); body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR': w, h, depth, ctype, _, _, lace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE': plte = body
        elif kind == b'tRNS': trns = body
        elif kind == b'IDAT': idat += body
        elif kind == b'IEND': break
        pos += 12 + n
    if lace: raise PngError('interlaced: save it without interlacing')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None: raise PngError('an unknown colour type (%d)' % ctype)
    if depth != 8 and not (ctype in (0, 3) and depth in (1, 2, 4)):
        raise PngError('%d bits a channel: save it with 8 (or indexed)' % depth)
    bits = depth * channels
    rows = _unfilter(zlib.decompress(idat), w, h, max(1, bits // 8), (w * bits + 7) // 8)
    if ctype == 3 or (ctype == 0 and depth < 8):
        vals = [[(r[x * depth // 8] >> (8 - depth - x * depth % 8)) & ((1 << depth) - 1) for x in range(w)] for r in rows]
        if ctype == 3:
            pal = [tuple(plte[3 * i:3 * i + 3]) + ((trns[i] if i < len(trns) else 255),) for i in range(len(plte) // 3)]
            return dict(w=w, h=h, kind='indexed', rows=vals, palette=pal)
        scale = 255 // ((1 << depth) - 1)
        return dict(w=w, h=h, kind='rgb', rows=[[(v * scale,) * 3 + (255,) for v in r] for r in vals])
    out = []
    for r in rows:
        if ctype == 0: out.append([(v, v, v, 255) for v in r])
        elif ctype == 4: out.append([(r[2 * x], r[2 * x], r[2 * x], r[2 * x + 1]) for x in range(w)])
        elif ctype == 2: out.append([tuple(r[3 * x:3 * x + 3]) + (255,) for x in range(w)])
        else: out.append([tuple(r[4 * x:4 * x + 4]) for x in range(w)])
    return dict(w=w, h=h, kind='rgb', rows=out)

def _chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

def write_indexed(path, rows, palette, transparent=None):
    """8-bit indexed; palette: [(r, g, b)]; transparent: the index shown see-through, or None"""
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    trns = _chunk(b'tRNS', bytes(0 if i == transparent else 255 for i in range(transparent + 1))) if transparent is not None else b''
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0))
                           + _chunk(b'PLTE', b''.join(bytes(c) for c in palette)) + trns
                           + _chunk(b'IDAT', zlib.compress(raw, 9)) + _chunk(b'IEND', b''))
