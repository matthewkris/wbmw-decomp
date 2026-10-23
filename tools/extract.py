"""Extract the game's data from the ROM into ordinary files (docs/EXTRACT.md).

Usage: extract.py ROM [OUT] [--no-rooms]

Writes into OUT (default assets/, which is git-ignored: these files are the game's data and are
never committed):

  manifest.json         the ROM's name, size and SHA-1, what was written, the decoders' checks
  graphics/block_NNN.bin  the 768 blocks of 32 tiles (1,024 bytes of VRAM data each), decoded
  graphics/sheet.png    all of them, a row of 32 tiles a block, in grey levels
  palettes/palettes.json  the 160 palette records, 16 colours each (Genesis words and RGB)
  palettes/swatches.png
  rooms/rooms.json      every room's script (layers, graphics, palettes, objects and so on)
  rooms/areaAA_roomRR.png  every room's map, drawn with its own graphics and palettes
  rooms/maps.json       every layer's map as data: its own block table (4 tile words and an
                        attribute a block) and its chunks (16 x 12 block numbers a place)
  rooms/shared_blocks.json  the three block tables every room shares
  rooms/lists.json      the other lists room scripts point at: event lists (exits, doors, things,
                        jumps on flags), spawn lists (the enemies), graphics slot lists, tile and
                        cell animations, and the cell lists, pictures, item lists and townspeople's
                        walk scripts they point at
  rooms/scenes.json     the ending's scenes (the directory's table 7): each scene's two layers'
                        maps, palette and graphics slot list
  rooms/house_pictures.json  the houses' and shops' pictures (the directory's table 2): place and
                        size, the frame's picture, the triggers' event list, the things' spawn list
  graphics/extra_NNN.bin  the 6 raw tiles after a raw block, which CopyRawTiles ($22A8) copies with it
  layout.json           the tables that point at the data: the data directory, the phrase
                        tables' bases, the animation tree's offset tables
  graphics/table.json   the graphics table ($41000): each block's format and address
  text/messages.txt     every message, readable, codes as {TAGS}, phrases expanded
  text/messages.json    the same parsed (characters, codes, commands, phrase references)
  text/phrases.txt, text/windows.json
  text/fragments.json   texts messages jump to that lie outside them (after the phrases)
  rooms/start_points.json  where the player starts in a room after a door, an exit or a new game
                        (the directory's table 0, $A37E): area, room, kind, place, a word
  rooms/music.json      the music of each area's rooms (the directory's table 3, $92D4)
  sprites/animations.json  the animation sets, groups and animations (frames, durations,
                        commands) and every sprite mapping (its pieces)
  sprites/player.png    Shion's animations, a row each
  sprites/enemies/enemy_TTTT.png  each enemy type's animations, as the rooms load it;
                        sprites/enemies/enemies.json says which room it is drawn with
  sprites/rooms/areaAA_roomRR.png  every room with its enemies at their spawn points
  sound/z80_driver.bin  the Z80 sound driver, as it is
  sound/music.json      the music and sound effects (tools/music.py): the sound table, each song's
                        and effect's header, every track and section as notes and commands
  sound/music.txt       the same, readable: notes by name
  tables/code_tables.json  the tables in the code region the game reads as data (tools/codetables.py):
                        each one's name, place, size, who reads it, and its values

This first step only writes the data out so that it can be looked at and its decoding checked;
the game still reads the ROM. The decoders are tools/gfx.py, tools/rooms.py, tools/text.py and
tools/sprites.py.
"""
import sys, os, json, hashlib, argparse
sys.path.insert(0, os.path.dirname(__file__))
import gfx, rooms, text, sprites, assetfmt, codetables, music

EXPECTED_SHA1 = '87a968f773c7e807e647c0737132457b06b78276'
FORMAT = 4                                   # raised whenever what this writes changes: pack.py wants the same
Z80_DRIVER = (0x098000, 0x099A77)            # docs/NOTES.md, the memory map; the data after it: music.DATA

