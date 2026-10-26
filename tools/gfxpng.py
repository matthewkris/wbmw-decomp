"""The graphics blocks as pictures (docs/MODDING.md, "Graphics"): each of the 768 blocks of 32
tiles as an indexed PNG in the colours the game draws it with most: the palette most used with its
tiles over every room's map and the sprites (the enemies', Wonder Boy's). Colour 0 is transparent,
as in the game. Blocks seen nowhere get grey levels. Two layouts, so that what the tiles make shows
whole: a block the maps use most is 16 tiles by 2 rows (128 x 16: a map's 16 x 16 cells are tiles
n, n+1, n+16, n+17); one the sprites use most is 8 columns of 4 tiles going down (64 x 32: a
sprite's piece runs down its columns, up to 4 tiles tall).

extract.py writes them (graphics/png/block_NNN.png, and graphics/png/blocks.json: each block's
palette record and colours); pack.py reads them back, so a changed picture is the game's.
A picture saved as indexed is read by its indices; one an editor saved in full colour is read by
matching its colours with the block's 16 (a colour that is not one of them is an error).
"""
import os, json, struct, collections
import rooms, sprites, gfx, pngio

TILES_ACROSS = 16
GREY = [(i * 17,) * 3 for i in range(16)]

def _slots_and_pals(d, area, room):
    slots = [0xFFFF] * 64
    rooms.gfx_slots(d, 0x96BC, slots)
    pals = [0, 1, 0, 0]
    for cmd, a in rooms.room_script(d, area, room):
        if cmd == 'gfx_slots': rooms.gfx_slots(d, a[0], slots)
        elif cmd == 'palette': pals[2] = a[0]
        elif cmd == 'palette3': pals[3] = a[0]
    return slots, pals

def usage(d, anim_tree):
    """{block: Counter(palette record)}: how often each block's tiles are drawn with each palette"""
    use = collections.defaultdict(collections.Counter)
    by = collections.defaultdict(collections.Counter)  # {block: Counter('map' or 'sprite')}
    shared = [{}, {}, {}]
    tl = rooms.be32(d, rooms.DIR + 4)
    for k in range(3): rooms.blocks(d, rooms.be32(d, tl + 4 * k) & 0xFFFFFF, shared[k])

    def count(slots, pals, tw, n=1, what='map'):
        s = slots[(tw & 0x7FF) >> 5 & 63]
        if s != 0xFFFF and not s & 0x8000: use[s][pals[tw >> 13 & 3]] += n; by[s][what] += n

    seen_types = {}
    for area in range(19):
        for room in range(rooms.room_count(d, area)):
            slots, pals = _slots_and_pals(d, area, room)
            script = rooms.room_script(d, area, room)
            for cmd, a in script:                   # the map: every cell's 4 tiles
                if cmd != 'layer': continue
                cmap, t3 = rooms.layer_map(d, a["src"])
                tabs = shared + [t3]
                for cells in cmap.values():
                    for bw in cells:
                        bdef = tabs[bw >> 8 & 3].get(bw & 0xFF)
                        if bdef: [count(slots, pals, tw) for tw in bdef[0]]
            for cmd, a in script:                   # the enemies the room loads, as extract.py draws them
                if cmd != 'enemies': continue
                types = sprites.placements(d, a[0])[0]
                for t in types: seen_types.setdefault(t, (area, room, sprites.kinds_palette(d, types)))
    for t, (area, room, room_pal3) in seen_types.items():
        try: st, g, ids = sprites.enemy(d, t)
        except ValueError: continue
        slots, pals = _slots_and_pals(d, area, room)
        first = 48 - len(ids)
        for k, b in enumerate(ids): slots[first + k] = b
        pals[3] = sprites.enemy_palette(d, t) or room_pal3 or pals[3]
        for p in anim_tree.get(st, {}).get(g, {}).values():
            for _, m in sprites.script(d, p)[0]:
                for w, h, tw, x, y in sprites.mapping(d, m):
                    tw = (tw + first * 32) & 0xFFFF
                    for k in range(w * h): count(slots, pals, (tw & ~0x7FF) | ((tw + k) & 0x7FF), what='sprite')
    slots, pals = _slots_and_pals(d, 2, 0)          # Wonder Boy, as extract.py draws him
    tmp = [0xFFFF] * 64
    for lst in (0xF6E0, 0xF6DC, 0xF710 + struct.unpack_from('>H', d, 0xF710)[0]): rooms.gfx_slots(d, lst, tmp)
    for i in range(8, 14): slots[i] = tmp[i]
    for al in anim_tree.get(0, {}).values():
        for p in al.values():
            for _, m in sprites.script(d, p)[0]:
                for w, h, tw, x, y in sprites.mapping(d, m):
                    for k in range(w * h): count(slots, pals, (tw & ~0x7FF) | ((tw + k) & 0x7FF), what='sprite')
    return use, by

