"""Check that the files tools/extract.py wrote hold all of the ROM's data: build the ROM's bytes
back from them and compare (docs/EXTRACT.md).

Usage: rebuild.py ROM [ASSETS] [--reads FILE]

The structured data is encoded back from the JSON (tools/assetfmt.py) and must match the ROM
byte for byte: the text (messages and phrases), the palettes, the room scripts, the animation
scripts and the sprite mappings, and the tables that point at the data (the data directory, the
message, window and phrase tables, the graphics table, the areas' room offsets, the animation
tree). The compressed data is checked by content against the port's own decoders, which the
harness verified against the original's: the graphics (wbmw --verify-graphics) and the rooms'
block tables and layer maps (wbmw --unpack-list). The raw parts (the sound) are compared as they
are. At the end, how much of the ROM the checks cover, and the largest parts they do not.

--reads FILE (from the native build's --rom-reads, over as much play as can be had) compares what
the game reads with what the checks cover: the bytes it reads that no file holds yet. In the
native build no 68000 code runs, so a read in the code region ($0-$1C601) is a table read as data.
"""
import sys, os, json, struct, argparse, subprocess, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import assetfmt, gfx, sprites, codetables

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('assets', nargs='?', default='assets')
    ap.add_argument('--reads', help="the native build's --rom-reads map")
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    J = lambda *p: json.load(open(os.path.join(A.assets, *p), encoding='utf-8'))
    report, bad_total = {}, 0
    covered = bytearray(len(d))                     # 1: built back byte for byte; 2: checked by content

    def mark(a, n, how=1):
        for i in range(a, min(a + n, len(d))): covered[i] = covered[i] or how

    def check(name, pieces):
        """pieces: (address, bytes built from the files); counts those not matching the ROM"""
        nonlocal bad_total
        bad, n, total = 0, 0, 0
        for a, b in pieces:
            n += 1; total += len(b); mark(a, len(b))
            if d[a:a + len(b)] != b:
                if bad < 3: print('%s: differs at $%06X (%d bytes)' % (name, a, len(b)))
                bad += 1
        report[name] = dict(pieces=n, bytes=total, differing=bad)
        bad_total += bad

    msgs = J('text', 'messages.json')
    check('messages', [(int(m['address'], 16), assetfmt.text_encode(m['parsed'])) for m in msgs])
    ph = J('text', 'phrases.json')
    check('phrases', [(int(p['address'], 16), assetfmt.text_encode(p['parsed'])) for p in ph])
    check('text fragments', [(int(f['address'], 16), assetfmt.text_encode(f['parsed'])) for f in J('text', 'fragments.json')])
    pals = J('palettes', 'palettes.json')
    check('palettes', [(gfx.PALETTES + 17 * p['number'], assetfmt.palette_encode([int(w, 16) for w in p['words']])) for p in pals])
    rms = J('rooms', 'rooms.json')
    check('room scripts', [(int(r['address'], 16), assetfmt.room_script_encode(r['items'])) for r in rms])
    an = J('sprites', 'animations.json')
    check('animation scripts', [(int(v['address'], 16), assetfmt.anim_encode(v['items'])) for v in an['animations'].values()])
    check('sprite mappings', [(int(v['address'], 16), assetfmt.mapping_encode(v)) for v in an['mappings'].values()])
    for f, a in (('z80_driver.bin', 0x098000), ('z80_data.bin', 0x099A77)):
        check('sound ' + f, [(a, open(os.path.join(A.assets, 'sound', f), 'rb').read())])

    # the lists the room scripts point at, and what those point at
    LS = J('rooms', 'lists.json')
    w16s = lambda ws: b''.join(struct.pack('>H', v) for v in ws)
    enc = dict(events=lambda v: assetfmt.flow_encode(v['items'], assetfmt.event_fmt),
               spawns=lambda v: assetfmt.flow_encode(v['items'], assetfmt.spawn_fmt),
               slots=lambda v: assetfmt.slots_encode(v['items']), tile_anims=lambda v: w16s(v['words']),
               cell_anims=lambda v: bytes(v['bytes']), cell_lists=lambda v: w16s(v['words']),
               pictures=assetfmt.picture_encode, item_lists=lambda v: bytes(v['bytes']),
               walk_scripts=lambda v: assetfmt.walk_script_encode(v['items']))
    for kind, f in enc.items():
        check('room ' + kind.replace('_', ' '), [(int(a, 16), f(v)) for a, v in LS[kind].items()])

    # the ending's scenes (from their fields) and the houses' pictures (their records as they are:
    # some fields are not named yet); the extra raw tiles after the raw graphics blocks
    sc = J('rooms', 'scenes.json')
    check('scene records', [(int(x['address'], 16), struct.pack('>IIHHI', int(x['layer_a'], 16), int(x['layer_b'], 16),
                                                                 x['palette'], x['word_8B56'], int(x['slots'], 16))) for x in sc])
    hp = J('rooms', 'house_pictures.json')
    check('house picture records', [(int(x['address'], 16), bytes.fromhex(x['raw'])) for x in hp['records']])
    sp = J('rooms', 'start_points.json'); t0 = int(sp['table'], 16)
    check('start points', [(t0 - 2, struct.pack('>H', sp['count_before']) + b''.join(struct.pack('>h', o) for o in sp['offsets']))]
          + [(t0 + int(o), bytes([v['area'], v['room']]) + struct.pack('>H', v['kind']) + bytes([v['x'], v['y']]) + struct.pack('>H', v['word']))
             for o, v in sp['points'].items()])
    mu = J('rooms', 'music.json'); t3 = int(mu['table'], 16)
    check('room music', [(t3, b''.join(struct.pack('>h', a['offset']) for a in mu['areas']))]
          + [(t3 + a['offset'], bytes(a['music'])) for a in mu['areas']])
    ex = []
    for i, e in enumerate(J('graphics', 'table.json')):
        e = int(e, 16)
        if e != 0xFFFFFFFF and e >> 24 & 1:
            ex.append(((e & 0xFFFFFF) + gfx.BLOCK, open(os.path.join(A.assets, 'graphics', 'extra_%03d.bin' % i), 'rb').read()))
    check('extra raw tiles', ex)

    # the tables that point at the data, from the files (the layout's own addresses: the data
    # directory at $1CC00, the graphics table at $41000, the animation data at $A0000)
    L = J('layout.json')
    dirs = [int(v, 16) for v in L['directory']]
    base = lambda i: dirs[i] & 0xFFFFFF
    check('data directory', [(0x1CC00, b''.join(struct.pack('>I', v) for v in dirs))])
    check('graphics table', [(gfx.TABLE, b''.join(struct.pack('>I', int(v, 16)) for v in J('graphics', 'table.json')))])
    check('message table', [(base(5), b''.join(bytes([m['window']]) + struct.pack('>H', int(m['address'], 16) - base(5)) for m in msgs))])
    W = J('text', 'windows.json')
    check('window table', [(base(6) + 4 * int(w), bytes([v['col'], v['row'], v['width'], v['lines']])) for w, v in W.items()])
    pt = [int(v, 16) for v in L['phrase_tables']]
    pieces = [(base(4), b''.join(struct.pack('>I', v) for v in pt))]
    for k in range(4):
        offs = [int(p['address'], 16) - (pt[k] & 0xFFFFFF) for p in sorted((p for p in ph if p['table'] == k), key=lambda p: p['number'])]
        pieces.append((pt[k] & 0xFFFFFF, b''.join(struct.pack('>h', o) for o in offs)))
    check('phrase tables', pieces)
    pieces = []
    for area in range(19):
        rs = sorted((r for r in rms if r['area'] == area), key=lambda r: r['room'])
        pieces.append((base(8 + area), b''.join(struct.pack('>h', int(r['address'], 16) - base(8 + area)) for r in rs)))
    check('area room tables', pieces)
    words = lambda vs: b''.join(struct.pack('>h', v) for v in vs)
    check('animation tree', [(sprites.ANIM, words(L['anim_level1']))]
          + [(sprites.ANIM + int(k), words(v)) for k, v in L['anim_level2'].items()]
          + [(sprites.ANIM + int(k), words(v)) for k, v in L['anim_level3'].items()])

    # the tables in the code region the game reads as data, from their rows
    check('code tables', [(int(t['address'], 16), codetables.encode(t['rows'], t['element'])) for t in J('tables', 'code_tables.json')])

    # the compressed data, by content, against the port's decoders in the native build
    exe = os.path.join(os.path.dirname(__file__), '..', 'work', 'native', 'wbmw.exe' if os.name == 'nt' else 'wbmw')
    if os.path.exists(exe):
        r = subprocess.run([exe, A.rom, '--verify-graphics', A.assets], capture_output=True, text=True)
        print(r.stdout.strip())
        report['graphics'] = dict(blocks=768, checked_by="the port's DecodeGfxBlock", differing=0 if r.returncode == 0 else 'some')
        bad_total += r.returncode != 0
        if r.returncode == 0:
            for e in gfx.table(d):
                if e != 0xFFFFFFFF: mark(e & 0xFFFFFF, gfx.decode(d, e)[1] - (e & 0xFFFFFF), 2)
        bad, read = check_maps(exe, A, J, report)
        bad_total += bad
        if not bad:                                 # the maps' data: every byte the port's unpackers read
            for i, r in enumerate(read):
                if r: mark(i, 1, 2)
    else:
        print('graphics and room maps: not checked (build the native game first: make -C native)')
        report['graphics'] = report['room maps'] = dict(checked=False)

    gaps, i = [], 0
    while i < len(d):
        if covered[i] or d[i] == 0xFF and all(b == 0xFF for b in d[i:i + 16]): i += 1; continue
        j = i
        while j < len(d) and not covered[j] and not (d[j] == 0xFF and all(b == 0xFF for b in d[j:j + 16])): j += 1
        gaps.append((j - i, i)); i = j
    CODE = 0x1C602                                  # $0-$1C601: the vectors, the header and the 68000 code
    data_gaps = [(n, a) for n, a in gaps if a >= CODE]
    report['coverage'] = dict(rom_bytes=len(d), built_back=covered.count(1), checked_by_content=covered.count(2),
                              padding=sum(1 for b in d if b == 0xFF),
                              code_and_its_tables_not_covered=sum(min(n, CODE - a) for n, a in gaps if a < CODE),
                              data_not_covered=sum(n for n, _ in data_gaps))
    print('largest data not covered:', ', '.join('$%06X (%d bytes)' % (a, n) for n, a in sorted(data_gaps, reverse=True)[:10]))

    if A.reads:
        rd = open(A.reads, 'rb').read()
        runs = []                                   # read, not covered: (length, start)
        i = 0
        while i < len(d):
            if rd[i] and not covered[i]:
                j = i
                while j < len(d) and rd[j] and not covered[j]: j += 1
                runs.append((j - i, i)); i = j
            else: i += 1
        code = [(n, a) for n, a in runs if a < CODE]; data = [(n, a) for n, a in runs if a >= CODE]
        report['reads'] = dict(bytes_read=sum(1 for b in rd if b), read_and_covered=sum(1 for i in range(len(d)) if rd[i] and covered[i]),
                               read_not_covered_code_region=sum(n for n, _ in code), read_not_covered_data=sum(n for n, _ in data),
                               covered_never_read=sum(1 for i in range(len(d)) if covered[i] and not rd[i]))
        print('read, not covered, in the code region (tables):', ', '.join('$%06X (%d)' % (a, n) for n, a in sorted(code, reverse=True)[:16]))
        print('read, not covered, data:', ', '.join('$%06X (%d)' % (a, n) for n, a in sorted(data, reverse=True)[:12]))

    print(json.dumps(report, indent=1))
    print('everything matches' if not bad_total else '%d pieces differ' % bad_total)
    return 1 if bad_total else 0

