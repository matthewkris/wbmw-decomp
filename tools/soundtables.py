"""The sound driver's own tables (docs/SOUND.md): its data after the code, Z80 $107B-$1A76.

Usage: soundtables.py ROM          (prints the checks)

parse(drv) reads them from the driver's bytes as the code reads them, encode(J) builds them back.
Each table's place and size is the code's (the addresses in the comments); a byte between two
tables that nothing reads is kept as it is (`filler`).
"""
import sys, struct

CODE_END = 0x107B                   # the driver's code (with its small tables in it) ends here
END = 0x1A77                        # the driver's end (6,775 bytes)
FM_FREQ = (0x107B, 97)              # $0586: FM frequency numbers, 8 steps a semitone, from a C (words)
PSG_FREQ = (0x113D, 97)             # $0549: PSG periods, the same steps (words)
ATTEN = (0x1200, 0x1240, 64)        # $0E1B: a track's volume to attenuation, FM ($1200: a TL) and PSG ($1240: steps of 8)
FM_INSTR = (0x1281, 51, 19)         # $0A08: 19 bytes each
ENVELOPES = (0x164B, 119)           # $0698: an operator's envelope, YM registers $50, $60, $70, $80
PSG_INSTR = (0x1828, 17, 6)         # $0AB2: 6 bytes each
PSG_ENV = (0x188F, 8)               # $0AD6: pointers to the volume envelopes ($09CF reads them)
MOD_A = (0x1901, 14)                # $0B0B: pointers to modulation scripts (the voice's +$10)
MOD_B = (0x19D8, 3)                 # $0B1E: pointers to modulation scripts (the voice's +$20)
DRUMS = (0x19F1, 22, 6)             # $0CB3: a drum note: voice type, voices, instrument, byte, pitch (word)
OPS = ('op1', 'op3', 'op2', 'op4')  # $0A55: the operators in the YM2612's register order

w16 = lambda d, a: d[a] | d[a + 1] << 8
s8 = lambda v: v - 256 if v & 0x80 else v
s16 = lambda v: v - 65536 if v & 0x8000 else v

# ---- the modulation scripts. Kind A (`mod_a`, $07D7) moves the voice's pitch offset (+$14, added
# to the note: 1/256 of a note), kind B (`mod_b`, $08F3) its volume offset (+$1C, added to the
# attenuation). Every `rate` frames (+$3A / +$38) the offset moves by the step's delta, for the
# step's count; then the next item is read. Under the first command byte: n, then the delta (A a
# word, B a byte), n steps of it. The commands: ----
MOD_CMDS = {0xFF: 'goto',          # a word: the script goes on there
            0xFE: 'loop_end',      # back to the loop's start until its count runs out
            0xFD: 'loop',          # a count: the loop's start
            0xFC: 'reverse',       # the delta turned round, for a count of steps
            0xFB: 'set',           # the offset set (A a word, B a byte)
            0xFA: 'stop',          # no more change; the offset held
            0xF9: 'rate',          # the frames between steps
            0xF8: 'random'}        # (A only) a random offset of a depth (a shift), for a count of steps ($0B66)

def mod_parse(b, kind, at):
    """a script's bytes (from Z80 address at) as items; None if they do not parse to their end"""
    items, p, first = [], 0, 0xF8 if kind == 'a' else 0xF9
    done = None                                     # (items, place) after the last item that ends the script
    try:
        while p < len(b):
            if items and items[-1].get('cmd') in ('end', 'stop', 'goto'): done = (len(items), p)
            c = b[p]; p += 1
            if c == 0: items.append(dict(cmd='end')); continue
            if c < first:
                if kind == 'a': items.append(dict(steps=c, delta=s16(w16(b, p)))); p += 2
                else: items.append(dict(steps=c, delta=s8(b[p]))); p += 1
                continue
            name = MOD_CMDS[c]
            if name == 'goto': items.append(dict(cmd=name, to='%04X' % w16(b, p))); p += 2
            elif name in ('loop', 'reverse', 'rate'): items.append(dict(cmd=name, n=b[p])); p += 1
            elif name == 'set':
                if kind == 'a': items.append(dict(cmd=name, value=s16(w16(b, p)))); p += 2
                else: items.append(dict(cmd=name, value=s8(b[p]))); p += 1
            elif name == 'random': items.append(dict(cmd=name, depth=b[p], steps=b[p + 1])); p += 2
            else: items.append(dict(cmd=name))
        if p != len(b): raise IndexError
    except (IndexError, KeyError):
        # bytes after the script's end that do not parse (never read: $19D3's last, whose step
        # would run into the next table) kept as they are
        if done is None: return None
        return items[:done[0]] + [dict(tail=b[done[1]:].hex())]
    return items

