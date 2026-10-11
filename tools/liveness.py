"""What each routine's callers actually use: interprocedural register liveness over the traced code.

Usage: liveness.py work/map.json [ADDR ...]
       liveness.py work/map.json --usedef OUT

--usedef writes, for every instruction, what it reads and writes: a table of 0x10000 pairs of
little-endian uint32 (reads, writes), indexed by address / 2, bits 0-15 = d0-d7, a0-a7, bit 16 =
the NZVC flags, bit 17 = X. The harness uses it at run time to check that registers an ordinary
C rewrite leaves different are really dead (harness/hybrid.c, contracts).

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
        table = bytearray(0x10000 * 8)
        for a, (sz, mn, ops) in m['insn'].items():
            a = int(a, 16)
            if a < 0x20000: struct.pack_into('<II', table, (a >> 1) * 8, *inst_use_def(mn, ops))
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
