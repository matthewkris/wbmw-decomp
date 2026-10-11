"""What each routine's callers actually use: interprocedural register liveness over the traced code.

Usage: liveness.py work/map.json [ADDR ...]
       liveness.py work/map.json --usedef OUT

--usedef writes, for every instruction, what it reads and writes: a table of 0x10000 triples of
little-endian uint64 (reads, writes, flag-only reads), indexed by address / 2. The third is for
shifts by a constant: the parts of the register whose bits reach only the flags (the last bit
shifted out goes to C and X), so a difference there moves into the flags instead of being used.
Data registers come in three parts
(bits 3i, 3i+1, 3i+2 = bits 0-7, 8-15, 16-31 of di), so that move.b into d4 writes only its low
byte and reads nothing; bits 24-31 = a0-a7, bit 32 = the NZVC flags, bit 33 = X. The harness uses
it at run time to check that registers an ordinary C rewrite leaves different are really dead
(harness/hybrid.c, contracts).

For every routine (a call target in the map) it computes
  USES   the registers and flags it reads before writing them (its inputs), and
  LIVE   the registers and flags live when it returns: what some caller reads afterwards
         before overwriting it (its outputs, as far as anyone can tell).
A rewrite of a routine into ordinary C must produce LIVE (and keep the stack pointer); the rest
of what the 68000 leaves in registers is dead. Printed for the given routines, or all.

Conservative: a call kills nothing (a callee may leave any register as it was), an unknown or
indirect jump keeps everything live, and a byte or word write to a data register reads it too
(the rest of the register is kept). The flags are two units, NZVC and X (X is kept by moves,
compares and logic, and read by ADDX, SUBX, NEGX, ROXL and ROXR). Game-independent.
"""
import sys, json, re

REGS = ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(8)]
NZVC, XF = 1 << 16, 1 << 17
ALL = (1 << 18) - 1
CONDS = {'t', 'ra', 'f', 'hi', 'ls', 'cc', 'hs', 'cs', 'lo', 'ne', 'eq', 'vc', 'vs', 'pl', 'mi', 'ge', 'lt', 'gt', 'le'}

def bit(r):
    r = r.strip()
    if r == 'sp': r = 'a7'
    return 1 << REGS.index(r)

def split_ops(ops):
    out, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '(': depth += 1
        if ch == ')': depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def ea_regs(op):
    """registers an operand reads to form its address, or is (if a register)"""
    return sum(bit(r) for r in re.findall(r'\b([da][0-7]|sp)\b', op))

def is_reg(op): return re.fullmatch(r'([da][0-7]|sp)', op.strip()) is not None

def reglist(s):
    m = 0
    for part in s.replace(' ', '').split('/'):
        if '-' in part:
            x, y = part.split('-'); kx, nx = x[0], int(x[1]); ny = int(y[1])
            for i in range(nx, ny + 1): m |= bit('%s%d' % (kx, i))
        else: m |= bit(part)
    return m

