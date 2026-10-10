"""Emit vasm (motorola syntax) source for the whole ROM from the tracer's map.

Usage: mkasm.py ROM map.json out.asm [--raw raw.json] [--lines lines.json]

The code region $200-$1D000 becomes instructions with labels. Every instruction listed in
raw.json (or that this script cannot convert) is emitted as dc.w words with the mnemonic in a
comment. Everything outside the code region is pulled from the ROM with incbin. lines.json maps
each output line number to the ROM address and size it should assemble to, for checkasm.py.
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
names = {}
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

def lab(v):
    return names.get(v)

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
        if mn in ('move.l', 'movea.l') and lab(v): return '#' + lab(v)
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

emit('; Generated by tools/mkasm.py. Do not edit; regenerate from the ROM.')
emit('    org 0')
incbin(0, START)
pc = START
while pc < END:
    if pc in names: emit('%s:' % names[pc])
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
    while j < END and kind[j] != 1 and j not in names and j not in wtab and j not in ltab and j - pc < 16: j += 1
    assert j > pc
    emit('    dc.b ' + ','.join('$%02X' % b for b in d[pc:j]), pc, j - pc)
    pc = j
incbin(END, N)

open(A.out, 'w', newline='\n').write('\n'.join(out) + '\n')
if A.lines: json.dump({'lines': lines, 'fails': {'%06X' % k: v for k, v in fails.items()}}, open(A.lines, 'w'))
print('wrote %s: %d lines, %d labels, %d raw by request, %d unconvertible' % (A.out, len(out), len(names), len(raw), len(fails)))