def sheet(d, rows, tile, colours, base=0):
    """animations as rows of frames, each cell as large as the largest frame (bosses included)"""
    x0 = y0 = 0; x1 = y1 = 8
    for _, fr in rows:
        for _, m in fr:
            for w, h, tw, x, y in sprites.mapping(d, m):
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x + 8 * w), max(y1, y + 8 * h)
    CW, CH = x1 - x0 + 8, y1 - y0 + 8
    cols = max([len(fr) for _, fr in rows] + [1])
    img = [[(32, 32, 48)] * (cols * CW) for _ in range(max(len(rows), 1) * CH)]
    for ri, (_, fr) in enumerate(rows):
        for ci, (_, m) in enumerate(fr):
            sprites.draw(img, ci * CW + 4 - x0, ri * CH + 4 - y0, sprites.mapping(d, m), tile, colours, base)
    return img

def dir_tables(d, maps, block_table):
    """the ending's scenes (table 7, $96D6: 16 bytes a scene; the six run up to the first record
    whose longs are not the ROM's addresses: another table follows) and the houses' pictures
    (table 2, $B986: 16 bytes a picture, up to the first data any of them points at).
    The scenes' layers are added to maps (as area -1) so that they are checked like the rooms'"""
    be32, be16, sw = rooms.be32, rooms.be16, rooms.sw
    t7 = be32(d, rooms.DIR + 0x1C) & 0xFFFFFF
    scenes, n = [], 0
    while all(be32(d, t7 + 16 * n + k) < len(d) for k in (0, 4, 12)):
        r = t7 + 16 * n
        a, b, slots = be32(d, r), be32(d, r + 4), be32(d, r + 12)
        scenes.append(dict(address='%06X' % r, layer_a='%06X' % a, layer_b='%06X' % b, palette=be16(d, r + 8),
                           word_8B56=be16(d, r + 10), slots='%06X' % slots, raw=d[r:r + 16].hex()))
        layers = []
        for plane, src in (('A', a), ('B', b)):
            cmap, _ = rooms.layer_map(d, src)
            layers.append(dict(plane=plane, src='%06X' % src, blocks_at='%06X' % (src + sw(d, src)),
                               blocks=block_table(src + sw(d, src)), places={str(k): v for k, v in cmap.items()}))
        maps.append(dict(area=-1, room=n, layers=layers))
        n += 1
    t2 = be32(d, rooms.DIR + 8) & 0xFFFFFF
    recs, n, first = [], 0, None
    while first is None or t2 + 16 * n < first:
        r = t2 + 16 * n
        spawn, events, pic = sw(d, r + 8), sw(d, r + 12), sw(d, r + 14)
        first = min(x for x in (t2 + events, t2 + pic, t2 + spawn if spawn else None, first) if x)
        recs.append(dict(address='%06X' % r, raw=d[r:r + 16].hex(), spawns='%06X' % (t2 + spawn) if spawn else None,
                         events='%06X' % (t2 + events), picture='%06X' % (t2 + pic)))
        n += 1
    return scenes, dict(table='%06X' % t2, records=recs)

