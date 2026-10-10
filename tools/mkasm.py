"""Emit vasm (motorola syntax) source for the whole ROM from the tracer's map.

Usage: mkasm.py ROM map.json out.asm [--raw raw.json] [--lines lines.json]

The vectors and header become dc.l labels and named fields. The code region $200-$1D000 becomes
instructions with labels. Every instruction listed in raw.json (or that this script cannot
convert) is emitted as dc.w words with the mnemonic in a comment. Everything else is described
by analysis/layout.json: pointer tables become dc.l labels, fills become dcb.b, and blobs are
pulled from the ROM with incbin under their own label. Immediates and absolutes that are ROM
addresses become labels (rules in the addr_imm pass; data-pointer fields come from the data_ptr
hint). lines.json maps each output line number to the ROM address and size it should assemble
to, for checkasm.py.
"""
import sys, json, re, struct, argparse

ap = argparse.ArgumentParser()
ap.add_argument('rom'); ap.add_argument('map'); ap.add_argument('out')
ap.add_argument('--raw'); ap.add_argument('--lines')
A = ap.parse_args()
d = open(A.rom, 'rb').read(); m = json.load(open(A.map)); N = len(d)
kind = bytes.fromhex(m['kind']); ins = {int(a, 16): v for a, v in m['insn'].items()}
raw = set(int(a, 16) for a in json.load(open(A.raw))) if A.raw else set()
START, END = 0x200, 0x1D000
be16 = lambda o: struct.unpack_from('>H', d, o)[0]
be32 = lambda o: struct.unpack_from('>I', d, o)[0]
H = lambda a: int(a, 16)

# ---- labels: one name per address, by priority ----
LAY = json.load(open('analysis/layout.json'))
names = {H(a): n for a, n in LAY['vector_names'].items()}
def name(a, prefix):
    if START <= a < END and a not in names and kind[a] != 2: names[a] = '%s_%06X' % (prefix, a)
for a in m['calls']: name(H(a), 'sub')
for a in m['ptr_entries']: name(H(a), 'ptr')
for a in m['orphans']: name(a, 'orphan')
for a, t in m['descriptors'].items(): name(H(a), 'task_' + t.replace('_', 'x'))
for a in m['auto_tables']: name(H(a), 'tbl')
for a in m['tables']: name(H(a), 'tbl')
for a in m['bra_tables']: name(H(a), 'tbl')
for a in m['jumps']: name(H(a), 'loc')

# word tables: addr -> (base, [targets]); long tables: addr -> [targets]
wtab, ltab = {}, {}
for a, (fmt, tg, site) in m['auto_tables'].items():
    (wtab.__setitem__(H(a), (H(a), tg)) if fmt == 'w' else ltab.__setitem__(H(a), tg))
for t in json.load(open('analysis/hints.json')).get('tables', []):
    a = H(t['addr']); tg = m['tables']['%06X' % a]
    if t['fmt'] == 'l_abs': ltab[a] = tg
    else: wtab[a] = (H(t.get('base', t['addr'])), tg)
for a, (base, tg) in wtab.items(): name(base, 'tbl')

# pc-relative operands need a label at their target, even inside data
PCREL = re.compile(r'\$([0-9a-f]+)\(pc')
for a, (sz, mn, ops) in ins.items():
    for mm in PCREL.finditer(ops): name(int(mm.group(1), 16), 'dat')

