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
  sound/z80_driver.bin  the Z80 sound driver; sound/z80_data.bin what follows it (not decoded yet)

This first step only writes the data out so that it can be looked at and its decoding checked;
the game still reads the ROM. The decoders are tools/gfx.py, tools/rooms.py and tools/text.py.
"""
import sys, os, json, hashlib, argparse
sys.path.insert(0, os.path.dirname(__file__))
import gfx, rooms, text

EXPECTED_SHA1 = '87a968f773c7e807e647c0737132457b06b78276'
Z80_DRIVER = (0x098000, 0x099A77)            # docs/NOTES.md, the memory map
Z80_DATA = (0x099A77, 0x09F777)

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
                          characters_outside_ascii=unknown)

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
