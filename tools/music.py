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

# songs the code queues itself (the rest come from the rooms' music, uses())
CODE_SONGS = {0x14: 'the title ($718A)',
              0x0D: "a boss's entrance: spawn point kind 12, when the player is below $1040 ($13380)",
              0x13: 'a boss beaten, outside area 18 ($176F4, called where three bosses are defeated: $17796, $17C20, $18560)',
              0x15: 'the ride of room script 13 ($D48C: area 16 room 4, area 18 room 6)',
              0x16: "the ending's script ($1B016)",
              0x54: 'the inn: a conversation ended by a fade, the night ($C1E8)'}

# the sound effects: a name, and where the game queues each (found in the C: the queue's calls with
# their numbers, the pickups' table at $10BD4, the ocarina's at $2B34, the messages' SOUND command;
# "not queued by the code": none found, though data could still name it)
EFFECTS = {
    0x18: ('ocarina note A (C5)', '$2B08: the ocarina played, button A'),
    0x19: ('ocarina note B (D5)', '$2B08: button B'),
    0x1A: ('ocarina note C (G5)', '$2B08: button C'),
    0x1B: ('ocarina tune 0, played whole (B A B A B C B)', 'not queued by the code'),
    0x1C: ('ocarina tune 1, played whole (A B C B C A B)', 'not queued by the code'),
    0x1D: ('ocarina tune 2, played whole (A C C A B A B)', 'not queued by the code'),
    0x1E: ('jump', '$EC08: leaving the ground upwards'),
    0x1F: ('an enemy hit', '$4986: an enemy hurt by the player; a darter hit ($17EE6)'),
    0x20: ('sword swing', '$ED5C: B swings the sword'),
    0x21: ('a low rumble, fading over four', 'not queued by the code'),
    0x22: ('a door closing', '$BD52: the door\'s frame, closing'),
    0x23: ('a door opening', '$BD0A: the door\'s frame, opening'),
    0x24: ('spirit magic', '$1194C: a spirit sets off'),
    0x25: ('cursor', 'a move in a menu or a message\'s choice ($8212, the title\'s menu $71C6, $13BC)'),
    0x26: ('confirm', 'a choice made ($13BC, the title\'s menu $71C6)'),
    0x27: ('gold dropped, rolling', '$10BD4: gold that rolls away appears (pickup kinds $00-$0A)'),
    0x28: ('gold or an item picked up', '$F1EC, $F256'),
    0x29: ('a heart picked up', '$F262'),
    0x2A: ('a thud', 'a sinking step ($12160), a weight ($1234C), a townsperson struck ($1649C)'),
    0x2B: ('a hit spark', '$10EC2: an attack\'s spark, when it hits'),
    0x2C: ('a chest opening', '$A666'),
    0x2D: ('a thing received', 'message 40: "Oh, it\'s the ...!"'),
    0x2E: ('quake magic', '$11590: the world stops and the screen shakes'),
    0x2F: ('a quake\'s rock falling', '$118BE'),
    0x30: ('a hit blocked', 'the shield\'s spark ($11868); a hit that only pushes back ($E04E, bit 4)'),
    0x31: ('up attack, or grabbing a ladder or vine', '$EA2A, $EC26'),
    0x32: ('a thing shaking', '$1221C'),
    0x33: ('a spring', 'a spring ($12298), a bounce tile ($DFA0), a lift ($B0DA)'),
    0x34: ('two low notes, alternating', 'not queued by the code'),
    0x35: ('buzz: cannot', 'the status screen: a line that cannot be chosen ($75F8, $7882, $78DC, $7D9C)'),
    0x36: ('the player hurt', '$E12E'),
    0x37: ('a shot', 'an enemy\'s shot ($15456, $18620); $DB36'),
    0x38: ('a room\'s message coming', '$9228: the screen fades for a message'),
    0x39: ('a crash: the screen shaken', '$183D4; Gragg and Glagg appearing ($15A86); a landing ($17DB8)'),
    0x3A: ('thunder magic', '$11170, kind 2'),
    0x3B: ('a seeker setting off', 'handler 25 ($1A0B0)'),
    0x3C: ('leaving by an exit', '$F76C; a beaten beast\'s rider thrown up ($18D74)'),
    0x3D: ('hearts refilling', '$F288; a message\'s command ($15C8)'),
    0x3E: ('healing at the inn', '$1BE6, $F2A6'),
    0x3F: ('a puff of debris', '$42EA, $19C34'),
    0x40: ('a picture appearing', '$CF5A: room script 9'),
    0x41: ('a lift starting', '$11FD2'),
    0x42: ('a lift stopping', '$11FD2'),
    0x43: ('the companion on the player\'s head', '$FC02: the companion, kind 0, state 10'),
    0x44: ('the player transformed', '$CA66'),
    0x45: ('a beaten beast\'s rider', '$18D74: the pair beaten'),
    0x46: ('pushed back by a hit', '$E04E, bit 6'),
    0x47: ('a splash', '$DD7C: into or out of the water'),
    0x48: ('gold appearing', '$10BD4: pickup kinds $12-$16'),
    0x49: ('the companion turning', '$FDAC: the companion, kind 0, state 6'),
    0x4A: ('a jump (C), swimming up, climbing', '$EB9E, $EC26'),
    0x4B: ('a wall crumbling', '$B400'),
    0x4C: ('room objects let out', '$C9B4: room script 0\'s release'),
    0x4D: ('shield magic', '$11170, kinds 4, 6, 7'),
    0x4E: ('a darter fired', 'handler 19 ($17EE6)'),
    0x4F: ('a message window opening', '$279C'),
    0x50: ('a diver diving', 'handler 24 ($19C86)'),
    0x51: ('the ride', '$D48C: room script 13'),
    0x52: ('a beaten boss\'s body', '$19946'),
    0x53: ('the player\'s death', '$E22E'),
    0x56: ('the status screen opening', '$901C: Start (the music paused first, $FC)'),
    0x57: ('a lift opened', '$2C4E: a puff and four sparks'),
}