# ---- immediates and absolutes that are ROM addresses (addr_imm: instruction -> target) ----
HINTS = json.load(open('analysis/hints.json'))
anyreg = lambda s: re.sub(r'\(a[0-7]\)', '(an)', s.replace(' ', ''))
DATA_PTR = set(anyreg(x) for x in HINTS.get('data_ptr', []))   # fields that always hold a data address
addr_imm = {}
seq = sorted(ins)
for i, a in enumerate(seq):
    sz, mn, ops = ins[a]
    parts = [p.replace(' ', '') for p in re.split(r',\s*(?![^()]*\))', ops)]
    mi = re.fullmatch(r'#\$([0-9a-f]+)', parts[0]) if parts else None
    v = int(mi.group(1), 16) if mi else None
    if mn in ('lea.l', 'pea.l'):                         # lea/pea of an absolute ROM address
        ml = re.fullmatch(r'\$([0-9a-f]+)\.l', parts[0])
        if ml and START <= int(ml.group(1), 16) < N: name(int(ml.group(1), 16), 'dat')
    elif v is None or not (START <= v < N): continue
    elif mn == 'move.l' and anyreg(parts[1]) in DATA_PTR:  # pointer into a known data field
        addr_imm[a] = v
    elif mn in ('move.l', 'movea.l') and kind[v] == 1:      # pointer to code (exit handlers etc.)
        addr_imm[a] = v
    elif mn == 'movea.l':                                     # loading an address register
        addr_imm[a] = v
    elif mn == 'addi.l' and re.fullmatch(r'd[0-7]', parts[1]) and i + 1 < len(seq):
        n_mn, n_ops = ins[seq[i + 1]][1:]                     # table base: next insn uses dN as an address
        n_parts = [p.replace(' ', '') for p in n_ops.split(', ')]
        if n_parts[0] == parts[1] and (n_mn == 'movea.l' or anyreg(n_parts[-1]) in DATA_PTR):
            addr_imm[a] = v
for a, v in addr_imm.items(): name(v, 'ptr' if kind[v] == 1 else 'dat')

# ---- data pieces from analysis/layout.json; tables add a blob piece at each target ----
pieces = {}                   # addr -> dict(type, name, ...)
for p in LAY['pieces']: pieces[H(p['addr'])] = dict(p)
for a, p in list(pieces.items()):
    if p['type'] == 'ptr32':
        for i in range(p['count']):
            t = be32(a + 4 * i) & 0xFFFFFF
            if t not in pieces: pieces[t] = {'type': 'blob', 'name': p['slot_names'][i]}
    if p['type'] == 'gfxtab':
        for i in range(p['count']):
            v = be32(a + 4 * i)
            if v != 0xFFFFFFFF and (v & 0xFFFFFF) not in pieces:
                pieces[v & 0xFFFFFF] = {'type': 'blob', 'name': 'gfx_%06X' % (v & 0xFFFFFF)}
starts = sorted(pieces) + [N]
for i, a in enumerate(starts[:-1]):
    p = pieces[a]
    p['end'] = a + 4 * p['count'] if p['type'] in ('ptr32', 'gfxtab') else starts[i + 1]
    assert p['end'] <= starts[i + 1], 'piece %06X overlaps the next' % a
    if p['type'] == 'fill': assert len(set(d[a:p['end']])) == 1, 'fill %06X is not one byte' % a
    names[a] = p['name']
names[N] = 'RomEnd'
inside = lambda v: next(((a, p) for a, p in pieces.items() if a < v < p['end']), None)
for v in [v for v in names if inside(v)]: del names[v]    # no line starts there; use piece+offset

def lab(v):
    if v in names: return names[v]
    ap = inside(v)
    return '%s+$%X' % (ap[1]['name'], v - ap[0]) if ap else None

def datalab(v):
    """label for an immediate that is a data address: only pieces and table slots, never code"""
    return lab(v) if v >= END or any(a <= v < p['end'] for a, p in pieces.items() if p['type'] != 'fill') else None

def split_ops(ops):
    out, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '(' : depth += 1
        if ch == ')' : depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

BRANCH = re.compile(r'^(b(ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le))\.([bw])$')
class Fail(Exception): pass