def check_maps(exe, A, J, report):
    """the rooms' block tables and layer maps: the port's unpackers' results against the files"""
    maps, shared = J('rooms', 'maps.json'), J('rooms', 'shared_blocks.json')
    want = {}                                       # ('B'|'L', address) -> the files' data
    for t in shared: want[('B', int(t['address'], 16))] = t['blocks']
    for r in maps:
        for lay in r['layers']:
            want[('B', int(lay['blocks_at'], 16))] = lay['blocks']
            want[('L', int(lay['src'], 16))] = lay['places']
    tmp = tempfile.mkdtemp()
    req, out = os.path.join(tmp, 'req.txt'), os.path.join(tmp, 'out.bin')
    open(req, 'w').write(''.join('%s %X\n' % k for k in want))
    r = subprocess.run([exe, A.rom, '--unpack-list', req, out], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); report['room maps'] = dict(checked=False); return 1, b''
    o, p, bad = open(out, 'rb').read(), 0, {'B': 0, 'L': 0}
    read = open(out + '.reads', 'rb').read()      # the ROM bytes the unpackers read, for the coverage
    n = {'B': 0, 'L': 0}
    while p < len(o):
        kind, addr = chr(o[p]), struct.unpack_from('>I', o, p + 1)[0]; p += 5
        files = want[(kind, addr)]; n[kind] += 1
        if kind == 'B':
            count = struct.unpack_from('>H', o, p)[0]; p += 2
            ok = count == len(files)
            for i in range(count):
                rec = o[p:p + 9]; p += 9
                if i >= len(files): continue
                if files[i] is None: ok &= rec == b'\xee' * 9
                else: ok &= list(struct.unpack('>4H', rec[:8])) + [rec[8]] == files[i]
        else:
            m, cells = o[p:p + 64], o[p + 64:p + 64 + 16 * 0x180]; p += 64 + 16 * 0x180
            ok = all((m[pos] == 0xFF) == (str(pos) not in files) for pos in range(64))
            for pos, want_cells in files.items():
                k = m[int(pos)]
                ok &= k < 16 and list(struct.unpack_from('>192H', cells, k * 0x180)) == want_cells
        if not ok:
            if bad['B'] + bad['L'] < 3: print('room maps: %s at $%06X differs' % ('block table' if kind == 'B' else 'layer map', addr))
            bad[kind] += 1
    report['room maps'] = dict(block_tables=n['B'], layer_maps=n['L'], checked_by="the port's UnpackBlocks and UnpackLayerMap",
                               differing=bad['B'] + bad['L'])
    print(r.stdout.strip())
    return bad['B'] + bad['L'], read

if __name__ == '__main__':
    sys.exit(main())
