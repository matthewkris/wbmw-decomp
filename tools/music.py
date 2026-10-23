"""The music and sound effects: the Z80 driver's data (docs/SOUND.md).

Usage: music.py ROM               (prints what it finds and the checks)

The driver (ROM $98000, run from Z80 RAM) sees the ROM's $98000-$9FFFF through its bank window at
Z80 $8000, so a Z80 address $8000 and up is ROM $98000 + (it - $8000). Its sound table at Z80
$9B01 (ROM $99B01) has a pointer for each sound command (the 68000's QueueSound, $366: $00-$57):

* a song: a priority byte with bit 7 set, the tempo (a word: timer B's step, $1C14), then its
  tracks' pointers to a zero word ($01E2-$0212);
* a sound effect: a priority byte, then (channel byte, track pointer) pairs to an $FF ($022D).

A track is read by $0C18: under $60 a note that then takes the step's length; $60-$BF a note
that takes no time (sounded with the next); $C0-$CF a length from the table at $0E35 (times 3);
$D0-$D7 call: the word after it, 1-8 times; $D8-$DF loop start, 1-8 times; $E0-$FF a command
(the table at $0E62): see CMDS.
"""
import sys, struct

BANK = 0x98000                                      # ROM address of Z80 $8000
TABLE = 0x9B01                                      # the sound table (Z80)
SOUNDS = 0x58                                       # $00-$57; $58 up are ignored ($01D5)
DATA = (0x99A77, 0x9F777)                           # the data after the driver, in the ROM

def rom(z): return BANK + z - 0x8000
def zaddr(r): return r - BANK + 0x8000

# $E0-$FF: name, argument format ('b' byte, 'w' word, 'j' a jump target: a word, absolute Z80)
CMDS = {0xE0: ('slide', 'b'),       # $0EA2: slide speed (-$0F), slides on (bit 5)
        0xE1: ('slide_off', ''),    # $0EAC
        0xE2: ('length', 'b'),      # $0EB1: the step's length, a byte (times 3)
        0xE3: ('length_w', 'w'),    # $0EB8: the same, a word
        0xE4: ('tempo', 'w'),       # $0ECC: timer B's step ($1C14)
        0xF0: ('instrument', 'b'),  # $0ED7: (+$09)
        0xF1: ('gate', 'b'),        # $0EDD: how much of the step a note sounds, in 32nds (+$0B; $0DBE)
        0xF2: ('detune', 'w'),      # $0EE3: pitch offset, 1/256 of a note (+$0C)
        0xF3: ('detune_add', 'w'),  # $0EEE
        0xF4: ('volume', 'b'),      # $0EFF: attenuation, higher is quieter (+$0E: the tables at $1200/$1240)
        0xF5: ('volume_add', 'b'),  # $0F05
        0xF6: ('voice_type', 'b'),  # $0F0E: 0 FM, 2 PSG tone, 4 noise (+$08; +$0A its voices: $3F, $07, $01)
        0xF7: ('voices', 'b'),      # $0F25: which voices of the type it may take, a bit each (+$0A; $0393)
        0xF8: ('drums', ''),        # $0F2B: drum mode on/off (+$00 bit 0: notes from $19F1)
        0xF9: ('pan', 'b'),         # $0F34: FM left and right (+$11 bits 6-7)
        0xFA: ('rest', ''),         # $0F41: the step's length without a note
        0xFB: ('key_off', ''),      # $0F45
        0xFC: ('tie', ''),          # $0F4B: the next note carries on ($1C1A)
        0xFD: ('goto', 'j'),        # $0F51
        0xFE: ('loop_end', ''),     # $0F56: back to the loop start until its count runs out
        0xFF: ('end', '')}          # $0F66: return from a call, or the track's end
for c in range(0xE5, 0xF0): CMDS[c] = ('nop_%02x' % c, '')   # $0EAB: nothing

def track_parse(d, z):
    """a track's bytes from Z80 address z: (items, end, targets). It ends at an end ($FF) or a
    goto, past any loop; targets are the calls' and gotos' addresses"""
    p, items, targets = rom(z), [], []
    while True:
        c = d[p]; p += 1
        if c < 0xC0: items.append(dict(note=c)); continue
        if c < 0xD0: items.append(dict(len=c & 0xF)); continue
        if c < 0xD8:
            t = struct.unpack_from('<H', d, p)[0]; p += 2
            items.append(dict(call=t, times=c - 0xCF)); targets.append(t); continue
        if c < 0xE0: items.append(dict(loop=c - 0xD7)); continue
        name, fmt = CMDS[c]
        args = []
        for f in fmt:
            if f == 'b': args.append(d[p]); p += 1
            else: args.append(struct.unpack_from('<H', d, p)[0]); p += 2
        items.append(dict(cmd=name, args=args) if args else dict(cmd=name))
        if c == 0xFD: targets.append(args[0])
        if c in (0xFD, 0xFF): return items, zaddr(p), targets

