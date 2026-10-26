"""Pack the files tools/extract.py wrote into the one file the game loads its data from
(docs/EXTRACT.md, "The game reads the files").

Usage: pack.py [ASSETS] [OUT]          (defaults: assets, ASSETS/game.pak)

Answers 3, writing nothing, when ASSETS was written by another version of extract.py (its
manifest's format): extract again (tools/makepack.sh, which the build runs, does).

The pack holds, from the files only (the ROM is not read):

  * the data image: every piece the files hold at its place in the ROM's address space, as
    tools/rebuild.py builds them (text, palettes, room scripts and lists, animations, the tables
    in the code region, the sound driver and its data, ...), and the raw graphics blocks;
  * its mask: which bytes of the image the files hold (the game reports any read of another);
  * the decoded data the game's decoders would make from the compressed data, which is not in
    the image: the 768 graphics blocks (DecodeGfxBlock, $6BC4), the rooms' block tables
    (UnpackBlocks, $9F6A) and their layers' maps (UnpackLayerMap, $A068), by their addresses.

Format, big-endian: "WBMWPAK1"; the image's size n (4 bytes), the image, the mask (n bytes, 1 for
a byte the files hold); 768 graphics blocks of 1,024 bytes; the block tables: their number, then
each: its address (4), its count (2), and a record a block (1 if the block is there, 0 if it is
left as it was; its 4 tile words; its attribute byte); the layers: their number, then each: its
data's address (4), its block table's address (4), its places' number (1), and each place in the
data's order: its place on the map (1) and its chunk's 192 cells (words).
"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(__file__))
import rebuild, extract, gfxpng, pngio, msgtext

SIZE = 0xC0000                                      # the ROM's address space: 786,432 bytes

def read_blocks(assets):
    """the 768 graphics blocks: from their pictures (graphics/png, gfxpng.py), which are what one
    edits; a block without one from its .bin. None, and why, if a picture cannot be read"""
    g = os.path.join(assets, 'graphics')
    index_path = os.path.join(g, 'png', 'blocks.json')
    index = json.load(open(index_path)) if os.path.exists(index_path) else {}
    blocks, bad = [], 0
    for i in range(768):
        png, info = os.path.join(g, 'png', 'block_%03d.png' % i), index.get('%03d' % i)
        if info and os.path.exists(png):
            try:
                blocks.append(gfxpng.read_block(png, [tuple(bytes.fromhex(c)) for c in info['colours']], info['layout']))
                continue
            except (pngio.PngError, OSError, ValueError, KeyError) as e:
                print('pack: graphics/png/block_%03d.png: %s' % (i, e)); bad += 1
        blocks.append(open(os.path.join(g, 'block_%03d.bin' % i), 'rb').read())
    if bad: print('pack: %d pictures could not be read, nothing packed (docs/MODDING.md, "Graphics")' % bad)
    return None if bad else blocks

def conflicts(found):
    """Every piece goes back at its own address, so one made longer by an edit runs into the next
    and breaks it without a word (docs/MODDING.md). The game's own data shares bytes (a phrase
    listed twice, a terminator two lists end on), so pieces may overlap where they agree; two that
    disagree about a byte are an error: say so and pack nothing. 1 if any"""
    for what, other, a in found[:20]:
        print('pack: %s and %s disagree from $%06X: one of them was made longer?' % (what, other, a)
              if other else 'pack: %s runs past the end of the image' % what)
    if found:
        print('pack: %d conflicts, nothing packed. An edited piece can only be as long as it was for now:\n'
              '      shorten it (docs/MODDING.md, "What can be changed").' % len(found))
    return bool(found)

def main():
    assets = sys.argv[1] if len(sys.argv) > 1 else 'assets'
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(assets, 'game.pak')
    J = lambda *p: json.load(open(os.path.join(assets, *p), encoding='utf-8'))
    fmt = J('manifest.json').get('format') if os.path.exists(os.path.join(assets, 'manifest.json')) else None
    if fmt != extract.FORMAT:                       # written by an older extract.py, or not at all
        print('%s: written by another version of tools/extract.py (format %s, this one wants %d): extract again'
              % (assets, fmt, extract.FORMAT))
        return 3
    try: built = rebuild.build_pieces(J, assets)
    except msgtext.TextError as e:
        print('pack: text/messages.txt: %s\npack: nothing packed (docs/MODDING.md, "Text")' % e); return 1
    # the image: the ROM's address space, and past it what has moved there (msgtext.EXTENSION)
    size = max([SIZE] + [a + len(b) for _, ps in built for a, b in ps])
    size = (size + 0xFFFF) & ~0xFFFF
    if size > 0x400000:
        print('pack: the data reaches $%06X, past the cartridge\'s 4 MB: nothing packed' % size); return 1
    image, mask = bytearray(size), bytearray(size)
    owner, found = {}, []                           # who wrote a byte (where they disagree), conflicts

    def put(a, b, what):
        if a + len(b) > size: found.append((what, None, a)); return
        old = image[a:a + len(b)]
        for k in range(len(b)):
            if mask[a + k] and old[k] != b[k]:
                found.append((what, owner.get(a + k, 'another piece'), a + k)); break
        image[a:a + len(b)] = b
        mask[a:a + len(b)] = b'\x01' * len(b)
        for k in range(len(b)): owner.setdefault(a + k, what)

    pieces = 0
    for name, ps in built:
        for a, b in ps: put(a, b, '%s at $%06X (%d bytes)' % (name, a, len(b))); pieces += 1
    blocks = read_blocks(assets)
    if blocks is None: return 1
    for i, e in enumerate(J('graphics', 'table.json')):
        e = int(e, 16)
        if e != 0xFFFFFFFF and e >> 24 & 1: put(e & 0xFFFFFF, blocks[i], 'graphics block %d (raw)' % i)
    if conflicts(found): return 1

    tables = {}
    for t in J('rooms', 'shared_blocks.json'): tables[int(t['address'], 16)] = t['blocks']
    layers = []
    for r in J('rooms', 'maps.json'):
        for lay in r['layers']:
            bt = int(lay['blocks_at'], 16); tables[bt] = lay['blocks']
            layers.append((int(lay['src'], 16), bt, lay['places']))
    layers = list({src: (src, bt, pl) for src, bt, pl in layers}.values())

    with open(out, 'wb') as f:
        f.write(b"WBMWPAK1" + struct.pack(">I", size) + image + mask)
        for b in blocks: f.write(b)
        f.write(struct.pack('>I', len(tables)))
        for a, bl in sorted(tables.items()):
            f.write(struct.pack('>IH', a, len(bl)))
            for b in bl: f.write(b'\x00' * 10 if b is None else b'\x01' + struct.pack('>4HB', *b))
        f.write(struct.pack('>I', len(layers)))
        for src, bt, places in sorted(layers):
            f.write(struct.pack('>IIB', src, bt, len(places)))
            for pos, cells in places.items(): f.write(bytes([int(pos)]) + struct.pack('>192H', *cells))
    print('%s: %d pieces, %d bytes of the image held; %d block tables, %d layers' %
          (out, pieces, sum(mask), len(tables), len(layers)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
