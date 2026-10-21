"""Check that the files tools/extract.py wrote hold all of the ROM's data: build the ROM's bytes
back from them and compare (docs/EXTRACT.md).

Usage: rebuild.py ROM [ASSETS]

The structured data is encoded back from the JSON (tools/assetfmt.py) and must match the ROM
byte for byte: the text (messages and phrases), the palettes, the room scripts, the animation
scripts and the sprite mappings. The graphics are compressed; their decoded blocks are checked
by the native build against the port's own decompressor (wbmw --verify-graphics ASSETS), which
the harness verified against the original's. The raw parts (the sound) are compared as they are.
"""
import sys, os, json, argparse, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import assetfmt, gfx

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('assets', nargs='?', default='assets')
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    J = lambda *p: json.load(open(os.path.join(A.assets, *p), encoding='utf-8'))
    report, bad_total = {}, 0

    def check(name, pieces):
        """pieces: (address, bytes built from the files); counts those not matching the ROM"""
        nonlocal bad_total
        bad, n, total = 0, 0, 0
        for a, b in pieces:
            n += 1; total += len(b)
            if d[a:a + len(b)] != b:
                if bad < 3: print('%s: differs at $%06X (%d bytes)' % (name, a, len(b)))
                bad += 1
        report[name] = dict(pieces=n, bytes=total, differing=bad)
        bad_total += bad

    msgs = J('text', 'messages.json')
    check('messages', [(int(m['address'], 16), assetfmt.text_encode(m['parsed'])) for m in msgs])
    ph = J('text', 'phrases.json')
    check('phrases', [(int(p['address'], 16), assetfmt.text_encode(p['parsed'])) for p in ph])
    pals = J('palettes', 'palettes.json')
    check('palettes', [(gfx.PALETTES + 17 * p['number'], assetfmt.palette_encode([int(w, 16) for w in p['words']])) for p in pals])
    rms = J('rooms', 'rooms.json')
    check('room scripts', [(int(r['address'], 16), assetfmt.room_script_encode(r['items'])) for r in rms])
    an = J('sprites', 'animations.json')
    check('animation scripts', [(int(v['address'], 16), assetfmt.anim_encode(v['items'])) for v in an['animations'].values()])
    check('sprite mappings', [(int(v['address'], 16), assetfmt.mapping_encode(v)) for v in an['mappings'].values()])
    for f, a in (('z80_driver.bin', 0x098000), ('z80_data.bin', 0x099A77)):
        check('sound ' + f, [(a, open(os.path.join(A.assets, 'sound', f), 'rb').read())])

    # the graphics: the port's decompressor, in the native build, against the decoded blocks
    exe = os.path.join(os.path.dirname(__file__), '..', 'work', 'native', 'wbmw.exe' if os.name == 'nt' else 'wbmw')
    if os.path.exists(exe):
        r = subprocess.run([exe, A.rom, '--verify-graphics', A.assets], capture_output=True, text=True)
        print(r.stdout.strip())
        report['graphics'] = dict(blocks=768, checked_by="the port's DecodeGfxBlock", differing=0 if r.returncode == 0 else 'some')
        bad_total += r.returncode != 0
    else:
        print('graphics: not checked (build the native game first: make -C native)')
        report['graphics'] = dict(checked=False)

    print(json.dumps(report, indent=1))
    print('everything matches' if not bad_total else '%d pieces differ' % bad_total)
    return 1 if bad_total else 0

if __name__ == '__main__':
    sys.exit(main())
