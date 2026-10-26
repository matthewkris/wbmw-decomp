"""The music as MIDI files, from the extracted files (docs/SOUND.md, "MIDI").

Usage: midi.py [ASSETS] [OUT] [--loops N] [--effects]
       (defaults: assets, ASSETS/sound/midi; N 2: a song's loop played twice)

Each song's tracks are played as the driver plays them (tools/music.py's items: lengths, loops,
calls, notes that sound with the next, ties, the gate, rests, key offs, the detune's whole notes,
tempo changes) into one MIDI file, a MIDI track for each of the song's tracks. With --effects the
sound effects are written too (sound_NN.mid).

Time: the driver steps its tracks every three ticks of the YM2612's timer B ($ED: 175.3 Hz),
a song's track by what its tempo added up over them (tempo / 256 a tick: $00C2) and an effect's
by 9; a length is the table's byte times 3 ($0C4C). A MIDI tick here is one of those units, 144
to a quarter note (the common length, 72, an eighth), the tempo set to match.
Pitch: a note splits into a block (n / 12) and a semitone ($0516); FM note 0 is C0 (frequency
number 654 at block 0), so MIDI note n + 12; a PSG note 0 is C3 (period 841), so n + 48.
Not carried over: the instruments' sounds (the chips' instruments have no General MIDI match: FM
tracks get a lead or, low, a synth bass, the PSG a square lead, and each track's name says its FM
instrument), the slides, the modulation, the detune's fraction and the pan.
"""
import sys, os, json, struct, math, argparse

TIMER_HZ = 7670454 / (144 * 16 * (256 - 0xED))     # timer B's rate: 175.27 a second
PPQ = 144                                           # MIDI ticks (driver units) to a quarter note
LENGTHS = [0x06, 0x09, 0x0C, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x90, 0xC0, 0x08, 0x10, 0x20, 0x3C, 0x78]   # $0E35
DRUM_CHANNEL = 9

def units_per_second(tempo, song=True):
    return TIMER_HZ * tempo / 256 if song else TIMER_HZ * 9 / 3

def play(J, z, song=True, loops=2, inst=None):
    """one track from Z80 address z: [(start, end, midi note, velocity, drum)], [(time, tempo)], its
    program and name. Time in driver units."""
    tracks = {int(a, 16): v for a, v in J['tracks'].items()}
    tracks.update({int(a, 16): v for a, v in J.get('unused', {}).items()})
    starts = {}                                     # address -> (track address, item index)
    for a, v in tracks.items():
        q = a
        for i, it in enumerate(v['items']):
            starts[q] = (a, i); q += size(it)
    pos = starts[z]
    t, length, gate = 0, 0x18 * 3, 8
    voice, instrument, volume, detune, drums, tie = 0, 0, 0, 0, False, False
    calls, loops_open = [], []
    pending, out, tempos, gotos, steps = [], [], [], 0, 0
    last = None                                     # the note sounding (an index into out), for ties and key offs
    used = set()
    def velocity():
        if not inst: return 100
        if voice == 0: db = -inst['attenuation_fm'][min(volume >> 1, 63)] * 0.75
        else: db = -2 * (inst['attenuation_psg'][min(volume >> 1, 63)] >> 3)
        return max(1, min(127, round(127 * 10 ** (db / 40))))
    def midi_note(n):
        if drums:
            d = inst['drums'][n] if inst and n < len(inst['drums']) else None
            if not d: return 38, True
            if d['voice_type'] == 4: return 42, True
            return (36 if (d['pitch'] >> 8) < 36 else 38), True
        if voice == 4: return 42, True
        semis = lambda w: (w >> 8) - 256 if w & 0x8000 else w >> 8      # a pitch word's whole notes
        if voice == 0:                              # the FM instrument's own offset (+$26/+$27, $0A68): mostly -12
            f = inst['fm_instruments'][instrument] if inst and instrument < len(inst['fm_instruments']) else None
            shift = semis(f['pitch_hi'] << 8 | f['pitch_lo']) if f else 0
            return n + semis(detune) + shift + 12, False
        return n + semis(detune) - 24 + 48, False   # the PSG's offset is always -24 ($0AFC)
    while steps < 20000:
        steps += 1
        a, i = pos
        items = tracks[a]['items']
        if i >= len(items): break
        it = items[i]; pos = (a, i + 1)
        if 'note' in it or ('cmd' in it and it['cmd'] == 'rest'):
            timed = 'cmd' in it or it['note'] < 0x60
            if 'note' in it:
                n, drum = midi_note(it['note'] - (0 if it['note'] < 0x60 else 0x60))
                if timed and tie and last is not None and out[last][2] == n:
                    out[last][1] = t + length                       # carried on
                else:
                    pending.append([t, None, n, velocity(), drum])
                used.add(instrument if voice == 0 else -1)
            tie = False
            if timed:
                sound = length * (gate & 31) // 32 if gate & 31 else length
                for p in pending:
                    p[1] = t + max(1, sound); out.append(p); last = len(out) - 1
                pending = []
                t += length
            continue
        if 'len' in it: length = LENGTHS[it['len']] * 3; continue
        if 'loop' in it: loops_open.append([pos, it['loop']]); continue
        if 'call' in it:
            calls.append([pos, it['times'], it['call']]); pos = starts[it['call']]; continue
        c, args = it['cmd'], it.get('args', [])
        if c == 'length': length = args[0] * 3
        elif c == 'length_w': length = args[0] * 3
        elif c == 'tempo': tempos.append((t, args[0]))
        elif c == 'instrument': instrument = args[0]
        elif c == 'gate': gate = args[0]
        elif c == 'detune': detune = args[0]
        elif c == 'detune_add': detune = (detune + args[0]) & 0xFFFF
        elif c == 'volume': volume = args[0]
        elif c == 'volume_add': volume = (volume + args[0]) & 0xFF
        elif c == 'voice_type': voice = args[0]
        elif c == 'drums': drums = not drums
        elif c == 'tie': tie = True
        elif c == 'key_off':
            if last is not None and out[last][1] > t: out[last][1] = t
        elif c == 'loop_end':
            if loops_open:
                loops_open[-1][1] -= 1
                if loops_open[-1][1]: pos = loops_open[-1][0]
                else: loops_open.pop()
        elif c == 'goto':
            gotos += 1
            if gotos >= loops: break
            pos = starts[args[0]]
        elif c == 'end':
            if not calls: break
            calls[-1][1] -= 1
            if calls[-1][1]: pos = starts[calls[-1][2]]
            else: pos = calls.pop()[0]
    melodic = [n for s, e, n, v, d in out if not d]
    low = melodic and sum(melodic) / len(melodic) < 48
    program = 80 if voice == 2 else 38 if low else 81
    name = ('PSG' if voice == 2 else 'noise' if voice == 4 else 'FM instrument %s' % ', '.join(str(x) for x in sorted(used) if x >= 0))
    return out, tempos, program, name