def use_def(mn, ops):
    """(use, def) bit masks of one instruction (without control flow and calls)"""
    base, _, suf = mn.partition('.')
    p = split_ops(ops) if ops else []
    use = defs = 0
    full = suf in ('l', '') or base in ('movea', 'moveq', 'lea', 'exg')
    if base in ('nop', 'bra', 'rts', 'rte'): return 0, 0
    if base == 'movem':
        if re.fullmatch(r'[da][0-7]([-/][da][0-7])*', p[0].replace(' ', '')):
            use |= reglist(p[0]) | ea_regs(p[1])
            if p[1].startswith('-('): defs |= ea_regs(p[1])
        else:
            use |= ea_regs(p[0]); defs |= reglist(p[1])
            if p[0].endswith(')+'): defs |= ea_regs(p[0])
        return use, defs
    if base in ('link',): return bit(p[0]) | bit('a7'), bit(p[0]) | bit('a7')
    if base in ('unlk',): return bit(p[0]), bit(p[0]) | bit('a7')
    if base == 'pea': return ea_regs(p[0]) | bit('a7'), bit('a7')
    if base == 'exg': r = bit(p[0]) | bit(p[1]); return r, r
    if base == 'swap': r = bit(p[0]); return r, r | NZVC
    if base == 'ext': r = bit(p[0]); return r, r | NZVC
    if base in ('tst', 'cmp', 'cmpa', 'cmpi', 'cmpm', 'btst'):
        for o in p: use |= ea_regs(o)
        return use, NZVC
    if base[0] == 's' and base[1:] in CONDS:          # Scc
        use = NZVC | (ea_regs(p[0]) if not is_reg(p[0]) else bit(p[0]))
        return use, (bit(p[0]) if is_reg(p[0]) else 0)
    if base in ('lea',): return ea_regs(p[0]), bit(p[1])
    if base in ('clr', 'neg', 'negx', 'not', 'tas'):
        o = p[0]
        if is_reg(o): use |= 0 if (base == 'clr' and suf == 'l') else bit(o); defs |= bit(o)
        else: use |= ea_regs(o)
        if base == 'negx': use |= XF
        return use, defs | NZVC | (XF if base in ('neg', 'negx') else 0)
    if len(p) == 2:
        src, dst = p
        use |= 0 if src.startswith('#') else ea_regs(src)
        if src.endswith(')+') or src.startswith('-('): defs |= ea_regs(src)
        if is_reg(dst):
            reads_dst = base not in ('move', 'movea', 'moveq') or not full
            if base in ('move',) and not full: reads_dst = True
            if base in ('movea', 'moveq'): reads_dst = False
            if reads_dst: use |= bit(dst)
            defs |= bit(dst)
        else:
            use |= ea_regs(dst)
            if dst.endswith(')+') or dst.startswith('-('): defs |= ea_regs(dst)
        flags = 0
        if base in ('move', 'moveq', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'bset', 'bclr', 'bchg', 'mulu', 'muls', 'divu', 'divs'):
            flags = NZVC
        elif base in ('add', 'addi', 'addq', 'sub', 'subi', 'subq', 'addx', 'subx', 'asl', 'asr', 'lsl', 'lsr', 'roxl', 'roxr'):
            flags = 0 if dst.startswith('a') and is_reg(dst) else NZVC | XF
        elif base in ('rol', 'ror'): flags = NZVC
        if dst in ('ccr', 'sr'): flags = NZVC | XF
        if base in ('addx', 'subx', 'roxl', 'roxr'): use |= XF
        return use, defs | flags
    if len(p) == 1 and base in ('asl', 'asr', 'lsl', 'lsr', 'roxl', 'roxr', 'rol', 'ror'):
        use |= ea_regs(p[0]) | (XF if base in ('roxl', 'roxr') else 0)
        return use, NZVC | (XF if base not in ('rol', 'ror') else 0)
    return ALL, ALL                                    # unknown: assume it reads and writes everything

# ---- the run-time table (--usedef): data registers in three parts, so that a byte or word write
# does not count as reading the rest. Bits 3i, 3i+1, 3i+2: bits 0-7, 8-15 and 16-31 of di; 24+i:
# ai (whole); 32 NZVC; 33 X.
L_NZVC, L_X = 1 << 32, 1 << 33
L_ALL = (1 << 34) - 1
PARTS = {'b': 1, 'w': 3, 'l': 7}

def lanes(r, size='l'):
    r = r.strip()
    if r == 'sp': r = 'a7'
    i = int(r[1])
    return PARTS[size] << (3 * i) if r[0] == 'd' else 1 << (24 + i)

def ea_lanes(op):
    """registers an addressing mode reads: address registers whole, index registers as sized"""
    m = 0
    for r, sz in re.findall(r'(?<![$\w])([da][0-7]|sp)(?:\.([wl]))?\b', op):   # not inside a hex number
        m |= lanes(r, sz or 'w') if r[0] == 'd' else lanes(r)
    if op.strip() in ('sr', 'ccr'): m |= L_NZVC | L_X
    return m

def bit_lane(n, r):
    """the part of data register r holding bit n"""
    n %= 32
    return (1 if n < 8 else 2 if n < 16 else 4) << (3 * int(r[1]))

def bit_range_lanes(r, lo, hi):
    """the parts of data register r holding any of bits lo..hi"""
    m, i = 0, int(r[1])
    for part, (a, b) in enumerate(((0, 7), (8, 15), (16, 31))):
        if lo <= b and hi >= a: m |= 1 << (3 * i + part)
    return m

def shift_lanes(base, size, n, r):
    """a shift of data register r by an immediate n: (parts the result comes from, parts that
    reach only the flags: the last bit shifted out, into C and X, and for asl the bits V sees)"""
    s = {'b': 8, 'w': 16, 'l': 32}[size]
    if base in ('lsl', 'asl'):
        value = bit_range_lanes(r, 0, s - n - 1) if n < s else 0
        flags = bit_range_lanes(r, s - n, s - n)
        if base == 'asl': flags |= bit_range_lanes(r, max(s - n - 1, 0), s - 1)
    else:                                              # lsr, asr
        value = bit_range_lanes(r, n, s - 1) if n < s else 0
        if base == 'asr': value |= bit_range_lanes(r, s - 1, s - 1)
        flags = bit_range_lanes(r, n - 1, n - 1)
    return value, flags & ~value

