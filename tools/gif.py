"""PNG frames (8-bit RGB or RGBA, as the harness and the native build write them) to an animated
GIF, or one PNG scaled up, with no libraries beyond Python's own: for the README's pictures.

Each frame of the GIF stores only the rectangle that changed since the last, and all frames share
one palette (the game's pictures have far fewer than 256 colours).

Usage: gif.py OUT.gif FRAME.png... [--scale N] [--delay CS] [--crop X,Y,W,H]
       gif.py OUT.png FRAME.png [--scale N] [--crop X,Y,W,H]       (a still)
"""
import sys, zlib, struct, argparse

def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', body)
            assert depth == 8 and ctype in (2, 6), 'only 8-bit RGB or RGBA'
            bpp = 3 if ctype == 2 else 4
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return rows

def transform(rows, scale, crop):
    if crop:
        x, y, w, h = crop
        rows = [r[x:x + w] for r in rows[y:y + h]]
    return [[p for p in r for _ in range(scale)] for r in rows for _ in range(scale)]

def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(c for p in r for c in p) for r in rows)
    chunk = lambda k, d: struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def lzw(indices, bits):
    """GIF's LZW: codes of growing width, a clear code when the table is full"""
    clear, end = 1 << bits, (1 << bits) + 1
    out, acc, nacc = bytearray(), 0, 0
    def put(code, width):
        nonlocal acc, nacc
        acc |= code << nacc; nacc += width
        while nacc >= 8: out.append(acc & 255); acc >>= 8; nacc -= 8
    table, width, nxt = {}, bits + 1, end + 1
    put(clear, width)
    cur = None
    for k in indices:
        if cur is None: cur = k; continue
        key = (cur, k)
        if key in table: cur = table[key]; continue
        put(cur, width)
        if nxt < 4096:
            table[key] = nxt; nxt += 1
            if nxt > (1 << width) and width < 12: width += 1
        else:
            put(clear, width); table, width, nxt = {}, bits + 1, end + 1
        cur = k
    if cur is not None: put(cur, width)
    put(end, width)
    if nacc: out.append(acc & 255)
    return bytes(out)

def write_gif(path, frames, delay):
    colours = {}
    for f in frames:
        for r in f:
            for p in r: colours.setdefault(p, len(colours))
    assert len(colours) <= 256, '%d colours: more than a GIF holds' % len(colours)
    bits = max(2, (len(colours) - 1).bit_length())
    pal = list(colours) + [(0, 0, 0)] * ((1 << bits) - len(colours))
    h, w = len(frames[0]), len(frames[0][0])
    out = bytearray(b'GIF89a' + struct.pack('<HHBBB', w, h, 0x80 | (bits - 1), 0, 0))
    out += bytes(c for p in pal for c in p)
    out += b'\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00'          # loop forever
    prev, wait = None, 0
    for f in frames:
        if prev is None: x0, y0, x1, y1 = 0, 0, w, h
        else:
            ys = [y for y in range(h) if f[y] != prev[y]]
            if not ys: wait += delay; continue                      # unchanged: the last one lasts longer
            xs = [x for y in ys for x in range(w) if f[y][x] != prev[y][x]]
            x0, y0, x1, y1 = min(xs), ys[0], max(xs) + 1, ys[-1] + 1
        if prev is not None: out[gce_at:gce_at + 2] = struct.pack('<H', delay + wait)
        wait = 0
        out += b'\x21\xF9\x04\x00'; gce_at = len(out); out += struct.pack('<H', delay) + b'\x00\x00'
        out += b'\x2C' + struct.pack('<HHHHB', x0, y0, x1 - x0, y1 - y0, 0) + bytes([bits])
        data = lzw((colours[f[y][x]] for y in range(y0, y1) for x in range(x0, x1)), bits)
        for i in range(0, len(data), 255): out += bytes([len(data[i:i + 255])]) + data[i:i + 255]
        out += b'\x00'
        prev = f
    if wait: out[gce_at:gce_at + 2] = struct.pack('<H', delay + wait)
    out += b'\x3B'
    open(path, 'wb').write(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out'); ap.add_argument('frames', nargs='+')
    ap.add_argument('--scale', type=int, default=1); ap.add_argument('--delay', type=int, default=3)
    ap.add_argument('--crop')
    A = ap.parse_args()
    crop = tuple(int(v) for v in A.crop.split(',')) if A.crop else None
    frames = [transform(read_png(p), A.scale, crop) for p in A.frames]
    if A.out.lower().endswith('.png'): write_png(A.out, frames[0])
    else: write_gif(A.out, frames, A.delay)

if __name__ == '__main__':
    main()