LAYOUTS = {'rows': (128, 16), 'columns': (64, 32)}

def _place(t, layout):
    """tile t's top left corner in the picture"""
    return (t % TILES_ACROSS * 8, t // TILES_ACROSS * 8) if layout == 'rows' else (t // 4 * 8, t % 4 * 8)

def block_rows(data, layout='rows'):
    """a block's 1,024 bytes as rows of colour numbers"""
    W, H = LAYOUTS[layout]
    rows = [[0] * W for _ in range(H)]
    for t in range(32):
        ox, oy = _place(t, layout)
        for y in range(8):
            for x in range(8):
                rows[oy + y][ox + x] = data[t * 32 + y * 4 + x // 2] >> (0 if x & 1 else 4) & 15
    return rows

def rows_block(rows, layout='rows'):
    out = bytearray(1024)
    for t in range(32):
        ox, oy = _place(t, layout)
        for y in range(8):
            for x in range(0, 8, 2):
                out[t * 32 + y * 4 + x // 2] = rows[oy + y][ox + x] << 4 | rows[oy + y][ox + x + 1]
    return bytes(out)

def export(d, blocks, out_dir, anim_tree):
    """graphics/png: a picture of each block, and blocks.json"""
    os.makedirs(out_dir, exist_ok=True)
    (use, by), index = usage(d, anim_tree), {}
    for i, data in enumerate(blocks):
        rec = use[i].most_common(1)[0][0] if use.get(i) else None
        colours = [gfx.rgb(w) for w in gfx.palette(d, rec)] if rec is not None else GREY
        layout = 'columns' if by[i]['sprite'] > by[i]['map'] else 'rows'
        pngio.write_indexed(os.path.join(out_dir, 'block_%03d.png' % i), block_rows(data, layout), colours, transparent=0)
        index['%03d' % i] = dict(palette=rec, layout=layout, colours=['%02X%02X%02X' % c for c in colours])
    json.dump(index, open(os.path.join(out_dir, 'blocks.json'), 'w'), indent=0)
    return sum(1 for i in range(len(blocks)) if use.get(i))

def read_block(path, colours, layout='rows'):
    """a block's picture back as its 1,024 bytes; colours: its 16 (r, g, b), for a full-colour file;
    layout: as blocks.json has it"""
    p = pngio.read(path)
    if (p['w'], p['h']) != LAYOUTS[layout]:
        raise pngio.PngError('%dx%d: this block is %dx%d (its layout is %s)' % ((p['w'], p['h']) + LAYOUTS[layout] + (layout,)))
    if p['kind'] == 'indexed':
        rows = p['rows']
        bad = max(max(r) for r in rows)
        if bad > 15: raise pngio.PngError('colour number %d: a block has 16 (0-15)' % bad)
        return rows_block(rows, layout)
    look = {tuple(c): i for i, c in reversed(list(enumerate(colours)))}
    rows = []
    for y, r in enumerate(p['rows']):
        out = []
        for x, (cr, cg, cb, ca) in enumerate(r):
            if ca < 128: out.append(0); continue
            i = look.get((cr, cg, cb))
            if i is None: raise pngio.PngError('the colour %02X%02X%02X at %d,%d is not one of the block\'s 16' % (cr, cg, cb, x, y))
            out.append(i)
        rows.append(out)
    return rows_block(rows, layout)
