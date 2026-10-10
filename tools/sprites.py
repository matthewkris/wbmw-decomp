"""Animation scripts and sprite mappings: list them and render frames to PNG.

Usage: sprites.py ROM --list
       sprites.py ROM --set S [--group G] --area A --room R [--base T] --png out.png
       sprites.py ROM --enemy TYPE --area A --room R --png out.png
       sprites.py ROM --area A --room R --room-png out.png    (room with its enemies)
       sprites.py ROM --player [--group G] [--sword N] [--shield N] --png out.png

anim_data ($A0000) is three levels of signed word offsets, all relative to anim_data and each
indexed by a byte offset: set (-$3D00 of an entity), group (-$3CFF), animation (-$3CFE). The
tables at each level are packed back to back. An animation script ($878 in UpdateAnim) is a
list of frames (duration byte < $F0, then a signed word offset from anim_data to a mapping) and
commands $F0-$FF. A mapping ($54F2) is a list of 5-byte pieces: size byte (negative ends), tile
word, X offset, Y offset. Frames are drawn with the VRAM contents of a room (its graphics
slots) plus the entity's tile base (-$3DFE).
"""
import sys, os, struct, argparse
sys.path.insert(0, os.path.dirname(__file__))
import gfx, rooms

ANIM = 0xA0000
sw = lambda d, o: struct.unpack_from('>h', d, o)[0]
CMD_ARGS = {0xA: 2, 0xB: 2, 0xC: 2}      # $FA move by (dx, dy), $FB box size, $FC box offset
CMD_NAMES = {0x8: 'flag1', 0x9: 'flag2', 0xA: 'move', 0xB: 'box', 0xC: 'boxpos', 0xD: 'stop',
             0xE: 'mark', 0xF: 'loop'}

def packed_tables(d, starts):
    """tables that sit back to back: each ends at the next start; the last ends where the
    lowest offset any of them holds begins (the next level of data)"""
    ss = sorted(set(starts)); out = {}; low = None
    for i, a in enumerate(ss):
        t = []; o = a
        while o < (ss[i + 1] if i + 1 < len(ss) else low):
            v = sw(d, ANIM + o); t.append(v); o += 2
            low = v if low is None else min(low, v)
        out[a] = t
    return out

def tree(d):
    """{set: {group: {anim: script address}}}, keyed by the byte offsets the game uses"""
    l1 = [sw(d, ANIM + o) for o in range(0, sw(d, ANIM), 2)]
    l2 = packed_tables(d, l1)
    l3 = packed_tables(d, [v for t in l2.values() for v in t])
    out = {}
    for si, o1 in enumerate(l1):
        out[2 * si] = {}
        for gi, o2 in enumerate(l2[o1]):
            out[2 * si][2 * gi] = {2 * ai: ANIM + o3 for ai, o3 in enumerate(l3[o2])}
    return out

def script(d, p, limit=64):
    """frames of an animation: list of (duration, mapping address); stops at stop/loop"""
    frames, cmds = [], []
    for _ in range(limit):
        b = d[p]; p += 1
        if b >= 0xF0:
            c = b & 0xF
            cmds.append(CMD_NAMES.get(c, 'nop'))
            p += CMD_ARGS.get(c, 0)
            if c in (0xD, 0xF): break
            continue
        frames.append((b, ANIM + sw(d, p))); p += 2
        if b == 0: break
    return frames, cmds

def mapping(d, p):
    """pieces of a mapping: (width, height in tiles, tile word, x, y)"""
    out = []
    while d[p] < 0x80:
        s = d[p]
        out.append(((s >> 2 & 3) + 1, (s & 3) + 1, d[p + 1] << 8 | d[p + 2],
                    d[p + 3] - 256 if d[p + 3] & 0x80 else d[p + 3],
                    d[p + 4] - 256 if d[p + 4] & 0x80 else d[p + 4]))
        p += 5
        if len(out) > 80: raise ValueError('mapping at %06X does not end' % p)
    return out

ENEMIES = 0x1BC0E

def enemy(d, t):
    """enemy type word t (category byte, id byte) -> (anim set, group, graphics block ids).
    $125A6/$126AA: record n = id/2 is 5 bytes at the category's table: flag, word offset of the
    graphics list (from $1BC0E), anim set, anim group. Category 2 is laid out differently."""
    cat, i = t >> 8, t & 0xFF
    if cat == 2: raise ValueError('category 2 records are not decoded yet')
    r = ENEMIES + sw(d, ENEMIES + cat) + 5 * (i >> 1)
    g = ENEMIES + struct.unpack_from('>H', d, r + 1)[0]
    n = struct.unpack_from('>H', d, g)[0]
    return d[r + 3], d[r + 4], [struct.unpack_from('>H', d, g + 2 + 2 * k)[0] for k in range(n)]

# placement script ($12848): spawner byte 00-0F -> bytes before the common (type, x, y) tail,
# or None when the spawner has no tail (its own layout, skipped by length)
SPAWN_ARGS = {0: 1, 1: 3, 2: 3, 3: 5, 4: 3, 5: 3, 7: 1, 8: 1, 9: 1, 0xB: 7, 0xC: 1, 0xF: 7}