def sounds(d):
    """the sound table: [(number, header address, kind, priority, tempo or None, [(channel, track)])]"""
    out = []
    for n in range(SOUNDS):
        h = struct.unpack_from('<H', d, rom(TABLE) + 2 * n)[0]
        p = rom(h); pri = d[p]; p += 1
        if pri & 0x80:
            tempo = struct.unpack_from('<H', d, p)[0]; p += 2
            tracks = []
            while True:
                t = struct.unpack_from('<H', d, p)[0]; p += 2
                if not t: break
                tracks.append((len(tracks), t))
            out.append((n, h, 'song', pri, tempo, tracks, zaddr(p)))
        else:
            tracks = []
            while d[p] != 0xFF:
                tracks.append((d[p], struct.unpack_from('<H', d, p + 1)[0])); p += 3
            out.append((n, h, 'effect', pri, None, tracks, zaddr(p + 1)))
    return out

def item_size(it):
    if 'note' in it or 'len' in it or 'loop' in it: return 1
    if 'call' in it: return 3
    c = next(k for k, v in CMDS.items() if v[0] == it['cmd'])
    return 1 + sum(1 if f == 'b' else 2 for f in CMDS[c][1])

def pieces(d):
    """every track and section the sounds reach: {address: (items, end, labels)}. A target inside
    another piece (a goto back into a track, a call into one) is a label of it, at an item's start"""
    found, todo = {}, [t for s in sounds(d) for _, t in s[5]]
    while todo:
        z = todo.pop()
        if z in found: continue
        items, end, targets = track_parse(d, z)
        found[z] = (items, end)
        todo += targets
    out = {}
    for z in sorted(found, key=lambda z: (z, -found[z][1])):
        holder = next((h for h in out if h < z < out[h][1]), None)
        if holder is None: out[z] = found[z] + ([],); continue
        q, starts = holder, set()
        for it in out[holder][0]: starts.add(q); q += item_size(it)
        if z not in starts or found[z][1] > out[holder][1]:
            raise ValueError('$%04X lies inside $%04X but not at one of its items' % (z, holder))
        out[holder][2].append(z)
    return out

def unused(d, used):
    """tracks nothing points at, in the gaps between the used data: (address, items, end) for each
    gap that parses as a track ending exactly at its end"""
    out = []
    for a, n in used:
        found, z = [], zaddr(a)
        try:
            while z < zaddr(a + n):
                items, end, _ = track_parse(d, z)
                found.append((z, items, end)); z = end
        except KeyError: continue
        if z == zaddr(a + n): out += found
    return out

def track_encode(items):
    out = bytearray()
    names = {v[0]: (k, v[1]) for k, v in CMDS.items()}
    for it in items:
        if 'note' in it: out.append(it['note'])
        elif 'len' in it: out.append(0xC0 | it['len'])
        elif 'call' in it: out.append(0xCF + it['times']); out += struct.pack('<H', it['call'])
        elif 'loop' in it: out.append(0xD7 + it['loop'])
        else:
            c, fmt = names[it['cmd']]
            out.append(c)
            for f, v in zip(fmt, it.get('args', [])): out += bytes([v]) if f == 'b' else struct.pack('<H', v)
    return bytes(out)