def size(it):
    import music
    return music.item_size(it)

def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n: b.append(0x80 | (n & 0x7F)); n >>= 7
    return bytes(reversed(b))

def track_chunk(events):
    """events: [(tick, bytes)] -> an MTrk chunk"""
    data, now = bytearray(), 0
    for tick, ev in sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90)):   # offs before ons at a tick
        data += vlq(tick - now) + ev; now = tick
    data += vlq(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data

def song_midi(J, inst, sound, loops):
    song = sound['kind'] == 'song'
    tracks = [int(t, 16) for _, t in sound['tracks']]
    played = [play(J, z, song, loops, inst) for z in tracks]
    first = sound['tempo'] if song else None
    tempo_map = [(0, first)] + sorted(x for p in played for x in p[1]) if song else [(0, None)]
    def tempo_event(tempo):
        us = round(PPQ * 1e6 / units_per_second(tempo, song)) if song else round(PPQ * 1e6 / units_per_second(0, False))
        return b'\xff\x51\x03' + us.to_bytes(3, 'big')
    head = [(t, tempo_event(v)) for t, v in tempo_map]
    title = ('song %02X' if song else 'sound %02X') % sound['number'] + (': ' + sound['name'] if sound.get('name') else '')
    title = title.encode('ascii', 'replace')
    head.append((0, b'\xff\x03' + vlq(len(title)) + title))
    chunks = [track_chunk(head)]
    ch = 0
    for k, (out, _, program, name) in enumerate(played):
        while ch == DRUM_CHANNEL: ch += 1
        mine = ch % 16; ch += 1
        ev = [(0, b'\xff\x03' + vlq(len(name)) + name.encode()), (0, bytes([0xC0 | mine, program]))]
        for s, e, n, v, drum in out:
            c = DRUM_CHANNEL if drum else mine
            if not 0 <= n < 128: continue
            ev.append((s, bytes([0x90 | c, n, v]))); ev.append((e, bytes([0x80 | c, n, 0])))
        chunks.append(track_chunk(ev))
    return b'MThd' + struct.pack('>IHHH', 6, 1, len(chunks), PPQ) + b''.join(chunks)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('assets', nargs='?', default='assets'); ap.add_argument('out', nargs='?')
    ap.add_argument('--loops', type=int, default=2); ap.add_argument('--effects', action='store_true')
    A = ap.parse_args()
    sys.path.insert(0, os.path.dirname(__file__))
    J = json.load(open(os.path.join(A.assets, 'sound', 'music.json'), encoding='utf-8'))
    inst = json.load(open(os.path.join(A.assets, 'sound', 'instruments.json'), encoding='utf-8'))
    out = A.out or os.path.join(A.assets, 'sound', 'midi')
    os.makedirs(out, exist_ok=True)
    n = 0
    for s in J['sounds']:
        if s['kind'] != 'song' and not A.effects: continue
        name = ('song_%02X.mid' if s['kind'] == 'song' else 'sound_%02X.mid') % s['number']
        open(os.path.join(out, name), 'wb').write(song_midi(J, inst, s, A.loops)); n += 1
    print('wrote %d MIDI files to %s' % (n, out))

if __name__ == '__main__':
    main()
