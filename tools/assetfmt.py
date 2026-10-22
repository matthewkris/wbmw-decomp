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

# ---- the lists the room scripts point at besides the maps: event lists (command 5), spawn
# lists (8), graphics slot lists (2), tile animations (4), cell animations (11), and the data
# their items point at. Each is read item by item as its interpreter reads it; a list with jumps
# goes on past an end while a jump points further on. An item keeps its code and its arguments
# by type: 'b' a byte, 'w' a word, 'j' a jump (a word: its target is its place plus it), 'o' an
# offset to data elsewhere (a word, the same way; 0 none), 'l' a long, 'n' a count byte. ----

def _args(d, p, fmt):
    vals, refs = [], []
    for f in fmt:
        if f in 'bn': vals.append(d[p]); p += 1
        elif f == 'w': vals.append(be16(d, p)); p += 2
        elif f in 'jo':
            v = sw(d, p); vals.append(v)
            if f == 'j' or v: refs.append((f, p + v))
            p += 2
        elif f == 'l': vals.append(be32(d, p)); p += 4
    return vals, refs, p

def _put(fmt, vals):
    out = bytearray()
    for f, v in zip(fmt, vals):
        if f in 'bn': out.append(v)
        elif f == 'l': out += struct.pack('>I', v)
        else: out += w16(v)
    return bytes(out)

def flow_parse(d, start, step, limit=4096):
    """a list read by step(d, p) -> (item, next p, refs, ends); it goes on past an end while a
    jump targets further on. refs: ('j', target) jumps, (kind, address) data elsewhere"""
    p, items, reach, data = start, [], start, []
    while p < start + limit:
        item, q, refs, ends = step(d, p)
        items.append(item)
        for f, t in refs:
            if f == 'j': reach = max(reach, t)
            else: data.append((f, t))
        p = q
        if ends and reach < p: break
    return items, p, data

def flow_encode(items, fmt):
    """fmt(item) -> its arguments' format"""
    out = bytearray()
    for it in items:
        if 'words' in it: out += b''.join(w16(v) for v in it['words']); continue
        out.append(it['code']); out += _put(fmt(it), it['args'])
    return bytes(out)

# the room's event list ($996C: $ABBE as it loads, $A3D8 each frame)
EVENT_SIZES = [5, 5, 5, 7, 2, 3, 4, 3, 3, 4, 2, 0, 7, 6, 1, 0]          # $A480, by the event's low nibble
EVENT_CMDS = {0xE0: 'b', 0xE2: 'b', 0xE3: 'b', 0xE8: 'bbbbbbbbb', 0xE9: 'o', 0xEA: 'bbbbbb', 0xEB: 'bbbbw',
              0xEC: 'bbo', 0xF6: 'bj', 0xF7: 'j', 0xF8: 'bj', 0xF9: 'bj', 0xFA: 'b', 0xFB: 'b', 0xFC: 'bj', 0xFE: 'j'}

def event_fmt(it):
    c = it['code']
    if c < 0xC0: return 'bbbbbo' if c & 0xF == 12 else 'b' * EVENT_SIZES[c & 0xF]   # 12: an item list at its offset
    if c == 0xFD: return 'bbj' if len(it['args']) == 3 else 'bbbj'                   # (its kind negative: no flag byte)
    return EVENT_CMDS.get(c, '')

def _event_step(d, p):
    c = d[p]
    fmt = ('bbj' if d[p + 2] & 0x80 else 'bbbj') if c == 0xFD else event_fmt(dict(code=c, args=[]))
    vals, refs, q = _args(d, p + 1, fmt)
    kind = 'items' if c < 0xC0 else 'cells' if c == 0xE9 else 'picture' if c == 0xEC else None
    refs = [(f if f == 'j' else kind, t) for f, t in refs]
    return dict(code=c, args=vals), q, refs, c in (0xFF, 0xFE)

def event_list_parse(d, a):
    return flow_parse(d, a, _event_step)

# what event lists point at: a cell list ($27FA: cell, x, y words to a negative word), a
# picture ($2856: width, height, then the cells), an item list (bytes to $FF: $29B4)
def cell_list_parse(d, a):
    words, p = [], a
    while True:
        w = be16(d, p); words.append(w); p += 2
        if w & 0x8000: return words, p
        words += [be16(d, p), be16(d, p + 2)]; p += 4

def picture_parse(d, a):
    w, h = d[a], d[a + 1]
    return dict(width=w, height=h, cells=[be16(d, a + 2 + 2 * i) for i in range(w * h)]), a + 2 + 2 * w * h

def picture_encode(pic):
    return bytes([pic['width'], pic['height']]) + b''.join(w16(v) for v in pic['cells'])

def item_list_parse(d, a):
    p = a
    while d[p] != 0xFF: p += 1
    return list(d[a:p + 1]), p + 1