def lane_use_def3(mn, ops):
    """(reads, writes, flag-only reads) of one instruction in the run-time table's bits"""
    base, _, suf = mn.partition('.')
    p = split_ops(ops) if ops else []
    if base in ('lsl', 'lsr', 'asl', 'asr') and len(p) == 2 and p[0].startswith('#') \
            and re.fullmatch(r'd[0-7]', p[1].strip()):
        n = int(p[0][1:].lstrip('$') or '0', 16 if '$' in p[0] else 10) or 8
        value, flags = shift_lanes(base, suf if suf in PARTS else 'l', n, p[1].strip())
        use, defs = lane_use_def(mn, ops)
        own = lanes(p[1].strip())
        return (use & ~own) | value, defs, flags
    use, defs = lane_use_def(mn, ops)
    return use, defs, 0

def lane_use_def(mn, ops):
    """(reads, writes) of one instruction in the run-time table's bits"""
    base, _, suf = mn.partition('.')
    size = suf if suf in PARTS else 'l'
    p = split_ops(ops) if ops else []
    a7 = lanes('a7')
    if base in ('nop', 'bra'): return 0, 0
    if base == 'rts': return a7, a7
    if base == 'rte': return a7, a7 | L_NZVC | L_X
    if base in ('jsr', 'bsr', 'pea'): return ea_lanes(ops) | a7, a7
    if base == 'jmp': return ea_lanes(ops), 0
    if base[0] == 'b' and base[1:] in CONDS and base not in ('bset', 'bclr', 'bchg', 'btst'): return L_NZVC, 0
    if base.startswith('db'):
        r = lanes(p[0], 'w')
        return r | (L_NZVC if base not in ('dbra', 'dbf') else 0), r
    def is_d(o): return re.fullmatch(r'd[0-7]', o.strip()) is not None
    def is_a(o): return re.fullmatch(r'(a[0-7]|sp)', o.strip()) is not None
    def src_reads(o, sz):
        if o.startswith('#'): return 0
        if is_d(o): return lanes(o, sz)
        if is_a(o): return lanes(o)
        return ea_lanes(o)
    def autoinc(o): return ea_lanes(o) if (o.endswith(')+') or o.startswith('-(')) else 0
    if base == 'movem':
        if re.fullmatch(r'[da][0-7]([-/][da][0-7])*', p[0].replace(' ', '')):
            regs = 0
            for i in range(16):
                if reglist(p[0]) >> i & 1: regs |= lanes(REGS[i], size)
            return regs | ea_lanes(p[1]), autoinc(p[1])
        regs = 0
        for i in range(16):
            if reglist(p[1]) >> i & 1: regs |= lanes(REGS[i])
        return ea_lanes(p[0]), regs | autoinc(p[0])
    if base == 'link': r = lanes(p[0]) | a7; return r, r
    if base == 'unlk': return lanes(p[0]), lanes(p[0]) | a7
    if base == 'exg': r = lanes(p[0]) | lanes(p[1]); return r, r
    if base == 'swap': r = lanes(p[0]); return r, r | L_NZVC
    if base == 'ext':
        return (lanes(p[0], 'b') if size == 'w' else lanes(p[0], 'w')), lanes(p[0], size) | L_NZVC
    if base == 'lea': return ea_lanes(p[0]), lanes(p[1])
    if base in ('tst', 'cmp', 'cmpi', 'cmpm', 'cmpa'):
        use = 0
        for i, o in enumerate(p):
            use |= lanes(o) if base == 'cmpa' and i == 1 else src_reads(o, size)
        return use, L_NZVC
    if base == 'btst' or base in ('bset', 'bclr', 'bchg'):
        src, dst = p
        use = 0 if src.startswith('#') else lanes(src, 'b')
        if is_d(dst):
            part = bit_lane(int(src[1:].lstrip('$') or '0', 16 if '$' in src else 10), dst) if src.startswith('#') else lanes(dst)
            use |= part
            return use, L_NZVC | (part if base != 'btst' else 0)
        return use | ea_lanes(dst), L_NZVC | autoinc(dst)
    if base[0] == 's' and base[1:] in CONDS:          # Scc
        o = p[0]
        if is_d(o): return L_NZVC, lanes(o, 'b')
        return L_NZVC | ea_lanes(o), autoinc(o)
    if base in ('clr', 'neg', 'negx', 'not', 'tas'):
        o = p[0]
        if is_d(o): r = lanes(o, size if base != 'tas' else 'b'); use, defs = (0 if base == 'clr' else r), r
        else: use, defs = ea_lanes(o), autoinc(o)
        if base == 'negx': use |= L_X
        return use, defs | L_NZVC | (L_X if base in ('neg', 'negx') else 0)
    if len(p) == 2:
        src, dst = p
        shift = base in ('asl', 'asr', 'lsl', 'lsr', 'roxl', 'roxr', 'rol', 'ror')
        use = (lanes(src, 'b') if shift and is_d(src) else src_reads(src, 'w' if base in ('mulu', 'muls', 'divu', 'divs') else size))
        defs = autoinc(src)
        if is_d(dst):
            if base == 'moveq': defs |= lanes(dst)
            elif base in ('mulu', 'muls'): use |= lanes(dst, 'w'); defs |= lanes(dst)
            elif base in ('divu', 'divs'): use |= lanes(dst); defs |= lanes(dst)
            elif base == 'move': defs |= lanes(dst, size)
            else: use |= lanes(dst, size); defs |= lanes(dst, size)
        elif is_a(dst):
            if base not in ('movea',): use |= lanes(dst)
            defs |= lanes(dst)
        elif dst in ('ccr', 'sr'):
            if base in ('andi', 'ori', 'eori', 'and', 'or', 'eor'): use |= L_NZVC | L_X
        else:
            use |= ea_lanes(dst); defs |= autoinc(dst)
        flags = 0
        if base in ('move', 'moveq', 'and', 'andi', 'or', 'ori', 'eor', 'eori', 'mulu', 'muls', 'divu', 'divs'):
            flags = L_NZVC
        elif base in ('add', 'addi', 'addq', 'sub', 'subi', 'subq', 'addx', 'subx', 'asl', 'asr', 'lsl', 'lsr', 'roxl', 'roxr'):
            flags = 0 if is_a(dst) else L_NZVC | L_X
        elif base in ('rol', 'ror'): flags = L_NZVC
        if dst in ('ccr', 'sr'): flags = L_NZVC | L_X
        if base in ('addx', 'subx', 'roxl', 'roxr'): use |= L_X
        if base in ('movea', 'adda', 'suba') or (base in ('addq', 'subq') and is_a(dst)): flags = 0
        return use, defs | flags
    if len(p) == 1 and base in ('asl', 'asr', 'lsl', 'lsr', 'roxl', 'roxr', 'rol', 'ror'):
        return ea_lanes(p[0]) | (L_X if base in ('roxl', 'roxr') else 0), autoinc(p[0]) | L_NZVC | (L_X if base not in ('rol', 'ror') else 0)
    return L_ALL, L_ALL                                # unknown: assume it reads and writes everything