def room_lists(d, allrooms, extra=()):
    """every list the room scripts point at (by address), and what those point at; with the
    number of codes their interpreters do not know (a sign of a misread list)"""
    out = {k: {} for k in ('events', 'spawns', 'slots', 'tile_anims', 'cell_anims', 'cell_lists', 'pictures', 'item_lists', 'walk_scripts')}
    odd = 0
    def ptr(r, a):
        return a['long'] & 0xFFFFFF if a['flag'] else rooms.area_base(d, r['area']) + a['offset']
    def sub(kind, a):
        nonlocal odd
        key = '%06X' % a
        if kind == 'cells': w, e = assetfmt.cell_list_parse(d, a); out['cell_lists'][key] = dict(words=w, end='%06X' % e)
        elif kind == 'picture': v, e = assetfmt.picture_parse(d, a); out['pictures'][key] = dict(v, end='%06X' % e)
        elif kind == 'items': v, e = assetfmt.item_list_parse(d, a); out['item_lists'][key] = dict(bytes=v, end='%06X' % e)
        elif kind == 'walk': v, e = assetfmt.walk_script_parse(d, a); out['walk_scripts'][key] = dict(items=v, end='%06X' % e)
    for kind, a in extra:                          # from the directory's tables
        key = '%06X' % a
        if kind == 'events' and key not in out['events']:
            items, end, data = assetfmt.event_list_parse(d, a)
            out['events'][key] = dict(items=items, end='%06X' % end, data=['%s %06X' % (k, t) for k, t in data])
            for k, t in data: sub(k, t)
        elif kind == 'spawns' and key not in out['spawns']:
            items, end, data = assetfmt.spawn_list_parse(d, a)
            if any('code' in x and not (x['code'] < 16 or 0xF8 <= x['code']) for x in items):
                items, end, data = assetfmt.spawn_list_parse(d, a, kinds=False)
            out['spawns'][key] = dict(items=items, end='%06X' % end, data=['%s %06X' % (k, t) for k, t in data])
            for k, t in data: sub(k, t)
        elif kind == 'picture': sub('picture', a)
        elif kind == 'slots' and key not in out['slots']:
            items, end = assetfmt.slots_parse(d, a); out['slots'][key] = dict(items=items, end='%06X' % end)
    for r in allrooms:
        for it in r['items']:
            c = it.get('code')
            if c in (2, 4, 5, 8, 0x0B) and 'args' in it:
                a = ptr(r, it['args'][0]); key = '%06X' % a
                if c == 5 and key not in out['events']:
                    items, end, data = assetfmt.event_list_parse(d, a)
                    odd += sum(1 for x in items if 0xE0 <= x['code'] < 0xFF and x['code'] not in assetfmt.EVENT_CMDS and x['code'] != 0xFD)
                    out['events'][key] = dict(items=items, end='%06X' % end, data=['%s %06X' % (k, t) for k, t in data])
                    for k, t in data: sub(k, t)
                elif c == 8 and key not in out['spawns']:
                    items, end, data = assetfmt.spawn_list_parse(d, a)
                    bad = sum(1 for x in items if 'code' in x and not (x['code'] < 16 or 0xF8 <= x['code']))
                    if bad:                         # a boss room's: no kinds to load first
                        items, end, data = assetfmt.spawn_list_parse(d, a, kinds=False)
                        bad = sum(1 for x in items if 'code' in x and not (x['code'] < 16 or 0xF8 <= x['code']))
                    odd += bad
                    out['spawns'][key] = dict(items=items, end='%06X' % end, data=['%s %06X' % (k, t) for k, t in data])
                    for k, t in data: sub(k, t)
                elif c == 2 and key not in out['slots']:
                    items, end = assetfmt.slots_parse(d, a); out['slots'][key] = dict(items=items, end='%06X' % end)
                elif c == 4 and key not in out['tile_anims']:
                    w, end = assetfmt.tile_anim_parse(d, a); out['tile_anims'][key] = dict(words=w, end='%06X' % end)
                elif c == 0x0B and key not in out['cell_anims']:
                    b, end = assetfmt.cell_anim_parse(d, a); out['cell_anims'][key] = dict(bytes=b, end='%06X' % end)
    return out, odd

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('out', nargs='?', default='assets')
    ap.add_argument('--no-rooms', action='store_true', help='skip drawing the rooms (the slow part)')
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    sha1 = hashlib.sha1(d).hexdigest()
    if sha1 != EXPECTED_SHA1:
        print('warning: %s is not the expected dump (SHA-1 %s, expected %s); the tables are at its places' % (A.rom, sha1, EXPECTED_SHA1))
    out = A.out
    files, checks = [], {}
    def path(*p):
        f = os.path.join(out, *p); os.makedirs(os.path.dirname(f), exist_ok=True)
        files.append('/'.join(p)); return f

    # graphics: the 768 blocks, each checked to decode to 1,024 bytes ending where the next begins
    blocks, bad = gfx.check(d)
    for i, b in enumerate(blocks): open(path('graphics', 'block_%03d.bin' % i), 'wb').write(b)
    gfx.png(path('graphics', 'sheet.png'), blocks)
    json.dump(['%08X' % e for e in gfx.table(d)], open(path('graphics', 'table.json'), 'w'), indent=0)
    for i, e in enumerate(gfx.table(d)):           # raw blocks: 6 more tiles follow, read by $22A8
        if e != 0xFFFFFFFF and e >> 24 & 1:
            a = (e & 0xFFFFFF) + gfx.BLOCK
            open(path('graphics', 'extra_%03d.bin' % i), 'wb').write(d[a:a + 192])
    checks['graphics'] = dict(blocks=len(blocks), problems=bad)

    # palettes
    pals = [gfx.palette(d, n) for n in range(gfx.NPAL)]
    json.dump([dict(number=n, words=['%04X' % w for w in p], rgb=['#%02X%02X%02X' % gfx.rgb(w) for w in p])
               for n, p in enumerate(pals)], open(path('palettes', 'palettes.json'), 'w'), indent=1)
    gfx.swatches(path('palettes', 'swatches.png'), d)
    checks['palettes'] = dict(records=len(pals))

    # rooms: every script parsed and every map decoded; drawn unless --no-rooms
    allrooms, bad = [], 0
    for area in range(19):
        for room in range(rooms.room_count(d, area)):
            try:
                script = rooms.room_script(d, area, room)
                for cmd, a in script:
                    if cmd == 'layer': rooms.layer_map(d, a['src'])
                start, items, end = assetfmt.room_script_parse(d, area, room)
                allrooms.append(dict(area=area, room=room, address='%06X' % start, items=items,
                                     script=[dict(command=c, args=a) for c, a in script]))
                if not A.no_rooms:
                    rooms.write(path('rooms', 'area%02d_room%02d.png' % (area, room)), rooms.render(d, area, room)[0])
            except Exception as e:
                print('area %d room %d: %s' % (area, room, e)); bad += 1
    json.dump(allrooms, open(path('rooms', 'rooms.json'), 'w'), indent=1)
    def block_table(t):
        """a block table ($9F6A) decoded: [4 tile words, attribute] a block, None for an empty one"""
        t3 = {}
        rooms.blocks(d, t, t3)
        return [t3[i][0] + [t3[i][1]] if i in t3 else None for i in range(rooms.be16(d, t))]
    maps = []
    for r in allrooms:
        layers = []
        for it in r['items']:
            if 'kind' not in it: continue
            a1 = rooms.area_base(d, r['area'])
            src = a1 + it['offset'] if 'offset' in it else it['long'] & 0xFFFFFF
            cmap, _ = rooms.layer_map(d, src)
            layers.append(dict(plane='B' if it['code'] & 1 else 'A', src='%06X' % src,
                               blocks_at='%06X' % (src + rooms.sw(d, src)), blocks=block_table(src + rooms.sw(d, src)),
                               places={str(k): v for k, v in cmap.items()}))
        maps.append(dict(area=r['area'], room=r['room'], layers=layers))
    scenes, pics = dir_tables(d, maps, block_table)
    # the start points (table 0: word offsets to 8-byte records; its count, a word, just before it)
    t0 = rooms.be32(d, rooms.DIR) & 0xFFFFFF
    n0 = rooms.be16(d, t0) // 2
    offs = [rooms.sw(d, t0 + 2 * i) for i in range(n0)]
    json.dump(dict(table='%06X' % t0, count_before=rooms.be16(d, t0 - 2), offsets=offs,
                   points={str(o): dict(area=d[t0 + o], room=d[t0 + o + 1], kind=rooms.be16(d, t0 + o + 2), x=d[t0 + o + 4],
                                        y=d[t0 + o + 5], word=rooms.be16(d, t0 + o + 6)) for o in sorted(set(offs))}),
              open(path('rooms', 'start_points.json'), 'w'), indent=0)
    # the rooms' music (table 3: a word offset an area to a byte a room)
    t3 = rooms.be32(d, rooms.DIR + 0xC) & 0xFFFFFF
    json.dump(dict(table='%06X' % t3, areas=[dict(offset=rooms.sw(d, t3 + 2 * a), music=list(d[t3 + rooms.sw(d, t3 + 2 * a):
              t3 + rooms.sw(d, t3 + 2 * a) + rooms.room_count(d, a)])) for a in range(19)]), open(path('rooms', 'music.json'), 'w'), indent=0)
    json.dump(scenes, open(path('rooms', 'scenes.json'), 'w'))
    json.dump(pics, open(path('rooms', 'house_pictures.json'), 'w'), indent=0)
    json.dump(maps, open(path('rooms', 'maps.json'), 'w'))   # with the scenes' layers too
    checks['scenes'] = dict(scenes=len(scenes), house_pictures=len(pics['records']))
    extra = [(k, int(x[k], 16)) for x in pics['records'] for k in ('events', 'spawns') if x[k]]
    extra += [('picture', int(x['picture'], 16)) for x in pics['records']]
    extra += [('slots', int(x['slots'], 16)) for x in scenes]
    lists, odd = room_lists(d, allrooms, extra)
    json.dump(lists, open(path('rooms', 'lists.json'), 'w'), indent=0)
    checks['room lists'] = dict({k: len(v) for k, v in lists.items()}, unknown_codes=odd)
    tl = rooms.be32(d, rooms.DIR + 4)
    shared = [rooms.be32(d, tl + 4 * k) & 0xFFFFFF for k in range(3)]
    json.dump([dict(address='%06X' % a, blocks=block_table(a)) for a in shared], open(path('rooms', 'shared_blocks.json'), 'w'))
    checks['rooms'] = dict(areas=19, rooms=len(allrooms), problems=bad, drawn=not A.no_rooms)

    # text
    msgs, phrases = text.all_messages(d)
    with open(path('text', 'messages.txt'), 'w', encoding='utf-8') as f:
        for m in msgs:
            f.write('--- message %d (window %d, at $%06X)\n%s\n\n' % (m['number'], m['window'], m['address'], m['text']))
    json.dump([dict(number=m['number'], window=m['window'], address='%06X' % m['address'], end='%06X' % m['end'],
                    text=m['text'], parsed=m['parsed']) for m in msgs], open(path('text', 'messages.json'), 'w'), indent=1)
    plist = []
    for k, lst in enumerate(phrases):
        for j, a in enumerate(lst):
            t, parsed, end = text.decode(d, a)
            plist.append(dict(table=k, number=j, address='%06X' % a, end='%06X' % end, text=t, parsed=parsed))
    json.dump(plist, open(path('text', 'phrases.json'), 'w'), indent=1)
    with open(path('text', 'phrases.txt'), 'w', encoding='utf-8') as f:
        for k, lst in enumerate(phrases):
            for j, a in enumerate(lst): f.write('%d.%d $%06X %r\n' % (k, j, a, text.decode(d, a, phrases=phrases)[0]))
    frags = text.fragments(d, msgs, phrases)
    json.dump([dict(address='%06X' % f['address'], end='%06X' % f['end'], text=f['text'], parsed=f['parsed']) for f in frags],
              open(path('text', 'fragments.json'), 'w'), indent=1)
    windows = sorted(set(m['window'] for m in msgs))
    json.dump({str(w): text.window(d, w) for w in windows}, open(path('text', 'windows.json'), 'w'), indent=1)
    unknown = sum(m['text'].count('{x') for m in msgs)
    checks['text'] = dict(messages=len(msgs), phrase_tables=[len(t) for t in phrases], windows=len(windows), fragments=len(frags),
                          unnamed_glyphs=unknown)

    # sprites: the animation data; Shion; every enemy type the rooms load; the rooms with them
    T = sprites.tree(d)
    anims, maps, bad = {}, {}, 0
    for st, groups in T.items():
        for g, al in groups.items():
            for an, a in al.items():
                try:
                    start, items, end = assetfmt.anim_parse(d, a)
                    for it in items:
                        if 'mapping' in it:
                            m = sprites.ANIM + it['mapping']
                            if '%06X' % m not in maps:
                                ms, mp, me = assetfmt.mapping_parse(d, m)
                                for q in mp['pieces']: q['width'], q['height'] = (q['size'] >> 2 & 3) + 1, (q['size'] & 3) + 1
                                maps['%06X' % m] = dict(mp, address='%06X' % m)
                    anims['%02X.%02X.%02X' % (st, g, an)] = dict(address='%06X' % a, items=items)
                except Exception as e:
                    print('animation %02X.%02X.%02X: %s' % (st, g, an, e)); bad += 1
    # a stop ($FD, or a frame of duration 0) keeps its place, and the animation goes on from the
    # next byte when its timer is set again (UpdateAnim, $80E): such a continuation is kept as its own script
    # when it fills the bytes up to the next script exactly
    starts = sorted(set(int(v['address'], 16) for v in anims.values()))
    for k, v in sorted(anims.items()):
        last = v['items'][-1]
        if not (last.get('cmd') == 0xFD or last.get('duration') == 0): continue
        p = int(v['address'], 16) + len(assetfmt.anim_encode(v['items']))
        nxt = next((s for s in starts if s >= p), None)
        if nxt is None or nxt == p: continue
        start, items, end = assetfmt.anim_parse(d, p)
        if end != nxt or not all('cmd' in it or '%06X' % (sprites.ANIM + it['mapping']) in maps for it in items): continue
        anims[k + '+'] = dict(address='%06X' % p, items=items, continues=k)
        starts = sorted(starts + [p])
    json.dump(dict(animations=anims, mappings=maps), open(path('sprites', 'animations.json'), 'w'), indent=1)
    tile, colours = sprites.player_vram(d, 2, 0)
    rows = [((g, a), sprites.script(d, p)[0]) for g, al in T[0].items() for a, p in al.items()]
    rooms.write(path('sprites', 'player.png'), sheet(d, rows, tile, colours))
    seen, index = {}, {}
    for r in allrooms:
        for c in r['script']:
            if c['command'] != 'enemies': continue
            types = sprites.placements(d, c['args'][0])[0]
            for t in types: seen.setdefault(t, (r['area'], r['room'], sprites.kinds_palette(d, types)))
    skipped = 0
    for t, (area, room, room_pal3) in sorted(seen.items()):
        try: st, g, ids = sprites.enemy(d, t)
        except ValueError: skipped += 1; continue          # category 2: its own layout, not decoded yet
        al = T.get(st, {}).get(g, {})
        if not al: skipped += 1; continue
        first = 48 - len(ids)                             # $125C4: its graphics just below slot 48
        pal = sprites.enemy_palette(d, t)               # its own palette in line 3, else what its room's kinds leave there
        tile, colours = sprites.vram(d, area, room, (first, ids), pal3=pal or room_pal3)
        rows = [(a, sprites.script(d, p)[0]) for a, p in al.items()]
        rooms.write(path('sprites', 'enemies', 'enemy_%04X.png' % t), sheet(d, rows, tile, colours, first * 32))
        index['%04X' % t] = dict(set=st, group=g, graphics=ids, palette=pal, area=area, room=room, animations=len(al))
    json.dump(index, open(path('sprites', 'enemies', 'enemies.json'), 'w'), indent=1)
    if not A.no_rooms:
        for r in allrooms:
            rooms.write(path('sprites', 'rooms', 'area%02d_room%02d.png' % (r['area'], r['room'])),
                        sprites.room_with_enemies(d, r['area'], r['room'], T))
    checks['sprites'] = dict(sets=len(T), animations=len(anims), mappings=len(maps), problems=bad,
                             enemy_types=len(index), enemy_types_not_drawn=skipped)

    # sound: the driver as it is; the music and effects decoded (tools/music.py)
    open(path('sound', 'z80_driver.bin'), 'wb').write(d[Z80_DRIVER[0]:Z80_DRIVER[1]])
    mj, mt = music.extract(d)
    json.dump(mj, open(path('sound', 'music.json'), 'w'), indent=0)
    open(path('sound', 'music.txt'), 'w', encoding='utf-8').write(mt)
    checks['sound'] = dict(driver_bytes=Z80_DRIVER[1] - Z80_DRIVER[0], sounds=len(mj['sounds']),
                           songs=sum(1 for s in mj['sounds'] if s['kind'] == 'song'), tracks=len(mj['tracks']),
                           unused_tracks=len(mj['unused']))

    # the tables in the code region the game reads as data
    json.dump(codetables.extract(d), open(path('tables', 'code_tables.json'), 'w'), indent=1)
    checks['code tables'] = dict(tables=len(codetables.TABLES), bytes=sum(t[1] for t in codetables.TABLES))

    # the tables that point at the data
    l1 = [sprites.sw(d, sprites.ANIM + o) for o in range(0, sprites.sw(d, sprites.ANIM), 2)]
    l2 = sprites.packed_tables(d, l1)
    l3 = sprites.packed_tables(d, [v for t in l2.values() for v in t])
    tb = rooms.be32(d, rooms.DIR + 0x10) & 0xFFFFFF
    json.dump(dict(directory=['%08X' % rooms.be32(d, rooms.DIR + 4 * i) for i in range(30)],
                   phrase_tables=['%08X' % rooms.be32(d, tb + 4 * k) for k in range(4)],
                   anim_level1=l1, anim_level2={str(k): v for k, v in l2.items()}, anim_level3={str(k): v for k, v in l3.items()}),
              open(path('layout.json'), 'w'), indent=1)

    manifest = dict(format=FORMAT, rom=os.path.basename(A.rom), size=len(d), sha1=sha1, expected_sha1=EXPECTED_SHA1,
                    checks=checks, files=len(files) + 1)
    json.dump(manifest, open(path('manifest.json'), 'w'), indent=1)
    print(json.dumps(checks, indent=1))
    print('wrote %d files to %s' % (len(files), out))
    return 0 if all(c.get('problems', 0) == 0 for c in checks.values()) else 1

if __name__ == '__main__':
    sys.exit(main())