def mod_encode(items, kind):
    out = bytearray()
    codes = {v: k for k, v in MOD_CMDS.items()}
    for it in items:
        if 'tail' in it: out += bytes.fromhex(it['tail']); continue
        if 'steps' in it and 'cmd' not in it:
            out.append(it['steps'])
            out += struct.pack('<h', it['delta']) if kind == 'a' else struct.pack('<b', it['delta'])
            continue
        c = it['cmd']
        if c == 'end': out.append(0); continue
        out.append(codes[c])
        if c == 'goto': out += struct.pack('<H', int(it['to'], 16))
        elif c in ('loop', 'reverse', 'rate'): out.append(it['n'])
        elif c == 'set': out += struct.pack('<h', it['value']) if kind == 'a' else struct.pack('<b', it['value'])
        elif c == 'random': out += bytes([it['depth'], it['steps']])
    return bytes(out)

# a PSG volume envelope ($09CF): a byte a step, its high nibble the attenuation (times 8, added to
# the volume's), its low nibble the frames it lasts (0: held); $F0 the end (the voice let go, $0460)
def env_parse(b):
    return [['end'] if v == 0xF0 else [v >> 4, v & 15] for v in b]

def env_encode(steps):
    return bytes(0xF0 if s == ['end'] else s[0] << 4 | s[1] for s in steps)

def parse(drv):
    J, used = {}, set()
    def take(a, n):
        used.update(range(a, a + n)); return drv[a:a + n]
    J['fm_frequencies'] = [w16(drv, FM_FREQ[0] + 2 * i) for i in range(FM_FREQ[1])]; take(FM_FREQ[0], 2 * FM_FREQ[1])
    J['psg_periods'] = [w16(drv, PSG_FREQ[0] + 2 * i) for i in range(PSG_FREQ[1])]; take(PSG_FREQ[0], 2 * PSG_FREQ[1])
    J['attenuation_fm'] = list(take(ATTEN[0], ATTEN[2]))
    J['attenuation_psg'] = list(take(ATTEN[1], ATTEN[2]))
    fm = []
    for i in range(FM_INSTR[1]):
        r = take(FM_INSTR[0] + FM_INSTR[2] * i, FM_INSTR[2])
        fm.append(dict(algorithm=r[0] & 7, feedback=r[0] >> 3 & 7, b0_high=r[0] >> 6, level=r[1], lfo=r[2],
                       operators={OPS[k]: dict(level=r[3 + 3 * k], detune_multiple=r[4 + 3 * k], envelope=r[5 + 3 * k]) for k in range(4)},
                       pitch_lo=r[15], pitch_hi=r[16], mod_a=r[17], mod_b=r[18]))
    J['fm_instruments'] = fm
    J['envelopes'] = [list(take(ENVELOPES[0] + 4 * i, 4)) for i in range(ENVELOPES[1])]
    J['psg_instruments'] = [dict(zip(('b0', 'b1', 'b2', 'envelope', 'mod_a', 'mod_b'), take(PSG_INSTR[0] + 6 * i, 6)))
                            for i in range(PSG_INSTR[1])]
    for name, (tab, n), stop in (('psg_envelopes', PSG_ENV, MOD_A[0]), ('mod_a', MOD_A, MOD_B[0]), ('mod_b', MOD_B, DRUMS[0])):
        ptrs = [w16(drv, tab + 2 * i) for i in range(n)]; take(tab, 2 * n)
        ends = sorted(set(ptrs) | {stop})
        J[name] = []
        for p in ptrs:
            b = take(p, ends[ends.index(p) + 1] - p)
            if name == 'psg_envelopes': J[name].append(dict(address='%04X' % p, steps=env_parse(b))); continue
            items = mod_parse(b, name[-1], p)
            J[name].append(dict(address='%04X' % p, items=items) if items is not None else dict(address='%04X' % p, bytes=b.hex()))
    J['drums'] = []
    for i in range(DRUMS[1]):
        r = take(DRUMS[0] + 6 * i, 6)
        J['drums'].append(dict(voice_type=r[0], voices=r[1], instrument=r[2], byte3=r[3], pitch=w16(r, 4)))
    J['filler'] = {'%04X' % a: '%02X' % drv[a] for a in range(CODE_END, END) if a not in used}
    return J

