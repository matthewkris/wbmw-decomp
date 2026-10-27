"""The rooms' scripts and lists laid out (docs/MODDING.md, "Levels"): each where it was, but those
that have to move (they grew into their neighbours: pack.py finds them), which go past the end of
the ROM's data, after the text.

* A list that moves (an event list, a spawn list, a graphics slot list, a tile animation) takes
  with it a copy of the data its items point at by an offset (an item list, a picture, a cell list,
  a walk script), since an offset reaches only 32 KB. A piece of such data that grew makes every
  list pointing at it move.
* A room script points at its lists by an offset from its area's block, or by an address (a flag
  byte says which; its layers the same, commands $00/$01 or $10/$11), and its area's block holds
  the offsets of its rooms' scripts. So an area with a room script that grew, or that points at a
  list that moved, moves whole: its block (the offsets) and all its rooms' scripts, every pointer
  in them an address, and the data directory's pointer to the block with it.
* The houses' pictures point at their spawn and event lists and pictures by offsets from their
  table: when one of those has to move, the table moves with a copy of each (place_houses). The
  ending's scenes point at their slot lists by address, which follows.

moves: dict(lists={'KIND/ADDRESS'}, children={'KIND/ADDRESS'}, areas={area}).
"""
import struct
import assetfmt, rooms

TOP = ('events', 'spawns', 'slots', 'tile_anims', 'cell_anims')
CHILD = ('item_lists', 'cell_lists', 'pictures', 'walk_scripts')
FMT = {'events': assetfmt.event_fmt, 'spawns': assetfmt.spawn_fmt,
       'walk_scripts': lambda it: assetfmt.WALK_FMT[it['code']]}

class LevelError(Exception): pass

def encode(kind, v, at, where=None):
    """a list's bytes at address at; where: KIND/ADDRESS -> where that data is now"""
    if kind == 'events': return assetfmt.flow_encode([assetfmt.event_raw(it) for it in v['items']], FMT[kind], at, where)
    if kind in FMT: return assetfmt.flow_encode(v['items'], FMT[kind], at, where)
    if kind == 'slots': return assetfmt.slots_encode(v['items'])
    if kind in ('tile_anims', 'cell_lists'): return b''.join(struct.pack('>H', w) for w in v['words'])
    if kind == 'cell_anims': return bytes(v['bytes'])
    if kind == 'pictures': return assetfmt.picture_encode(v)
    if kind == 'item_lists': return bytes(v['bytes'])
    raise LevelError('no list kind %s' % kind)

def children(v):
    """the data a list's items point at: KIND/ADDRESS keys, in order"""
    out = []
    for it in v.get('items', []):                   # in args, or in a named event's fields
        for a in list(it.get('args', [])) + [x for k, x in it.items() if k not in ('args', 'label')]:
            if isinstance(a, dict) and 'o' in a and a['o'] not in out: out.append(a['o'])
    return out

def parents_of(LS):
    """KIND/ADDRESS of a piece of data -> the lists pointing at it"""
    out = {}
    for kind in TOP:
        for a, v in LS[kind].items():
            for c in children(v): out.setdefault(c, []).append('%s/%s' % (kind, a))
    return out

def place_lists(LS, moves, at):
    """the lists' pieces by kind name, the new address of each list that moved (by its old one),
    and where the extension ends"""
    pieces = {k: [] for k in TOP + CHILD}
    moved = {}
    keep_children = set()
    for kind in TOP:
        for a, v in LS[kind].items():
            key = '%s/%s' % (kind, a)
            if key in moves['lists']: continue
            keep_children.update(children(v))
            pieces[kind].append((int(a, 16), encode(kind, v, int(a, 16))))
    parents = parents_of(LS)
    for kind in CHILD:                              # in place while a list there points at it (or none does)
        for a, v in LS[kind].items():
            key = '%s/%s' % (kind, a)
            if key in keep_children and key in moves['children']:
                raise LevelError('%s grew, but a list that cannot move points at it' % key)
            if key in keep_children or key not in parents and key not in moves['children']:
                pieces[kind].append((int(a, 16), encode(kind, v, int(a, 16))))
    for kind in TOP:                                # those that move, each with its own copy of its data
        for a, v in LS[kind].items():
            key = '%s/%s' % (kind, a)
            if key not in moves['lists']: continue
            size = len(encode(kind, v, at, {c: at for c in children(v)}))
            where, q = {}, at + size
            for c in children(v):
                ck, ca = c.split('/')
                if ck not in LS or ca not in LS[ck]: raise LevelError('%s points at %s, which lists.json does not have' % (key, c))
                where[c] = q; q += len(encode(ck, LS[ck][ca], q))
                q += q & 1
            pieces[kind].append((at, encode(kind, v, at, where)))
            for c, ca in where.items():
                ck, cs = c.split('/'); pieces[ck].append((ca, encode(ck, LS[ck][cs], ca)))
            moved[int(a, 16)] = at
            at = q + (q & 1)
    return pieces, moved, at