def uses(d):
    """where each song plays: {song: [text]}, from the rooms' music (the directory's table 3,
    $92D4: a song, $80 + n a pair at $932A by a game flag, $EF by flags $4D/$4E ($DAC8), $F0 up
    the music as it was) and CODE_SONGS"""
    import rooms
    t3 = rooms.be32(d, rooms.DIR + 0xC) & 0xFFFFFF
    where = {}
    def add(song, text): where.setdefault(song, []).append(text)
    for area in range(19):
        base = t3 + rooms.sw(d, t3 + 2 * area)
        for room in range(rooms.room_count(d, area)):
            v = d[base + room]
            at = 'area %d room %d' % (area, room)
            if v < 0x80: add(v, at)
            elif v == 0xEF: add(0x0F, at + ' (game flag $4D or $4E set)'); add(0x55, at + ' (game flags $4D and $4E clear)')
            elif v < 0xF0:
                f, clear, set_ = d[0x932A + 3 * (v & 0x7F):0x932A + 3 * (v & 0x7F) + 3]
                add(clear, at + ' (game flag $%02X clear)' % f); add(set_, at + ' (game flag $%02X set)' % f)
    out = {}
    for song in sorted(set(where) | set(CODE_SONGS)):
        out[song] = ([CODE_SONGS[song]] if song in CODE_SONGS else []) + where.get(song, [])
    return out

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

def group(texts):
    """'area 2 room 0', 'area 2 room 1', ... as 'area 2 rooms 0, 1'; other texts as they are"""
    out, rooms_of = [], {}
    for t in texts:
        w = t.split(' ', 4)
        if len(w) >= 4 and w[0] == 'area' and w[2] == 'room':
            key = (int(w[1]), w[4] if len(w) > 4 else '')
            if key not in rooms_of: rooms_of[key] = []; out.append(key)
            rooms_of[key].append(w[3])
        else: out.append(t)
    return [t if isinstance(t, str) else 'area %d room%s %s%s' % (t[0], 's' if len(rooms_of[t]) > 1 else '', ', '.join(rooms_of[t]),
                                                                   ' ' + t[1] if t[1] else '') for t in out]

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
    W = uses(d)
    J = dict(header=d[DATA[0]:rom(TABLE)].hex(),
             sounds=[dict(number=n, address='%04X' % h, kind=k, priority=pri, tempo=tempo,
                          tracks=[[ch, '%04X' % t] for ch, t in tr],
                          **({'plays': W.get(n, [])} if k == 'song' else
                             {'name': EFFECTS[n][0], 'used': EFFECTS[n][1]} if n in EFFECTS else {}))
                     for n, h, k, pri, tempo, tr, _ in S],
             tracks={'%04X' % z: dict(items=items, end='%04X' % end, labels=['%04X' % l for l in sorted(labels)])
                     for z, (items, end, labels) in sorted(P.items())},
             unused={'%04X' % z: dict(items=items, end='%04X' % end) for z, items, end in U})
    lines = ['# The music and sound effects (tools/music.py; docs/SOUND.md). Notes by name, as the tracks',
             '# number them (note 0 C0, note 48 C4); what sounds is shifted by the instrument (most FM ones an',
             '# octave down) and, on the PSG, an octave up (the MIDI files, tools/midi.py, give',
             '# the pitch heard). +X sounds with the next note; lN a length; [N ... ] a loop.', '']
    names = {}
    for n, h, k, pri, tempo, tr, _ in S:
        for ch, t in tr: names.setdefault(t, []).append('%s $%02X%s %s %d' % (k, n, ' "%s"' % EFFECTS[n][0] if n in EFFECTS else '',
                                                                              'track' if k == 'song' else 'channel', ch))
        lines.append('%s $%02X at $%04X: priority $%02X%s, %d track%s' % (k, n, h, pri, ', tempo %d' % tempo if tempo is not None else '',
                                                                        len(tr), '' if len(tr) == 1 else 's'))
        if k == 'song': lines += ['    plays: ' + w for w in (group(W[n]) if n in W else ['nowhere found'])]
        elif n in EFFECTS: lines.append('    %s: %s' % EFFECTS[n])
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
