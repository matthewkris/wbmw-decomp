"""The rooms' maps for Tiled (https://www.mapeditor.org, docs/MODDING.md, "Maps"): each layer's map
as a Tiled map, read back by the pack.

    python tools/tiled.py export [ASSETS]     writes ASSETS/rooms/tiled/ (a minute or so)

A layer (a room's plane A or B; rooms can share one, the backgrounds mostly) is a grid of places,
8 by 8 at most, each 16 x 12 blocks of 16 x 16 pixels; a block is four tiles and an attribute (its
collision), from one of four tables: the three every room shares and the layer's own
(rooms/maps.json, `blocks`). A cell is a word: the table (bits 8-9) and the block (bits 0-7).

* A Tiled map per layer, `areaAA_roomRR_P.tmx` after the first room that uses it (its properties
  name them all), one tile layer, 16 x 16 tiles; its tileset `..._blocks.png` has all 1,024 blocks
  the layer can use, drawn with that room's graphics and palettes, so that a tile's number is the
  cell's word (Tiled's gid less one): rows 0-7 the first shared table, 8-15 the second, 16-23 the
  third, 24-31 the layer's own. Places the layer does not use are empty cells.
* Read back (read): a map whose cells or size differ from the layer's becomes its places (an empty
  place, all its cells empty, is not used; at most 16 used). A map made larger or smaller (whole
  places: a multiple of 16 x 12 cells) gives every room that uses the layer that size (the room
  scripts' layer commands).
"""
import sys, os, json, struct, base64, zlib, gzip
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(__file__))
import assetfmt, gfx, gif, balance

PLACE_W, PLACE_H, GRID = 16, 12, 8
MAX_PLACES = 16                                     # chunks a plane has in RAM ($A068)
BASE_SLOTS = 0x96BC                                 # the slot list every room starts from (SetGfxSlots)

class TiledError(Exception): pass

def apply_slots(items, slots):
    """a graphics slot list (lists.json's items) into slots, as SetGfxSlots ($234C) does"""
    i = 0
    for it in items:
        if 'block' in it: slots[i & 63] = it['block']; i += 1; continue
        c = it['code']
        if c < 0xC0: i = c & 0x3F
        elif c == 0xC0: i += 1
        elif c == 0xFF: return
        elif c < 0xF8:
            for k in range(c & 0x3F): slots[i & 63] = (it['args'][0] + k) & 0xFFFF; i += 1
        else:
            for k in range((c & 7) + 1): slots[i & 63] = 0xFFFF; i += 1

def layer_src(r, it, dirs):
    return it['long'] & 0xFFFFFF if 'long' in it else (dirs[8 + r['area']] & 0xFFFFFF) + it['offset']

def load(assets):
    J = lambda *p: json.load(open(os.path.join(assets, *p), encoding='utf-8'))
    return dict(rooms=J('rooms', 'rooms.json'), maps=J('rooms', 'maps.json'), shared=J('rooms', 'shared_blocks.json'),
                lists=J('rooms', 'lists.json'), pals=J('palettes', 'palettes.json'), tables=J('tables', 'code_tables.json'),
                dirs=[int(v, 16) for v in J('layout.json')['directory']])

def layers_of(D):
    """{src: dict(maps entry, rooms [(area, room, plane, w, h)])} for the rooms' layers"""
    out = {}
    for r in D['maps']:
        if r['area'] < 0: continue                  # (the ending's scenes)
        for L in r['layers']: out.setdefault(L['src'], dict(layer=L, rooms=[]))
    for r in D['rooms']:
        for it in r['items']:
            if 'kind' not in it: continue
            src = '%06X' % layer_src(r, it, D['dirs'])
            if src in out: out[src]['rooms'].append((r['area'], r['room'], 'B' if it['code'] & 1 else 'A', it['w'], it['h']))
    return {k: v for k, v in out.items() if v['rooms']}

