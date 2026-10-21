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
  text/messages.txt     every message, readable, codes as {TAGS}, phrases expanded
  text/messages.json    the same parsed (characters, codes, commands, phrase references)
  text/phrases.txt, text/windows.json
  sprites/animations.json  the animation sets, groups and animations (frames, durations,
                        commands) and every sprite mapping (its pieces)
  sprites/player.png    Shion's animations, a row each
  sprites/enemies/enemy_TTTT.png  each enemy type's animations, as the rooms load it;
                        sprites/enemies/enemies.json says which room it is drawn with
  sprites/rooms/areaAA_roomRR.png  every room with its enemies at their spawn points
  sound/z80_driver.bin  the Z80 sound driver; sound/z80_data.bin what follows it (not decoded yet)

This first step only writes the data out so that it can be looked at and its decoding checked;
the game still reads the ROM. The decoders are tools/gfx.py, tools/rooms.py, tools/text.py and
tools/sprites.py.
"""
import sys, os, json, hashlib, argparse
sys.path.insert(0, os.path.dirname(__file__))
import gfx, rooms, text, sprites

EXPECTED_SHA1 = '87a968f773c7e807e647c0737132457b06b78276'
Z80_DRIVER = (0x098000, 0x099A77)            # docs/NOTES.md, the memory map
Z80_DATA = (0x099A77, 0x09F777)

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
                allrooms.append(dict(area=area, room=room, script=[dict(command=c, args=a) for c, a in script]))
                if not A.no_rooms:
                    rooms.write(path('rooms', 'area%02d_room%02d.png' % (area, room)), rooms.render(d, area, room)[0])
            except Exception as e:
                print('area %d room %d: %s' % (area, room, e)); bad += 1
    json.dump(allrooms, open(path('rooms', 'rooms.json'), 'w'), indent=1)
    checks['rooms'] = dict(areas=19, rooms=len(allrooms), problems=bad, drawn=not A.no_rooms)

    # text
    msgs, phrases = text.all_messages(d)
    with open(path('text', 'messages.txt'), 'w', encoding='utf-8') as f:
        for m in msgs:
            f.write('--- message %d (window %d, at $%06X)\n%s\n\n' % (m['number'], m['window'], m['address'], m['text']))
    json.dump([dict(number=m['number'], window=m['window'], address='%06X' % m['address'], text=m['text'],
                    parsed=m['parsed']) for m in msgs], open(path('text', 'messages.json'), 'w'), indent=1)
    with open(path('text', 'phrases.txt'), 'w', encoding='utf-8') as f:
        for k, lst in enumerate(phrases):
            for j, a in enumerate(lst): f.write('%d.%d $%06X %r\n' % (k, j, a, text.decode(d, a, phrases=phrases)[0]))
    windows = sorted(set(m['window'] for m in msgs))
    json.dump({str(w): text.window(d, w) for w in windows}, open(path('text', 'windows.json'), 'w'), indent=1)
    unknown = sum(m['text'].count('{x') for m in msgs)
    checks['text'] = dict(messages=len(msgs), phrase_tables=[len(t) for t in phrases], windows=len(windows),
                          unnamed_glyphs=unknown)

    # sprites: the animation data; Shion; every enemy type the rooms load; the rooms with them
    T = sprites.tree(d)
    anims, maps, bad = {}, {}, 0
    for st, groups in T.items():
        for g, al in groups.items():
            for an, a in al.items():
                try:
                    fr, cmds = sprites.script(d, a)
                    for _, m in fr: maps.setdefault('%06X' % m, sprites.mapping(d, m))
                    anims['%02X.%02X.%02X' % (st, g, an)] = dict(address='%06X' % a, commands=cmds,
                                                               frames=[dict(duration=t, mapping='%06X' % m) for t, m in fr])
                except Exception as e:
                    print('animation %02X.%02X.%02X: %s' % (st, g, an, e)); bad += 1
    json.dump(dict(animations=anims, mappings={k: [dict(width=w, height=h, tile='%04X' % t, x=x, y=y) for w, h, t, x, y in v]
                                             for k, v in maps.items()}), open(path('sprites', 'animations.json'), 'w'), indent=1)
    tile, colours = sprites.player_vram(d, 2, 0)
    rows = [((g, a), sprites.script(d, p)[0]) for g, al in T[0].items() for a, p in al.items()]
    rooms.write(path('sprites', 'player.png'), sheet(d, rows, tile, colours))
    seen, index = {}, {}
    for r in allrooms:
        for c in r['script']:
            if c['command'] != 'enemies': continue
            for t in sprites.placements(d, c['args'][0])[0]: seen.setdefault(t, (r['area'], r['room']))
    skipped = 0
    for t, (area, room) in sorted(seen.items()):
        try: st, g, ids = sprites.enemy(d, t)
        except ValueError: skipped += 1; continue          # category 2: its own layout, not decoded yet
        al = T.get(st, {}).get(g, {})
        if not al: skipped += 1; continue
        first = 48 - len(ids)                             # $125C4: its graphics just below slot 48
        tile, colours = sprites.vram(d, area, room, (first, ids))
        rows = [(a, sprites.script(d, p)[0]) for a, p in al.items()]
        rooms.write(path('sprites', 'enemies', 'enemy_%04X.png' % t), sheet(d, rows, tile, colours, first * 32))
        index['%04X' % t] = dict(set=st, group=g, graphics=ids, area=area, room=room, animations=len(al))
    json.dump(index, open(path('sprites', 'enemies', 'enemies.json'), 'w'), indent=1)
    if not A.no_rooms:
        for r in allrooms:
            rooms.write(path('sprites', 'rooms', 'area%02d_room%02d.png' % (r['area'], r['room'])),
                        sprites.room_with_enemies(d, r['area'], r['room'], T))
    checks['sprites'] = dict(sets=len(T), animations=len(anims), mappings=len(maps), problems=bad,
                             enemy_types=len(index), enemy_types_not_drawn=skipped)

    # sound: as it is, not decoded yet
    open(path('sound', 'z80_driver.bin'), 'wb').write(d[Z80_DRIVER[0]:Z80_DRIVER[1]])
    open(path('sound', 'z80_data.bin'), 'wb').write(d[Z80_DATA[0]:Z80_DATA[1]])
    checks['sound'] = dict(driver_bytes=Z80_DRIVER[1] - Z80_DRIVER[0], data_bytes=Z80_DATA[1] - Z80_DATA[0], decoded=False)

    manifest = dict(rom=os.path.basename(A.rom), size=len(d), sha1=sha1, expected_sha1=EXPECTED_SHA1,
                    checks=checks, files=len(files) + 1)
    json.dump(manifest, open(path('manifest.json'), 'w'), indent=1)
    print(json.dumps(checks, indent=1))
    print('wrote %d files to %s' % (len(files), out))
    return 0 if all(c.get('problems', 0) == 0 for c in checks.values()) else 1

if __name__ == '__main__':
    sys.exit(main())