def conv_op(o, pc, mn, last):
    o = o.replace(' ', '')
    if mn.startswith('db') and last or BRANCH.match(mn):
        mm = re.fullmatch(r'\$([0-9a-f]+)', o)
        if not mm or lab(int(mm.group(1), 16)) is None: raise Fail('branch target')
        return lab(int(mm.group(1), 16))
    mm = re.fullmatch(r'#\$([0-9a-f]+)', o)
    if mm:
        v = int(mm.group(1), 16)
        if mn == 'moveq': return '#%d' % (v - 256 if v & 0x80 else v)
        if (pc in addr_imm and lab(v)) or (mn.endswith('.l') and datalab(v)): return '#' + lab(v)
        return '#$%x' % v
    mm = re.fullmatch(r'\$([0-9a-f]+)\.w', o)
    if mm:
        v = int(mm.group(1), 16)
        if v < 0x8000:
            return '(%s).w' % lab(v) if mn in ('jsr', 'jmp', 'lea.l', 'pea.l') and lab(v) else '($%x).w' % v
        return '($%08x).w' % (v | 0xFFFF0000)
    mm = re.fullmatch(r'\$([0-9a-f]+)\.l', o)
    if mm:
        v = int(mm.group(1), 16)
        return '(%s).l' % lab(v) if lab(v) else '($%x).l' % v
    mm = re.fullmatch(r'\$([0-9a-f]+)\(pc(,[ad]\d\.[wl])?\)', o)
    if mm:
        v = int(mm.group(1), 16)
        if lab(v) is None: raise Fail('pc target')
        return '%s(pc%s)' % (lab(v), mm.group(2) or '')
    if re.fullmatch(r'-?\$?[0-9a-f]*\((a[0-7]|sp)(,[ad]\d\.[wl])?\)|\((a[0-7]|sp)\)\+|-\((a[0-7]|sp)\)', o): return o
    if re.fullmatch(r'[ad][0-7]|sp|sr|ccr|usp', o): return o
    if re.fullmatch(r'[ad][0-7](-[ad][0-7])?(/[ad][0-7](-[ad][0-7])?)*', o): return o
    raise Fail('operand ' + o)

def conv(pc, sz, mn, ops):
    if mn == 'dc.w': raise Fail('capstone dc.w')
    mn2 = mn
    mb = BRANCH.match(mn)
    if mb: mn2 = mb.group(1) + ('.s' if mb.group(3) == 'b' else '.w')
    if mn in ('jsr', 'jmp'): mn2 = mn
    if mn == 'move':
        if 'usp' not in ops: raise Fail('unsized move')
        mn2 = 'move.l'                                  # capstone leaves move to/from usp unsized
    parts = split_ops(ops)
    if mn[:4] in ('btst', 'bset', 'bclr', 'bchg'):    # size follows the destination, not the opcode
        mn2 = mn[:4] + ('.l' if re.fullmatch(r'd[0-7]', parts[-1].strip()) else '.b')
    return (mn2 + ' ' + ','.join(conv_op(o, pc, mn, i == len(parts) - 1) for i, o in enumerate(parts))).strip()

out = []; lines = {}; fails = {}
def emit(s, addr=None, size=None, insn=False):
    out.append(s)
    if addr is not None: lines[len(out)] = (addr, size, insn)
def rawwords(pc, sz, why):
    return 'dc.w ' + ','.join('$%04X' % be16(pc + i) for i in range(0, sz, 2))

def incbin(a, b):
    emit('    incbin "%s",$%X,$%X' % (A.rom.replace('\\', '/'), a, b - a), a, b - a)

def text(a, n):
    s = d[a:a + n].decode('latin1')
    if all(32 <= ord(c) < 127 for c in s) and "'" not in s: return "dc.b '%s'" % s
    return 'dc.b ' + ','.join('$%02X' % b for b in d[a:a + n])