def encode(J):
    """the tables' bytes, Z80 $107B-$1A76"""
    out = bytearray(END - CODE_END)
    def put(a, b): out[a - CODE_END:a - CODE_END + len(b)] = b
    put(FM_FREQ[0], b''.join(struct.pack('<H', v) for v in J['fm_frequencies']))
    put(PSG_FREQ[0], b''.join(struct.pack('<H', v) for v in J['psg_periods']))
    put(ATTEN[0], bytes(J['attenuation_fm'])); put(ATTEN[1], bytes(J['attenuation_psg']))
    for i, f in enumerate(J['fm_instruments']):
        ops = b''.join(bytes([f['operators'][o]['level'], f['operators'][o]['detune_multiple'], f['operators'][o]['envelope']]) for o in OPS)
        put(FM_INSTR[0] + FM_INSTR[2] * i, bytes([f['b0_high'] << 6 | f['feedback'] << 3 | f['algorithm'], f['level'], f['lfo']]) + ops
            + bytes([f['pitch_lo'], f['pitch_hi'], f['mod_a'], f['mod_b']]))
    for i, e in enumerate(J['envelopes']): put(ENVELOPES[0] + 4 * i, bytes(e))
    for i, p in enumerate(J['psg_instruments']): put(PSG_INSTR[0] + 6 * i, bytes(p[k] for k in ('b0', 'b1', 'b2', 'envelope', 'mod_a', 'mod_b')))
    for name, (tab, n), stop in (('psg_envelopes', PSG_ENV, MOD_A[0]), ('mod_a', MOD_A, MOD_B[0]), ('mod_b', MOD_B, DRUMS[0])):
        p = tab + 2 * n                              # the scripts follow their pointers, in order
        for i, s in enumerate(J[name]):
            if 'steps' in s: b = env_encode(s['steps'])
            elif 'items' in s: b = mod_encode(s['items'], name[-1])
            else: b = bytes.fromhex(s['bytes'])
            put(tab + 2 * i, struct.pack('<H', p)); put(p, b); p += len(b)
    for i, r in enumerate(J['drums']):
        put(DRUMS[0] + 6 * i, bytes([r['voice_type'], r['voices'], r['instrument'], r['byte3']]) + struct.pack('<H', r['pitch']))
    for a, v in J['filler'].items(): put(int(a, 16), bytes([int(v, 16)]))
    return bytes(out)

def main():
    d = open(sys.argv[1], 'rb').read()
    drv = d[0x98000:0x98000 + END]
    J = parse(drv)
    print('FM instruments %d, envelopes %d, PSG instruments %d, PSG envelopes %d, modulation scripts %d + %d, drums %d; filler bytes %d: %s'
          % (len(J['fm_instruments']), len(J['envelopes']), len(J['psg_instruments']), len(J['psg_envelopes']), len(J['mod_a']),
             len(J['mod_b']), len(J['drums']), len(J['filler']), ' '.join('%s=%s' % kv for kv in J['filler'].items())))
    print('encoded back:', 'the same' if encode(J) == drv[CODE_END:] else 'DIFFERENT')

if __name__ == '__main__':
    main()