def grid_size(entry):
    """the map's size in places: the rooms' largest, and every place it has"""
    w = max([x[3] for x in entry['rooms']] + [int(p) % GRID + 1 for p in entry['layer']['places']])
    h = max([x[4] for x in entry['rooms']] + [int(p) // GRID + 1 for p in entry['layer']['places']])
    return min(w, GRID), min(h, GRID)

# ---- drawing the blocks ----

def room_look(D, area, room):
    """a room's graphics slots and its four palette lines' colours"""
    base = balance.code_bytes(D['tables'], BASE_SLOTS, 256)
    slots = [0xFFFF] * 64
    apply_slots(assetfmt.slots_parse(base, 0)[0], slots)
    pals = [0, 1, 0, 0]
    r = next(x for x in D['rooms'] if x['area'] == area and x['room'] == room)
    for c in r['script']:
        if c['command'] == 'gfx_slots':
            lst = D['lists']['slots'].get('%06X' % c['args'][0])
            if lst: apply_slots(lst['items'], slots)
        elif c['command'] == 'palette': pals[2] = c['args'][0]
        elif c['command'] == 'palette3': pals[3] = c['args'][0]
    words = {p['number']: [int(w, 16) for w in p['words']] for p in D['pals']}
    return tuple(slots), [gfx.rgb(w) for p in pals for w in words[p]]

def block_pixels(bdef, slots, colours, blocks):
    """a block's 16 x 16 pixels (None transparent)"""
    img = [[None] * 16 for _ in range(16)]
    if not bdef: return img
    for q, tw in enumerate(bdef[:4]):
        n = tw & 0x7FF
        s = slots[n >> 5]
        if s == 0xFFFF or s & 0x8000 or s >= len(blocks): continue
        t = blocks[s][(n & 31) * 32:(n & 31) * 32 + 32]
        pal = (tw >> 13 & 3) * 16
        ox, oy = (q & 1) * 8, (q >> 1) * 8
        for y in range(8):
            yy = 7 - y if tw & 0x1000 else y
            for x in range(8):
                xx = 7 - x if tw & 0x800 else x
                c = t[yy * 4 + xx // 2] >> (0 if xx & 1 else 4) & 15
                if c: img[oy + y][ox + x] = colours[pal + c]
    return img

def tileset_rows(tables, look, blocks, cache):
    """the 1,024 blocks as a 512 x 512 picture's rows: 32 a row, table by table"""
    slots, colours = look
    rows = [[None] * 512 for _ in range(512)]
    for t, table in enumerate(tables):
        for i, bdef in enumerate(table):
            key = (look[0], tuple(colours), tuple(bdef) if bdef else None)
            if key not in cache: cache[key] = block_pixels(bdef, slots, colours, blocks)
            px = cache[key]
            n = t * 256 + i
            ox, oy = n % 32 * 16, n // 32 * 16
            for y in range(16): rows[oy + y][ox:ox + 16] = px[y]
    return rows

# ---- the files ----

def name_of(entry):
    a, r, p = entry['rooms'][0][:3]
    return 'area%02d_room%02d_%s' % (a, r, p)

def cells_of(entry, w, h):
    """the layer's cells as Tiled gids, row by row (0: no place there)"""
    W, H = w * PLACE_W, h * PLACE_H
    grid = [[0] * W for _ in range(H)]
    for pos, cells in entry['layer']['places'].items():
        cx, cy = int(pos) % GRID, int(pos) // GRID
        if cx >= w or cy >= h: continue
        for k, v in enumerate(cells): grid[cy * PLACE_H + k // PLACE_W][cx * PLACE_W + k % PLACE_W] = v + 1
    return grid

def write_tmx(path, entry, w, h, tiles_png):
    grid = cells_of(entry, w, h)
    rooms = ', '.join('area %d room %d (%s)' % x[:3] for x in entry['rooms'])
    prop = lambda k, v: '   <property name="%s" value="%s"/>\n' % (k, v)
    data = ',\n'.join(','.join(str(v) for v in row) for row in grid)
    open(path, 'w', encoding='utf-8', newline='\n').write(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<map version="1.10" orientation="orthogonal" renderorder="right-down" width="%d" height="%d" '
        'tilewidth="16" tileheight="16" infinite="0" nextlayerid="2" nextobjectid="1">\n'
        '  <properties>\n%s%s%s  </properties>\n'
        '  <tileset firstgid="1" name="blocks" tilewidth="16" tileheight="16" tilecount="1024" columns="32">\n'
        '    <image source="%s" width="512" height="512"/>\n  </tileset>\n'
        '  <layer id="1" name="%s" width="%d" height="%d">\n    <data encoding="csv">\n%s\n</data>\n  </layer>\n</map>\n'
        % (w * PLACE_W, h * PLACE_H, prop('layer', entry['layer']['src']), prop('rooms', rooms),
           prop('exported_places', '%dx%d' % (w, h)), tiles_png, name_of(entry)[-1], w * PLACE_W, h * PLACE_H, data))

def export(assets):
    D = load(assets)
    import pack
    blocks = pack.read_blocks(assets)
    if blocks is None: raise TiledError('the graphics could not be read')
    shared = [[None] * 256 for _ in range(3)]
    for k, t in enumerate(D['shared']):
        for i, b in enumerate(t['blocks'][:256]): shared[k][i] = b
    out = os.path.join(assets, 'rooms', 'tiled')
    os.makedirs(out, exist_ok=True)
    looks, cache, n = {}, {}, 0
    index = {}
    for src, entry in sorted(layers_of(D).items()):
        a, r = entry['rooms'][0][:2]
        if (a, r) not in looks: looks[(a, r)] = room_look(D, a, r)
        own = (entry['layer']['blocks'] + [None] * 256)[:256]
        name = name_of(entry)
        gif.write_png(os.path.join(out, name + '_blocks.png'), tileset_rows(shared + [own], looks[(a, r)], blocks, cache))
        w, h = grid_size(entry)
        write_tmx(os.path.join(out, name + '.tmx'), entry, w, h, name + '_blocks.png')
        index[name] = dict(layer=src, rooms=['area %d room %d %s' % x[:3] for x in entry['rooms']], places='%dx%d' % (w, h))
        n += 1
    json.dump(index, open(os.path.join(out, 'index.json'), 'w'), indent=1)
    return n

def read_tmx(path):
    """(properties, width, height, gids row by row)"""
    root = ET.parse(path).getroot()
    props = {p.get('name'): p.get('value') for p in root.iter('property')}
    layers = root.findall('layer')
    if len(layers) != 1: raise TiledError('%s: keep one tile layer (it has %d)' % (os.path.basename(path), len(layers)))
    L = layers[0]
    w, h = int(L.get('width')), int(L.get('height'))
    data = L.find('data')
    enc, comp = data.get('encoding'), data.get('compression')
    if enc == 'csv': gids = [int(v) for v in data.text.replace('\n', '').split(',') if v.strip()]
    elif enc == 'base64':
        raw = base64.b64decode(data.text.strip())
        if comp == 'zlib': raw = zlib.decompress(raw)
        elif comp == 'gzip': raw = gzip.decompress(raw)
        elif comp: raise TiledError('%s: save it with CSV or base64 (zlib, gzip) data' % os.path.basename(path))
        gids = list(struct.unpack('<%dI' % (len(raw) // 4), raw))
    else: raise TiledError('%s: save it with CSV data (Map Properties: Tile Layer Format)' % os.path.basename(path))
    if len(gids) != w * h: raise TiledError('%s: %d cells for %dx%d' % (os.path.basename(path), len(gids), w, h))
    return props, w, h, [gids[y * w:(y + 1) * w] for y in range(h)]

def read(assets):
    """the maps changed in Tiled: {src: dict(places, size (w, h) or None when not resized)}"""
    tdir = os.path.join(assets, 'rooms', 'tiled')
    if not os.path.isdir(tdir): return {}
    D = load(assets)
    layers = layers_of(D)
    out = {}
    for f in sorted(os.listdir(tdir)):
        if not f.endswith('.tmx'): continue
        props, W, H, rows = read_tmx(os.path.join(tdir, f))
        src = props.get('layer')
        if src not in layers: raise TiledError('%s: its "layer" property names no layer of the rooms' % f)
        if W % PLACE_W or H % PLACE_H: raise TiledError('%s: %dx%d cells: a map is whole places of 16 x 12' % (f, W, H))
        w, h = W // PLACE_W, H // PLACE_H
        if w > GRID or h > GRID: raise TiledError('%s: %dx%d places: 8 x 8 at most' % (f, w, h))
        places = {}
        for cy in range(h):
            for cx in range(w):
                cells = [rows[cy * PLACE_H + k // PLACE_W][cx * PLACE_W + k % PLACE_W] for k in range(PLACE_W * PLACE_H)]
                if all(g == 0 for g in cells): continue
                bad = next((g for g in cells if g > 1024), None)
                if bad: raise TiledError('%s: a tile from beyond its tileset (gid %d), or flipped (Tiled flips are not the game\'s)' % (f, bad))
                places[str(cy * GRID + cx)] = [max(g - 1, 0) for g in cells]
        if len(places) > MAX_PLACES: raise TiledError('%s: %d places used: a layer has room for 16' % (f, len(places)))
        entry = layers[src]
        ew, eh = (int(v) for v in props.get('exported_places', '%dx%d' % grid_size(entry)).split('x'))
        orig = {p: c for p, c in entry['layer']['places'].items() if int(p) % GRID < ew and int(p) // GRID < eh}
        resized = (w, h) != (ew, eh)
        if places == orig and not resized: continue
        if not resized:                             # the places outside the map it shows stay
            places.update({p: c for p, c in entry['layer']['places'].items() if p not in orig})
        out[src] = dict(places=places, size=(w, h) if resized else None, file=f)
    return out

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] != 'export':
        print(__doc__); sys.exit(1)
    assets = sys.argv[2] if len(sys.argv) > 2 else 'assets'
    print('wrote %d maps to %s' % (export(assets), os.path.join(assets, 'rooms', 'tiled')))