def placements(d, p):
    """enemy list of a room (room command 08): the type words it loads graphics for
    ($12640, until a negative word) and the spawns [(spawner, type, x, y)] in room pixels.
    Conditional modifiers ($F8-$FD) are read as not taken; $FE jumps."""
    types = []
    while struct.unpack_from('>h', d, p)[0] >= 0:
        types.append(struct.unpack_from('>H', d, p)[0]); p += 2
    p += 2
    spawns = []
    for _ in range(200):
        b = d[p]; p += 1
        if b == 0xFF: break
        if b >= 0xF8:
            k = b & 7
            if k == 6: p += 1; p += struct.unpack_from('>h', d, p)[0]
            else: p += 3 if k in (0, 1, 4) else 1
            continue
        if b == 6: p += 1 + 6 * d[p]; continue
        if b == 0xA: p += 3; continue
        if b == 0xD: n = d[p + 1]; p += 3 + 2 * n
        elif b == 0xE: p += 5; continue
        else: p += SPAWN_ARGS[b]
        t, x, y = struct.unpack_from('>HHH', d, p); p += 6
        spawns.append((b, t, x - 0x1000, y - 0x1000))
    return types, spawns

def room_with_enemies(d, area, room, T):
    """room render with each spawned enemy's first frame drawn at its spawn point"""
    img, rscript = rooms.render(d, area, room)
    lists = [a[0] for c, a in rscript if c == 'enemies']
    if not lists: return img
    types, spawns = placements(d, lists[0])
    base, used, extra = {}, 0, []
    for t in types:                            # $125C4: each type's graphics below slot 48
        try: s_, g_, ids = enemy(d, t)
        except ValueError: continue
        used += len(ids); base[t] = (48 - used) * 32
        extra.append((48 - used, ids))
    tile, colours = vram(d, area, room)
    for first, ids in extra:
        t0 = tile                              # chain the extra slots in front of the room's
        def tile(n, first=first, ids=ids, t0=t0):
            s = n >> 5
            if first <= s < first + len(ids):
                b = ids[s - first]
                key = ('gfx', b)
                if key not in _cache: _cache[key] = gfx.decode(d, struct.unpack_from('>I', d, gfx.TABLE + 4 * b)[0])[0]
                return _cache[key][(n & 31) * 32:(n & 31) * 32 + 32]
            return t0(n)
    for sp, t, x, y in spawns:
        if t not in base: continue
        s_, g_, _ = enemy(d, t)
        anims = T.get(s_, {}).get(g_, {})
        if not anims: continue
        fr, _ = script(d, anims[min(anims)])
        if fr: draw(img, x, y, mapping(d, fr[0][1]), tile, colours, base[t])
    return img

_cache = {}

def player_vram(d, area, room, sword=0, shield=0):
    """VRAM with Shion's graphics as the equipment loaders build it: body slots 8-11 ($F6E0, the
    default of $F698) and slot 12 ($F6DC), the sword's slot 13 ($F6F8, table $F710 indexed by
    $FF95CE), and 32 raw shield tiles copied to tile $5C ($F742: tile number from $F75E indexed
    by $FF95D0, copied by CopyRawTiles linearly from that tile's place in the ROM)."""
    tile, colours = vram(d, area, room)
    slots = {}
    tmp = [0xFFFF] * 64
    for lst in (0xF6E0, 0xF6DC, 0xF710 + struct.unpack_from('>H', d, 0xF710 + 2 * sword)[0]):
        rooms.gfx_slots(d, lst, tmp)
    for i in (8, 9, 10, 11, 12, 13): slots[i] = tmp[i]
    t = struct.unpack_from('>H', d, 0xF75E + 2 * shield)[0]
    src = struct.unpack_from('>I', d, gfx.TABLE + 4 * (t >> 5))[0] & 0xFFFFFF
    raw = d[src + (t & 31) * 32: src + (t & 31) * 32 + 32 * 32]
    def ptile(n):
        if 0x5C <= n < 0x7C: return raw[(n - 0x5C) * 32:(n - 0x5C) * 32 + 32]
        if (n >> 5) in slots:
            b = slots[n >> 5]
            if ('gfx', b) not in _cache: _cache[('gfx', b)] = gfx.decode(d, struct.unpack_from('>I', d, gfx.TABLE + 4 * b)[0])[0]
            return _cache[('gfx', b)][(n & 31) * 32:(n & 31) * 32 + 32]
        return tile(n)
    return ptile, colours