def emit_piece(a, p):
    e = p['end']
    if p['type'] == 'fill':
        emit('    dcb.b $%X,$%02X' % (e - a, d[a]), a, e - a)
    elif p['type'] == 'blob':
        incbin(a, e)
    elif p['type'] == 'ptr32':
        for i in range(p['count']):
            emit('    dc.l %s' % lab(be32(a + 4 * i) & 0xFFFFFF), a + 4 * i, 4)
    elif p['type'] == 'gfxtab':
        for i in range(p['count']):
            v = be32(a + 4 * i)
            if v == 0xFFFFFFFF: emit('    dc.l $FFFFFFFF', a + 4 * i, 4)
            else: emit('    dc.l %s+$%02X000000' % (lab(v & 0xFFFFFF), v >> 24), a + 4 * i, 4)
    return e

HEADER = [('console', 16), ('copyright', 16), ('title_domestic', 48), ('title_overseas', 48),
          ('serial', 14), ('checksum', 2), ('io_support', 16), ('rom_start', 4), ('rom_end', 4),
          ('ram_start', 4), ('ram_end', 4), ('sram_id', 2), ('sram_type', 2), ('sram_start', 4),
          ('sram_end', 4), ('modem', 12), ('memo', 40), ('region', 16)]

emit('; Generated by tools/mkasm.py. Do not edit; regenerate from the ROM.')
emit('    org 0')
emit('Vectors:')
for i in range(64):
    v = be32(4 * i)
    emit('    dc.l %s' % (names[v] if v in names and kind[v] == 1 else '$%08X' % v), 4 * i, 4)
emit('Header:')
a = 0x100
for f, n in HEADER:
    if f == 'rom_end': s = 'dc.l RomEnd-1'
    elif f == 'checksum': s = 'dc.w $%04X' % be16(a)
    elif n == 4: s = 'dc.l $%08X' % be32(a)
    elif f == 'sram_type': s = 'dc.w $%04X' % be16(a)
    else: s = text(a, n)
    emit('    %-44s ; %s' % (s, f), a, n); a += n
assert a == START
pc = START
while pc < N:
    if pc in names: emit('%s:' % names[pc])
    if pc in pieces:
        pc = emit_piece(pc, pieces[pc]); continue
    assert pc < END, 'no layout piece covers %06X' % pc
    if kind[pc] == 1:
        sz, mn, ops = ins[pc]
        cmt = '%s %s' % (mn, ops)
        if pc in raw:
            emit('    %-44s ; %06X %s' % (rawwords(pc, sz, ''), pc, cmt), pc, sz, True)
        else:
            try:
                emit('    %-44s ; %06X' % (conv(pc, sz, mn, ops), pc), pc, sz, True)
            except Fail as e:
                fails[pc] = str(e)
                emit('    %-44s ; %06X %s' % (rawwords(pc, sz, ''), pc, cmt), pc, sz, True)
        pc += sz
        continue
    if wtab.get(pc, (0, []))[1]:
        base, tg = wtab[pc]
        for i, t in enumerate(tg):
            emit('    dc.w %s-%s' % (names[t], names[base]), pc + 2 * i, 2)
        pc += 2 * len(tg); continue
    if ltab.get(pc):
        for i, t in enumerate(ltab[pc]):
            emit('    dc.l %s' % (names[t] if t else '0'), pc + 4 * i, 4)
        pc += 4 * len(ltab[pc]); continue
    if '%06X' % pc in m['descriptors']:
        emit("    dc.b '%s'" % d[pc:pc + 4].decode('latin1'), pc, 4)
        pc += 4; continue
    j = pc + 1
    while j < END and kind[j] != 1 and j not in names and j not in pieces and j not in wtab and j not in ltab and j - pc < 16: j += 1
    assert j > pc
    emit('    dc.b ' + ','.join('$%02X' % b for b in d[pc:j]), pc, j - pc)
    pc = j
emit('RomEnd:')

open(A.out, 'w', newline='\n').write('\n'.join(out) + '\n')
if A.lines: json.dump({'lines': lines, 'fails': {'%06X' % k: v for k, v in fails.items()}}, open(A.lines, 'w'))
print('wrote %s: %d lines, %d labels, %d raw by request, %d unconvertible' % (A.out, len(out), len(names), len(raw), len(fails)))