def place_rooms(rms, dirs, moved, moves, at):
    """the room scripts' pieces and the areas' blocks; dirs (the data directory) changed for the
    areas that moved. Returns (scripts, blocks, where the extension ends)"""
    targets = lambda r: [pointer(r, it, a, dirs) for it in r['items'] for a in it.get('args', []) if 'flag' in a]
    areas = set(moves['areas']) | {r['area'] for r in rms if any(t in moved for t in targets(r))}
    moves['placed_areas'] = sorted(areas)
    scripts, blocks = [], []
    for area in range(19):
        rs = sorted((r for r in rms if r['area'] == area), key=lambda r: r['room'])
        base = dirs[8 + area] & 0xFFFFFF
        if area not in areas:
            for r in rs: scripts.append((int(r['address'], 16), assetfmt.room_script_encode(r['items'])))
            blocks.append((base, b''.join(struct.pack('>h', int(r['address'], 16) - base) for r in rs)))
            continue
        new_base = at
        p = at + 2 * len(rs)
        offs = []
        for r in rs:
            items = []
            for it in r['items']:
                if 'kind' in it:                    # a layer: its map by address ($10/$11)
                    src = it['long'] & 0xFFFFFF if 'long' in it else base + it['offset']
                    items.append(dict((k, v) for k, v in it.items() if k not in ('offset', 'long')) | dict(code=it['code'] | 0x10, long=src))
                    continue
                args = []
                for a in it.get('args', []):
                    if 'flag' in a:
                        t = pointer(r, it, a, dirs)
                        args.append(dict(flag=1, long=moved.get(t, t)))
                    else: args.append(a)
                items.append(dict(it, args=args) if 'args' in it else it)
            b = assetfmt.room_script_encode(items)
            offs.append(p - new_base); scripts.append((p, b)); p += len(b)
        if p - new_base >= 32768: raise LevelError('area %d is too large (%d bytes)' % (area, p - new_base))
        blocks.append((new_base, b''.join(struct.pack('>h', o) for o in offs)))
        dirs[8 + area] = dirs[8 + area] & 0xFF000000 | new_base
        at = p + (p & 1)
    return scripts, blocks, at

HOUSE_FIELDS = (('spawns', 8), ('events', 12), ('pictures', 14))   # a record's offsets from the table

def place_houses(hp, LS, moves, dirs, at):
    """the houses' and shops' pictures (directory table 2): records of 16 bytes pointing at their
    spawn list, event list and picture by offsets from the table. In place; but when one of those
    lists has to move, the table moves too, past the ROM's data, with a copy of every list its
    records point at (and of what those point at), all within the offsets' reach; the data
    directory's pointer follows. Returns (records, the copies by kind, where the extension ends)"""
    key = lambda kind, a: '%s/%s' % (kind, a)
    rec_keys = [{kind: key(kind, x[{'pictures': 'picture'}.get(kind, kind)]) for kind, _ in HOUSE_FIELDS
                 if x[{'pictures': 'picture'}.get(kind, kind)]} for x in hp['records']]
    if not any(k in moves['lists'] or k in moves['children'] for ks in rec_keys for k in ks.values()):
        return [(int(x['address'], 16), bytes.fromhex(x['raw'])) for x in hp['records']], {}, at
    table, n = at, len(hp['records'])
    q = at + 16 * n
    where, copies = {}, {}
    def put(k):
        nonlocal q
        if k in where: return
        kind, a = k.split('/')
        if kind not in LS or a not in LS[kind]: raise LevelError('a house picture points at %s, which lists.json does not have' % k)
        v = LS[kind][a]
        where[k] = q
        sub = {}
        size = len(encode(kind, v, q, {c: q for c in children(v)}))
        p = q + size; p += p & 1
        for c in children(v):                       # what the list points at, after it
            ck, ca = c.split('/')
            sub[c] = p; p += len(encode(ck, LS[ck][ca], p)); p += p & 1
        copies.setdefault(kind, []).append((q, encode(kind, v, q, sub)))
        for c, ca in sub.items():
            ck, cs = c.split('/'); copies.setdefault(ck, []).append((ca, encode(ck, LS[ck][cs], ca)))
        q = p
    records = []
    for x, ks in zip(hp['records'], rec_keys):
        raw = bytearray.fromhex(x['raw'])
        for kind, off in HOUSE_FIELDS:
            if kind in ks:
                put(ks[kind])
                o = where[ks[kind]] - table
                if o >= 32768: raise LevelError('the houses\' lists are too large to keep within reach of their table')
                raw[off:off + 2] = struct.pack('>h', o)
        records.append((table + 16 * len(records), bytes(raw)))
    dirs[2] = dirs[2] & 0xFF000000 | table
    moves['houses_moved'] = True
    return records, copies, q + (q & 1)

def pointer(r, it, a, dirs):
    return a['long'] & 0xFFFFFF if a['flag'] else (dirs[8 + r['area']] & 0xFFFFFF) + a['offset']