# the room's spawn list ($A11E: $12848): the kinds to load (words to a negative one, not in a boss
# room), then spawn points (a kind 0-15 and its fields) and commands ($F8-$FE), $FF the end
PLACE = 'www'                                       # its enemy's kind, x, y
SPAWN_KINDS = {0: 'b' + PLACE, 1: 'bbb' + PLACE, 2: 'bbb' + PLACE, 3: 'bbbbb' + PLACE, 4: 'bw' + PLACE,
               5: 'bo' + PLACE, 7: 'b' + PLACE, 8: 'b' + PLACE, 9: 'b' + PLACE, 10: 'bw', 11: 'bbbl' + PLACE,
               12: 'b' + PLACE, 14: 'bow', 15: 'bwwbb' + PLACE}
SPAWN_CMDS = {0xF8: 'bj', 0xF9: 'bj', 0xFA: 'b', 0xFB: 'b', 0xFC: 'bj', 0xFD: 'b', 0xFE: 'bj'}

def spawn_fmt(it):
    c = it['code']
    if c == 6: return 'n' + 'b' * (6 * it['args'][0])                   # the chests: n records of 6 bytes
    if c == 13: return 'bnb' + 'w' * it['args'][1] + PLACE                # a group: n words
    return SPAWN_KINDS.get(c) or SPAWN_CMDS.get(c, '')

def _spawn_step(d, p):
    c = d[p]
    if c == 6: fmt = 'n' + 'b' * (6 * d[p + 1])
    elif c == 13: fmt = 'bnb' + 'w' * d[p + 2] + PLACE
    else: fmt = spawn_fmt(dict(code=c, args=[]))
    vals, refs, q = _args(d, p + 1, fmt)
    refs = [(f if f == 'j' else 'walk' if c == 5 else 'waves', t) for f, t in refs]
    return dict(code=c, args=vals), q, refs, c in (0xFF, 0xFE)

def spawn_list_parse(d, a, kinds=True):
    p, words = a, []
    if kinds:
        while True:
            w = be16(d, p); words.append(w); p += 2
            if w & 0x8000: break
    items, end, data = flow_parse(d, p, _spawn_step)
    return ([dict(words=words)] if kinds else []) + items, end, data

# a townsperson's walk script (handler 11, $16598; a spawn point of kind 5 points at it): 0 wait
# (frames), 1 walk (a signed distance), 2 walk home, 3 speak (a word), 4, 5 jump (a word from its
# own place); it ends with a jump back
WALK_FMT = {0: 'b', 1: 'b', 2: '', 3: 'bb', 4: '', 5: 'j'}

def _walk_step(d, p):
    c = d[p]
    if c not in WALK_FMT: raise ValueError('walk script command %d at $%06X' % (c, p))
    vals, refs, q = _args(d, p + 1, WALK_FMT[c])
    return dict(code=c, args=vals), q, refs, c == 5

def walk_script_parse(d, a):
    items, end, _ = flow_parse(d, a, _walk_step)
    return items, end

def walk_script_encode(items):
    return flow_encode(items, lambda it: WALK_FMT[it['code']])

# a graphics slot list ($234C, rooms.gfx_slots): $80-$BF a slot, $C0 the next, $C1-$F7 a run of
# blocks (a word), $F8-$FE empty slots, under $80 a block (two bytes), $FF the end
def slots_parse(d, a):
    items, p = [], a
    while True:
        b = d[p]
        if b < 0x80: items.append(dict(block=be16(d, p))); p += 2; continue
        if 0xC1 <= b < 0xF8: items.append(dict(code=b, args=[be16(d, p + 1)])); p += 3; continue
        items.append(dict(code=b, args=[])); p += 1
        if b == 0xFF: return items, p

def slots_encode(items):
    out = bytearray()
    for it in items:
        if 'block' in it: out += w16(it['block'])
        else: out.append(it['code']); out += b''.join(w16(v) for v in it['args'])
    return bytes(out)

# a tile animation ($23A6, run by $6EAC): its VRAM word, then words: 0 the end, $8000 again from
# the start, $80xx a jump (an offset word; $8001 only once), else a frame (count and time, tile)
def tile_anim_parse(d, a):
    words, p, reach = [be16(d, a)], a + 2, a
    while True:
        w = be16(d, p); words.append(w); p += 2
        if w == 0 or w == 0x8000:
            if reach < p: return words, p
            continue
        words.append(be16(d, p))
        if w & 0x8000: reach = max(reach, p + sw(d, p))
        p += 2
        if w & 0x8000 and w != 0x8001 and reach < p: return words, p

# a cell animation ($2974, run by $28AC): 0 the end, $FF the frame's end, $FD a flag, $FE a jump,
# $FA a count, $FB back to it, $FC the plane swapped, else a frame: its time, then (cell, dx, dy)
# to a negative word
def cell_anim_parse(d, a):
    p, reach = a, a
    while True:
        k = d[p]; p += 1
        if k == 0:
            if reach < p: return list(d[a:p]), p
            continue
        if k == 0xFE:
            reach = max(reach, p + sw(d, p)); p += 2
            if reach < p: return list(d[a:p]), p
            continue
        if k == 0xFA: p += 1; continue
        if k >= 0xFB: continue
        while not d[p] & 0x80: p += 4
        p += 2