def inst_use_def(mn, ops):
    """reads and writes of one instruction, control flow included"""
    base = mn.split('.')[0]
    if base == 'rts': return bit('a7'), bit('a7')
    if base == 'rte': return bit('a7'), bit('a7') | NZVC | XF       # restores the flags, not the registers
    if base in ('jsr', 'bsr', 'pea'): return ea_regs(ops) | bit('a7'), bit('a7')
    if base == 'jmp': return ea_regs(ops), 0
    if base == 'bra': return 0, 0
    if base[0] == 'b' and base[1:] in CONDS and base not in ('bset', 'bclr', 'bchg', 'btst'): return NZVC, 0
    if base.startswith('db'):
        r = bit(split_ops(ops)[0])
        return r | (NZVC if base not in ('dbra', 'dbf') else 0), r
    return use_def(mn, ops)

def main():
    m = json.load(open(sys.argv[1]))
    if len(sys.argv) > 3 and sys.argv[2] == '--usedef':
        import struct
        table = bytearray(0x10000 * 24)
        for a, (sz, mn, ops) in m['insn'].items():
            a = int(a, 16)
            if a < 0x20000: struct.pack_into('<QQQ', table, (a >> 1) * 24, *lane_use_def3(mn, ops))
        open(sys.argv[3], 'wb').write(table)
        return
    ins = {int(a, 16): v for a, v in m['insn'].items()}
    calls = {int(a, 16) for a in m['calls']}
    order = sorted(ins)
    nxt = {a: a + ins[a][0] for a in order}
    # indirect jumps with known targets: the tracer's tables
    site_targets = {}
    for t, (fmt, tg, site) in m['auto_tables'].items():
        site_targets.setdefault(site, set()).update(x for x in tg if x is not None)
    d = open(sys.argv[1].replace('map.json', '../rom/' + 'x'), 'rb').read() if False else None
    for t, (n, site) in m['bra_tables'].items():
        q = int(t, 16)
        for _ in range(n):
            site_targets.setdefault(site, set()).add(q)
            q += ins[q][0] if q in ins and ins[q][1].startswith('bra') else 2

    def target(ops):
        t = re.fullmatch(r'\$([0-9a-f]+)(\.[wl])?', ops.strip())
        return int(t.group(1), 16) if t else None

    # routine bodies: from each call target, by branches and fall-through, not into calls
    def body(e):
        seen, work = set(), [e]
        while work:
            a = work.pop()
            while a in ins and a not in seen:
                seen.add(a); sz, mn, ops = ins[a]; base = mn.split('.')[0]
                if base in ('rts', 'rte'): break
                if base == 'jmp':
                    t = target(ops)
                    if t is not None and t not in calls: work.append(t)
                    for x in site_targets.get(a, ()):
                        if x not in calls: work.append(x)
                    break
                if base == 'bra': work.append(int(ops[1:], 16)); break
                if base[0] == 'b' and base[1:] in CONDS and base not in ('bset', 'bclr', 'bchg', 'btst') or base.startswith('db'):
                    work.append(int(re.search(r'\$([0-9a-f]+)$', ops).group(1), 16))
                a = nxt[a]
        return seen
    routines = {e: body(e) for e in calls if e in ins}
    uses = {e: 0 for e in routines}                   # inputs
    live_out = {e: 0 for e in routines}               # needed after return

    def analyse(e, out):
        """backward liveness over e's body; returns live at entry, and live after each call site"""
        b = routines[e]
        live = {a: 0 for a in b}
        after_call = {}
        changed = True
        while changed:
            changed = False
            for a in sorted(b, reverse=True):
                sz, mn, ops = ins[a]; base = mn.split('.')[0]
                succ_live = 0
                n = nxt[a]
                def L(x): return live.get(x, ALL if x not in b else live[x])
                if base in ('rts', 'rte'): new = out | bit('a7')
                elif base == 'jmp':
                    t = target(ops)
                    if t is None and a in site_targets:        # a table of local targets, or of routines
                        new = ea_regs(ops) | bit('a7')
                        for x in site_targets[a]:
                            new |= L(x) if x not in calls else (uses.get(x, ALL) | out)
                    elif t is None: new = ALL
                    elif t in calls: new = uses.get(t, ALL) | out | bit('a7')
                    else: new = L(t)
                elif base == 'bra': new = L(int(ops[1:], 16))
                elif base in ('jsr', 'bsr'):
                    t = target(ops) if base == 'jsr' else int(ops[1:], 16)
                    after = L(n)
                    ts = [t] if t is not None else sorted(site_targets.get(a, ()))
                    for x in ts: after_call.setdefault(x, 0); after_call[x] |= after
                    callee = ALL if not ts else 0
                    for x in ts: callee |= uses.get(x, ALL)
                    new = callee | after | bit('a7') | ea_regs(ops)
                else:
                    u, d = use_def(mn, ops)
                    cont = L(n)
                    if base[0] == 'b' and base[1:] in CONDS and base not in ('bset', 'bclr', 'bchg', 'btst'):
                        cont |= L(int(ops[1:], 16)); u |= NZVC; d = 0
                    elif base.startswith('db'):
                        cont |= L(int(re.search(r'\$([0-9a-f]+)$', ops).group(1), 16))
                        r = bit(split_ops(ops)[0]); u |= r | (NZVC if base not in ('dbra', 'dbf') else 0); d = 0
                    new = u | (cont & ~d)
                if new != live[a]: live[a] = new; changed = True
        return live[e], after_call

    for _ in range(30):                               # interprocedural fixpoint
        changed = False
        new_out = {e: 0 for e in routines}
        for e in routines:
            entry_live, after = analyse(e, live_out[e])
            if entry_live != uses[e] and _ < 29: uses[e] = entry_live; changed = True
            for t, l in after.items():
                if t in new_out: new_out[t] |= l
        for e in routines:
            if new_out[e] | live_out[e] != live_out[e]: live_out[e] |= new_out[e]; changed = True
        if not changed: break

    def names(mask):
        r = [REGS[i] for i in range(16) if mask >> i & 1]
        if mask & NZVC: r.append('NZVC')
        if mask & XF: r.append('X')
        return ' '.join(r)
    want = [int(a, 16) for a in sys.argv[2:]] or sorted(routines)
    for e in want:
        if e not in routines: print('%06X: not a routine in the map' % e); continue
        print('%06X  uses: %-40s live after: %s' % (e, names(uses[e]), names(live_out[e])))

if __name__ == '__main__':
    main()