def header_encode(kind, pri, tempo, tracks):
    if kind == 'song':
        return bytes([pri]) + struct.pack('<H', tempo) + b''.join(struct.pack('<H', t) for _, t in tracks) + bytes(2)
    return bytes([pri]) + b''.join(bytes([ch]) + struct.pack('<H', t) for ch, t in tracks) + bytes([0xFF])

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
def note_name(n):
    """note n: block n // 12, semitone n % 12 ($0516); the frequency table ($107B) starts at a C"""
    return '%s%d' % (NAMES[n % 12], n // 12)

def track_text(items, z, labels=()):
    """a track as text: notes by name (+ a note that takes no time), lN a length, [N ... ] a loop,
    call and goto by address, the rest by command name"""
    out, q = [], z
    for it in items:
        if q in labels: out.append('@%04X:' % q)
        if 'note' in it: n = it['note']; out.append(('+' + note_name(n - 0x60)) if n >= 0x60 else note_name(n))
        elif 'len' in it: out.append('l%d' % it['len'])
        elif 'call' in it: out.append('call $%04X x%d' % (it['call'], it['times']))
        elif 'loop' in it: out.append('[%d' % it['loop'])
        elif it['cmd'] == 'loop_end': out.append(']')
        elif it['cmd'] == 'goto': out.append('goto $%04X' % it['args'][0])
        else: out.append(it['cmd'] + ''.join(' %d' % a for a in it.get('args', [])))
        q += item_size(it)
    return ' '.join(out)

def extract(d):
    """everything in the data region, for assets/sound/music.json, and the text for music.txt"""
    S, P = sounds(d), pieces(d)
    used, i = [], DATA[0]
    cov = set()
    for a, b in [(TABLE, TABLE + 2 * SOUNDS)] + [(s[1], s[6]) for s in S] + [(z, v[1]) for z, v in P.items()]:
        cov.update(range(rom(a), rom(b)))
    gaps, i = [], DATA[0]
    while i < DATA[1]:
        if i in cov: i += 1; continue
        j = i
        while j < DATA[1] and j not in cov: j += 1
        gaps.append((i, j - i)); i = j
    U = unused(d, gaps[1:])                          # (the first is the header, before the table)
    J = dict(header=d[DATA[0]:rom(TABLE)].hex(),
             sounds=[dict(number=n, address='%04X' % h, kind=k, priority=pri, tempo=tempo,
                          tracks=[[ch, '%04X' % t] for ch, t in tr]) for n, h, k, pri, tempo, tr, _ in S],
             tracks={'%04X' % z: dict(items=items, end='%04X' % end, labels=['%04X' % l for l in sorted(labels)])
                     for z, (items, end, labels) in sorted(P.items())},
             unused={'%04X' % z: dict(items=items, end='%04X' % end) for z, items, end in U})
    lines = ['# The music and sound effects (tools/music.py; docs/SOUND.md). Notes by name (note 0 is',
             '# C0, note 48 C4); +X sounds with the next note; lN a length; [N ... ] a loop.', '']
    names = {}
    for n, h, k, pri, tempo, tr, _ in S:
        for ch, t in tr: names.setdefault(t, []).append('%s $%02X %s %d' % (k, n, 'track' if k == 'song' else 'channel', ch))
        lines.append('%s $%02X at $%04X: priority $%02X%s, %d track%s' % (k, n, h, pri, ', tempo %d' % tempo if tempo is not None else '',
                                                                        len(tr), '' if len(tr) == 1 else 's'))
    lines.append('')
    for z, (items, end, labels) in sorted(P.items()):
        lines.append('$%04X (%s):' % (z, '; '.join(names.get(z, ['called or gone to']))))
        lines.append('  ' + track_text(items, z, set(labels)))
    for z, items, end in U:
        lines.append('$%04X (not used):' % z)
        lines.append('  ' + track_text(items, z))
    return J, '\n'.join(lines) + '\n'

def build(J):
    """the data region's pieces from music.json: [(ROM address, bytes)]"""
    S = J['sounds']
    out = [(DATA[0], bytes.fromhex(J['header'])),
           (rom(TABLE), b''.join(struct.pack('<H', int(s['address'], 16)) for s in S))]
    seen = set()
    for s in S:
        h = int(s['address'], 16)
        if h in seen: continue                       # (two sounds may share a header)
        seen.add(h)
        out.append((rom(h), header_encode(s['kind'], s['priority'], s['tempo'], [(ch, int(t, 16)) for ch, t in s['tracks']])))
    for part in ('tracks', 'unused'):
        for z, v in J[part].items(): out.append((rom(int(z, 16)), track_encode(v['items'])))
    return out

def main():
    d = open(sys.argv[1], 'rb').read()
    S = sounds(d); P = pieces(d)
    print('%d sounds: %d songs, %d effects; %d tracks and sections' %
          (len(S), sum(1 for s in S if s[2] == 'song'), sum(1 for s in S if s[2] == 'effect'), len(P)))
    cov = bytearray(DATA[1] - DATA[0])
    def mark(a, b):
        for i in range(rom(a), rom(b)):
            if DATA[0] <= i < DATA[1]: cov[i - DATA[0]] += 1
    mark(TABLE, TABLE + 2 * SOUNDS)
    for s in S: mark(s[1], s[6])
    for z, (items, end, labels) in P.items(): mark(z, end)
    print('labels inside pieces: %d' % sum(len(v[2]) for v in P.values()))
    over = sum(1 for v in cov if v > 1)
    gaps, i = [], 0
    while i < len(cov):
        if cov[i]: i += 1; continue
        j = i
        while j < len(cov) and not cov[j]: j += 1
        gaps.append((DATA[0] + i, j - i)); i = j
    U = unused(d, gaps)
    print('unused tracks: %d, %d bytes' % (len(U), sum(rom(e) - rom(z) for z, _, e in U)))
    for z, _, e in U: mark(z, e)
    gaps, i = [], 0
    while i < len(cov):
        if cov[i]: i += 1; continue
        j = i
        while j < len(cov) and not cov[j]: j += 1
        gaps.append((DATA[0] + i, j - i)); i = j
    print('covered %d of %d bytes; overlapping %d; gaps %d:' % (sum(1 for v in cov if v), len(cov), over, len(gaps)))
    bad = sum(1 for z, (items, end, _) in P.items() if track_encode(items) != d[rom(z):rom(end)])
    bad += sum(1 for z, items, end in U if track_encode(items) != d[rom(z):rom(end)])
    bad += sum(1 for s in S if header_encode(s[2], s[3], s[4], s[5]) != d[rom(s[1]):rom(s[6])])
    print('encoded back: %d differ' % bad)
    for a, n in gaps[:30]: print('  $%06X %d %s' % (a, n, d[a:a + min(n, 24)].hex(' ')))

if __name__ == '__main__':
    main()
