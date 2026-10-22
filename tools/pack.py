"""Pack the files tools/extract.py wrote into the one file the game loads its data from
(docs/EXTRACT.md, "The game reads the files").

Usage: pack.py [ASSETS] [OUT]          (defaults: assets, ASSETS/game.pak)

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
import rebuild, gfx

SIZE = 0xC0000                                      # the ROM's address space: 786,432 bytes

def main():
    assets = sys.argv[1] if len(sys.argv) > 1 else 'assets'
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(assets, 'game.pak')
    J = lambda *p: json.load(open(os.path.join(assets, *p), encoding='utf-8'))
    image, mask = bytearray(SIZE), bytearray(SIZE)

    def put(a, b):
        image[a:a + len(b)] = b
        mask[a:a + len(b)] = b'\x01' * len(b)

    pieces = 0
    for name, ps in rebuild.build_pieces(J, assets):
        for a, b in ps: put(a, b); pieces += 1
    blocks = [open(os.path.join(assets, 'graphics', 'block_%03d.bin' % i), 'rb').read() for i in range(768)]
    for i, e in enumerate(J('graphics', 'table.json')):
        e = int(e, 16)
        if e != 0xFFFFFFFF and e >> 24 & 1: put(e & 0xFFFFFF, blocks[i])     # raw: read as it is

    tables = {}
    for t in J('rooms', 'shared_blocks.json'): tables[int(t['address'], 16)] = t['blocks']
    layers = []
    for r in J('rooms', 'maps.json'):
        for lay in r['layers']:
            bt = int(lay['blocks_at'], 16); tables[bt] = lay['blocks']
            layers.append((int(lay['src'], 16), bt, lay['places']))
    layers = list({src: (src, bt, pl) for src, bt, pl in layers}.values())

    with open(out, 'wb') as f:
        f.write(b'WBMWPAK1' + struct.pack('>I', SIZE) + image + mask)
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
