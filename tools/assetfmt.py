"""The extracted data's formats, each as a parse (ROM bytes -> plain data for JSON) and an encode
(plain data -> the same bytes) pair, so that tools/rebuild.py can check that the files written by
tools/extract.py hold everything the ROM does (docs/EXTRACT.md). The plain forms keep every byte
that matters: a pointer's form (long or offset), a terminator's value, a command's arguments.
"""
import struct
import rooms, sprites, text

be16 = lambda d, o: struct.unpack_from('>H', d, o)[0]
be32 = lambda d, o: struct.unpack_from('>I', d, o)[0]
sw = lambda d, o: struct.unpack_from('>h', d, o)[0]
s8 = lambda v: v - 256 if v & 0x80 else v
w16 = lambda v: struct.pack('>H', v & 0xFFFF)

# ---- room scripts ($93C4): a command byte and its arguments, until $FF ----

def room_script_parse(d, area, room):
    """(address, [items]); an item: dict(code, args) with args typed so they encode back"""
    a1 = rooms.area_base(d, area)
    start = p = a1 + rooms.sw(d, a1 + 2 * room)
    items = []
    while True:
        c = d[p]; p += 1
        if c == 0xFF: return start, items, p
        lay = rooms.CMDS[c]
        if lay in ('layer', 'layerL'):
            it = dict(code=c, kind=d[p], w=d[p + 1], h=d[p + 2], z=be16(d, p + 3)); p += 5
            if lay == 'layer': it['offset'] = sw(d, p); p += 2
            else: it['long'] = be32(d, p); p += 4
            items.append(it); continue
        args = []
        if lay.startswith('x'):
            n = int(lay[1:]); args.append(dict(hex=d[p:p + n].hex())); p += n
        else:
            for f in lay:
                if f == 'P':
                    if d[p]: args.append(dict(flag=d[p], long=be32(d, p + 1))); p += 5
                    else: args.append(dict(flag=0, offset=sw(d, p + 1))); p += 3
                elif f == 'b': args.append(dict(b=d[p])); p += 1
                elif f == 'w': args.append(dict(w=be16(d, p))); p += 2
        items.append(dict(code=c, args=args))

def room_script_encode(items):
    out = bytearray()
    for it in items:
        out.append(it['code'])
        if 'kind' in it:
            out += bytes([it['kind'], it['w'], it['h']]) + w16(it['z'])
            out += w16(it['offset']) if 'offset' in it else struct.pack('>I', it['long'])
            continue
        for a in it['args']:
            if 'hex' in a: out += bytes.fromhex(a['hex'])
            elif 'flag' in a:
                out.append(a['flag'])
                out += struct.pack('>I', a['long']) if a['flag'] else w16(a['offset'])
            elif 'b' in a: out.append(a['b'])
            else: out += w16(a['w'])
    return bytes(out + b'\xff')

# ---- animation scripts ($878): frames (duration < $F0, signed word offset from $A0000 to a
# mapping) and commands $F0-$FF; a 0 duration or a stop ($FD) or loop ($FF) command ends it ----

def anim_parse(d, p):
    start, items = p, []
    for _ in range(256):
        b = d[p]; p += 1
        if b >= 0xF0:
            n = sprites.CMD_ARGS.get(b & 0xF, 0)
            items.append(dict(cmd=b, args=list(d[p:p + n]))); p += n
            if b & 0xF in (0xD, 0xF): break
            continue
        items.append(dict(duration=b, mapping=sw(d, p))); p += 2
        if b == 0: break
    return start, items, p

def anim_encode(items):
    out = bytearray()
    for it in items:
        if 'cmd' in it: out.append(it['cmd']); out += bytes(it['args'])
        else: out.append(it['duration']); out += w16(it['mapping'])
    return bytes(out)

# ---- sprite mappings ($54F2): 5-byte pieces (size byte, tile word, x, y) until a size byte
# with bit 7 set ----

def mapping_parse(d, p):
    start, pieces = p, []
    while d[p] < 0x80:
        pieces.append(dict(size=d[p], tile=be16(d, p + 1), x=s8(d[p + 3]), y=s8(d[p + 4]))); p += 5
    return start, dict(pieces=pieces, end=d[p]), p + 1

def mapping_encode(m):
    out = bytearray()
    for q in m['pieces']: out += bytes([q['size']]) + w16(q['tile']) + bytes([q['x'] & 255, q['y'] & 255])
    return bytes(out + bytes([m['end']]))

# ---- palette records ($599C, $1370): a word of high blue bits (LSB first) and 15 bytes BBGGGRRR ----

def palette_encode(words):
    hi, body = 0, bytearray()
    for i, w in enumerate(words[1:16]):
        body.append((w >> 1 & 7) | (w >> 5 & 7) << 3 | (w >> 9 & 3) << 6)
        hi |= (w >> 11 & 1) << i
    return w16(hi) + bytes(body)

# ---- text: the parsed form of tools/text.py back to bytes ----

def text_encode(parsed):
    out = bytearray()
    for it in parsed:
        kind = it[0]
        if kind == 'char': out += bytes(it[1:])
        elif kind == 'phrase': out += bytes([0x0C | it[1], it[2]])
        elif kind == 'code':
            b, vals = it[1], it[2]
            out.append(b)
            if b == 6: out += w16(vals[0])
            else: out += bytes(vals)
        else:                                       # ('cmd', number, values)
            c, vals = it[1], it[2]
            out += bytes([11, c])
            fmt = text.COMMANDS.get(c, text.COMMANDS[50])[1]
            v = iter(vals)
            for f in fmt:
                if f == 'b': out.append(next(v))
                elif f == 'j': out += w16(next(v))
                elif f == 'Q':
                    for _ in range(text.QUIZ_JUMPS): out += w16(next(v))
    return bytes(out)

# ---- where the rooms' compressed data ends, for counting what the checks cover ----

def blocks_end(d, a3):
    """the end of a block table ($9F6A): the nibbles, then each block's bytes"""
    n = be16(d, a3); a3 += 2
    if n == 0: return a3
    a2 = a3 + (n + 1) // 2
    for i in range(n):
        code = d[a3 + i // 2] >> (0 if i & 1 else 4) & 0xF
        if code == 0: continue
        m = code ^ 0xF
        a2 += (0 if m & 8 else 1) + 2 + {0: 6, 1: 0, 2: 2, 3: 3}.get(m & 7, 3)
    return a2

def chunk_end(d, p):
    """the end of a chunk's data ($A0F4): the bytes it reads from the ROM, up to a zero-run
    stream (the rest is read from what that stream expands to)"""
    left, q = 192, p
    c = d[q]; q += 1
    if c & 0x80:                                    # $A1C4: a count, a mask's length, the mask, the bytes kept
        total, nmask = d[q], d[q + 1]
        zeros = sum(bin(d[q + 2 + i]).count('1') for i in range(nmask))
        return q + 2 + nmask + total - zeros
    filled = [False] * 192
    for _ in range(c):
        q += 2; nb = 0
        for i in range(192):
            if filled[i]: continue
            if nb == 0: bits = d[q]; q += 1; nb = 8
            nb -= 1
            if bits & 1: filled[i] = True; left -= 1
            bits >>= 1
    return q + (left * 10 + 7) // 8