def vram(d, area, room, extra=None):
    """tile fetcher for the VRAM a room sets up (graphics slots), and its palette lines.
    extra = (first slot, block ids) places more graphics, as the enemy loader does."""
    slots = [0xFFFF] * 64
    rooms.gfx_slots(d, 0x96BC, slots)
    pals = [0, 1, 0, 0]
    for cmd, a in rooms.room_script(d, area, room):
        if cmd == 'gfx_slots': rooms.gfx_slots(d, a[0], slots)
        elif cmd == 'palette': pals[2] = a[0]
        elif cmd == 'palette3': pals[3] = a[0]
    if extra:
        for k, b in enumerate(extra[1]): slots[extra[0] + k] = b
    cache = {}
    def tile(n):
        s = slots[n >> 5 & 63]
        if s == 0xFFFF or s & 0x8000: return None
        if s not in cache: cache[s] = gfx.decode(d, struct.unpack_from('>I', d, gfx.TABLE + 4 * s)[0])[0]
        return cache[s][(n & 31) * 32:(n & 31) * 32 + 32]
    return tile, [gfx.rgb(w) for p in pals for w in gfx.palette(d, p)]

def draw(img, ox, oy, pieces, tile, colours, base=0):
    """sprite pieces at origin (ox, oy); a piece's tiles run down columns, as on the VDP"""
    H, W = len(img), len(img[0])
    for w, h, tw, x, y in pieces:
        tw = (tw + base) & 0xFFFF
        hf, vf = tw & 0x800, tw & 0x1000
        for c in range(w):
            for r in range(h):
                t = tile((tw & 0x7FF) + c * h + r)
                if t is None: continue
                cx = (w - 1 - c) if hf else c
                cy = (h - 1 - r) if vf else r
                for py in range(8):
                    sy = 7 - py if vf else py
                    for px in range(8):
                        sx = 7 - px if hf else px
                        v = t[sy * 4 + sx // 2] >> (0 if sx & 1 else 4) & 15
                        X, Y = ox + x + cx * 8 + px, oy + y + cy * 8 + py
                        if v and 0 <= X < W and 0 <= Y < H: img[Y][X] = colours[(tw >> 13 & 3) * 16 + v]

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('--list', action='store_true')
    ap.add_argument('--set', type=lambda x: int(x, 0)); ap.add_argument('--group', type=lambda x: int(x, 0))
    ap.add_argument('--area', type=int, default=1); ap.add_argument('--room', type=int, default=0)
    ap.add_argument('--base', type=lambda x: int(x, 0), default=0); ap.add_argument('--png')
    ap.add_argument('--enemy', type=lambda x: int(x, 0)); ap.add_argument('--room-png')
    ap.add_argument('--all-rooms'); ap.add_argument('--player', action='store_true'); ap.add_argument('--sword', type=int, default=0); ap.add_argument('--shield', type=int, default=0)
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    T = tree(d)
    if A.list:
        bad = nframes = 0
        for s, groups in T.items():
            n = sum(len(g) for g in groups.values())
            print('set %02X: %d groups, %d animations' % (s, len(groups), n))
            for g, anims in groups.items():
                for a, p in anims.items():
                    try:
                        fr, _ = script(d, p)
                        for _, m in fr: mapping(d, m); nframes += 1
                    except Exception as e:
                        print('  set %02X group %02X anim %02X: %s' % (s, g, a, e)); bad += 1
        print('frames: %d  problems: %d' % (nframes, bad)); sys.exit(1 if bad else 0)
    if A.all_rooms:
        os.makedirs(A.all_rooms, exist_ok=True); bad = 0; n = 0
        for area in range(19):
            for room in range(rooms.room_count(d, area)):
                try:
                    rooms.write(os.path.join(A.all_rooms, 'area%02d_room%02d.png' % (area, room)),
                                room_with_enemies(d, area, room, T))
                except Exception as e:
                    print('area %d room %d: %s' % (area, room, e)); bad += 1
        print('problems:', bad); sys.exit(1 if bad else 0)
    if A.room_png:
        rooms.write(A.room_png, room_with_enemies(d, A.area, A.room, T)); sys.exit(0)
    extra = None
    if A.enemy is not None:                    # as $125C4: blocks go just below slot 48
        A.set, A.group, ids = enemy(d, A.enemy)
        extra = (48 - len(ids), ids); A.base = (48 - len(ids)) * 32
        print('enemy %04X: set %02X group %02X, graphics %s, tile base %X' % (A.enemy, A.set, A.group, ids, A.base))
    if A.player:
        A.set = 0 if A.set is None else A.set
        tile, colours = player_vram(d, A.area, A.room, A.sword, A.shield)
    else:
        tile, colours = vram(d, A.area, A.room, extra)
    groups = {A.group: T[A.set][A.group]} if A.group is not None else T[A.set]
    rows = [(g, a, script(d, p)[0]) for g, anims in groups.items() for a, p in anims.items()]
    cols = max(len(fr) for _, _, fr in rows)
    CW, CH = 96, 96
    img = [[(32, 32, 48)] * (cols * CW) for _ in range(len(rows) * CH)]
    for ri, (g, a, fr) in enumerate(rows):
        for ci, (dur, m) in enumerate(fr):
            draw(img, ci * CW + CW // 2, ri * CH + CH * 3 // 4, mapping(d, m), tile, colours, A.base)
    rooms.write(A.png, img)
