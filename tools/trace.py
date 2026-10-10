"""Recursive-descent 68000 code tracer for a Genesis ROM.
Usage: trace.py ROM [--extra file] [--out map.json]
"""
import sys, struct, json, argparse, collections
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000

ap = argparse.ArgumentParser()
ap.add_argument('rom'); ap.add_argument('--extra'); ap.add_argument('--out'); ap.add_argument('-v', action='store_true'); ap.add_argument('--bounds'); ap.add_argument('--orphans', action='store_true')
A = ap.parse_args()
d = open(A.rom,'rb').read(); N = len(d)
md = Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
be16 = lambda o: struct.unpack_from('>H', d, o)[0]
be32 = lambda o: struct.unpack_from('>I', d, o)[0]
s8  = lambda v: v-256 if v&0x80 else v
s16 = lambda v: v-65536 if v&0x8000 else v

kind = bytearray(N)          # 0 unknown, 1 insn start, 2 insn body, 3 data(table)
insn = {}                    # addr -> (size, mnem, ops)
calls = collections.defaultdict(set)    # target -> callers
jumps = collections.defaultdict(set)    # target -> branch sources
indirect = []                # (addr, text)
refs = collections.defaultdict(set)     # rom addr -> {(pc, mnem, how)}
import re
RX = [(re.compile(r'#\$([0-9a-f]+)'),'imm'),(re.compile(r'\$([0-9a-f]+)\(pc'),'pc'),(re.compile(r'\$([0-9a-f]+)\.l'),'abs')]
bad = []                     # (addr, reason, came_from)
tables = {}                  # table addr -> list of targets
work = []

def push(t, src, why):
    if t >= N or t & 1 or t < 0x200:
        bad.append((t, 'target out of range/odd ('+why+')', src)); return
    work.append((t, src))

def decode(pc):
    it = next(md.disasm(d[pc:pc+12], pc, 1), None)
    if it is None: return None
    if '[' in it.op_str or '*' in it.op_str: return None
    if it.mnemonic == 'dc.w': return None        # capstone's spelling of an undecodable word
    return it

def ea_ext_words(mode, reg, size_imm):
    if mode in (5,6): return 1
    if mode == 7:
        return {0:1,1:2,2:1,3:1,4:size_imm}.get(reg,0)
    return 0

def trace(start, src):
    pc = start
    hist = []
    while True:
        if pc >= N: bad.append((pc,'ran off end',src)); return
        if kind[pc] == 1: return
        if kind[pc] == 2: bad.append((pc,'lands mid-instruction',src)); return
        if kind[pc] == 3: bad.append((pc,'runs into table data',src)); return
        it = decode(pc)
        if it is None: bad.append((pc,'invalid opcode %04X'%be16(pc),src)); return
        sz = it.size
        if any(kind[pc+i] for i in range(sz)): bad.append((pc,'overlaps existing',src)); return
        kind[pc] = 1
        for i in range(1,sz): kind[pc+i] = 2
        insn[pc] = (sz, it.mnemonic, it.op_str)
        hist.append(pc)
        op = be16(pc)
        if not (op & 0xF000 == 0x6000 or op & 0xF0F8 == 0x50C8 or op & 0xFF80 == 0x4E80):
            for rx, how in RX:
                for mm in rx.finditer(it.op_str):
                    v = int(mm.group(1),16)
                    if how == 'imm' and not (sz >= 6 and it.mnemonic in ('move.l','movea.l')): continue
                    if 0x200 <= v < N: refs[v].add((pc, it.mnemonic, how))
        # --- control flow ---
        if op in (0x4E75, 0x4E73, 0x4E77): return                     # rts rte rtr
        if op == 0x4E72: return                                        # stop
        if op & 0xFF00 == 0x6000 or op & 0xFF00 == 0x6100 or (op & 0xF000 == 0x6000):
            disp = s8(op & 0xFF)
            if disp == 0: disp = s16(be16(pc+2))
            elif disp == -1: bad.append((pc,'bra.l (68020)',src)); return
            t = pc + 2 + disp
            cc = (op >> 8) & 0xF
            if cc == 1: calls[t].add(pc); push(t, pc, 'bsr')
            else: jumps[t].add(pc); push(t, pc, 'bcc')
            if cc == 0: return
            pc += sz; continue
        if op & 0xF0F8 == 0x50C8:                                      # dbcc
            t = pc + 2 + s16(be16(pc+2)); jumps[t].add(pc); push(t, pc, 'dbcc')
            pc += sz; continue
        if op & 0xFF80 == 0x4E80:                                      # jsr / jmp
            isjmp = bool(op & 0x40); mode = (op>>3)&7; reg = op&7; t = None
            if mode == 7 and reg == 0: t = s16(be16(pc+2)) & 0xFFFFFFFF
            elif mode == 7 and reg == 1: t = be32(pc+2)
            elif mode == 7 and reg == 2: t = pc + 2 + s16(be16(pc+2))
            if t is not None:
                t &= 0xFFFFFF
                if t < N:
                    (jumps if isjmp else calls)[t].add(pc); push(t, pc, 'jmp' if isjmp else 'jsr')
                else:
                    indirect.append((pc, 'RAM '+it.mnemonic+' '+it.op_str))
            else:
                indirect.append((pc, it.mnemonic+' '+it.op_str))
                hook_indirect(pc, op, hist)
            if isjmp: return
            pc += sz; continue
        pc += sz

# ---- jump table heuristics (filled in below from --extra) ----
CODE_END = 0x1D000
auto_tables = {}             # table addr -> (fmt, [targets], site)
bra_tables = {}
descriptors = {}             # addr -> tag
ptr_entries = {}             # code addr -> site that stores the pointer
unresolved = []
orphans = []
bounds = set(json.load(open(A.bounds))) if A.bounds else set()
def plausible(t):
    """Sandboxed recursive descent from t: every path must decode cleanly and agree with committed code."""
    if t < 0x200 or t >= CODE_END or t & 1: return False
    if kind[t] == 1: return True
    if kind[t] in (2,3): return False
    seen = {}; todo = [t]; count = 0
    while todo:
        pc = todo.pop()
        while True:
            if pc < 0x200 or pc >= CODE_END or pc & 1: return False
            if kind[pc] == 1 or seen.get(pc) == 1: break
            if kind[pc] in (2,3) or seen.get(pc) == 2: return False
            it = decode(pc)
            if it is None: return False
            sz = it.size
            if any(kind[pc+i] or (pc+i) in seen for i in range(1,sz)): return False
            seen[pc] = 1
            for i in range(1,sz): seen[pc+i] = 2
            count += 1
            if count > 600: return True
            op = be16(pc)
            if op in (0x4E75,0x4E73,0x4E77): break
            if op & 0xF000 == 0x6000:
                disp = s8(op & 0xFF)
                if disp == 0: disp = s16(be16(pc+2))
                elif disp == -1: return False
                todo.append(pc+2+disp)
                if (op>>8)&0xF == 0: break
            elif op & 0xF0F8 == 0x50C8:
                todo.append(pc+2+s16(be16(pc+2)))
            elif op & 0xFF80 == 0x4E80:
                mode=(op>>3)&7; reg=op&7; x=None
                if mode==7 and reg==0: x = s16(be16(pc+2)) & 0xFFFFFF
                elif mode==7 and reg==1: x = be32(pc+2) & 0xFFFFFF
                elif mode==7 and reg==2: x = pc+2+s16(be16(pc+2))
                if x is not None and x < N: todo.append(x)
                if op & 0x40: break
            pc += sz
    return True
def read_table(T, fmt, base, site):
    if T in auto_tables: return
    tg = []; i = 0; lo = None; step = 4 if fmt == 'l' else 2
    while True:
        a = T + step*i
        if a + step > N: break
        if lo is not None and a >= lo: break
        if i > 0 and a in bounds: break
        if any(kind[a+k] in (1,2) for k in range(step)): break
        if fmt == 'l':
            v = be32(a)
            if v == 0: tg.append(None); i += 1; continue
            x = v
        else:
            x = base + s16(be16(a))
        if not plausible(x): break
        if x > T and (lo is None or x < lo): lo = x
        tg.append(x); i += 1
    while tg and tg[-1] is None: tg.pop()
    for k in range(len(tg)*step): kind[T+k] = 3
    auto_tables[T] = (fmt, tg, site)
    for x in tg:
        if x is not None: jumps[x].add(T); work.append((x, T))
RL = re.compile(r'lea\.l \$([0-9a-f]+)(?:\(pc\)|\.l|\.w), (a\d)')
def hook_indirect(pc, op, hist):
    h = [(a,)+insn[a][1:] for a in hist[-6:-1]]
    mode = (op>>3)&7; r = op&7; T = None
    if mode == 7 and r == 3: T = pc + 2 + s8(be16(pc+2) & 0xFF)          # jmp d8(pc,xn)
    elif mode == 6 and (be16(pc+2) & 0xFF) == 0:                          # jmp (an,xn)
        for a2, m2, o2 in reversed(h):
            l = RL.fullmatch(m2+' '+o2)
            if l and l.group(2) == 'a%d' % r: T = int(l.group(1),16); break
    if T is not None:
        q = T; n = 0; lo = None
        while be16(q) & 0xFF00 == 0x6000 and (lo is None or q < lo):
            dd = s8(be16(q) & 0xFF); 
            t = q + 2 + (dd if dd else s16(be16(q+2)))
            if t > q and (lo is None or t < lo): lo = t
            work.append((q, pc)); jumps[q].add(pc); n += 1
            q += 2 if dd else 4
        bra_tables[T] = (n, pc)
        if n: return
    reg = 'a%d' % (op & 7)
    if not h: unresolved.append(pc); return
    a1, m1, o1 = h[-1]
    mm = re.fullmatch(r'\((a\d), d\d\.w\), (a\d)', o1)
    if mm and m1 in ('adda.w','movea.l') and mm.group(2) == reg:
        for a2, m2, o2 in reversed(h[:-1]):
            l = RL.fullmatch(m2+' '+o2)
            if l and l.group(2) == mm.group(1):
                T = int(l.group(1),16)
                read_table(T, 'w' if m1 == 'adda.w' else 'l', T, pc); return
    mm = re.fullmatch(r'\$([0-9a-f]+)\(pc, d\d\.w\), (a\d)', o1)
    if mm and m1 == 'movea.l' and mm.group(2) == reg:
        T = int(mm.group(1),16); read_table(T, 'l', T, pc); return
    unresolved.append(pc)

for v in range(1, 64):
    t = be32(v*4)
    if 0x200 <= t < N and not t & 1: work.append((t, -v))
extra = {}
if A.extra:
    extra = json.load(open(A.extra))
    for e in extra.get('entries', []): work.append((int(e,16), -1000))
    for a,b in extra.get('data_ranges', []):
        for i in range(int(a,16), int(b,16)): kind[i] = 3
    for t in extra.get('tables', []):
        # {"addr": "...", "count": n, "fmt": "w_rel"|"l_abs"|"w_rel_base", "base": "..."}
        a = int(t['addr'],16); base = int(t.get('base', t['addr']),16); tg = []
        for i in range(t['count']):
            if t['fmt'] == 'l_abs':
                x = be32(a+4*i) & 0xFFFFFF
                for k in range(4): kind[a+4*i+k] = 3
            else:
                x = base + s16(be16(a+2*i))
                for k in range(2): kind[a+2*i+k] = 3
            tg.append(x); jumps[x].add(a); work.append((x, a))
        tables[a] = tg

SPAWN = {0x50D4, 0x50E2, 0x47A, 0x4A2, 0x444}
SPAWN_LIST = {0x5106}
anyreg = lambda s: re.sub(r'\(a[0-7]\)', '(an)', s)    # entity fields are reached through any of a0-a4
DATA_DST = set(anyreg(x) for x in extra.get('data_dst', []))
def call_target(a):
    """absolute target if instruction at a is a direct call/jump, else None"""
    if a not in insn: return None
    mm = re.search(r'^(?:jsr|jmp|bsr\.[bw]|bra\.[bw]) \$([0-9a-f]+)', insn[a][1]+' '+insn[a][2])
    return int(mm.group(1),16) if mm else None
def add_desc(t, site):
    if t in descriptors or t+4 >= CODE_END or kind[t] in (1,2): return
    if not plausible(t+4): return
    descriptors[t] = d[t:t+4].decode('latin1')
    for i in range(4): kind[t+i] = 3
    work.append((t+4, site)); ptr_entries[t+4] = site
def discover():
    for t, lst in list(refs.items()):
        if t >= CODE_END: continue
        for pc, mn, how in list(lst):
            sz, _, ops = insn[pc]; dst = ops.split(', ')[-1]
            if how == 'imm' and mn == 'move.l' and anyreg(dst) not in DATA_DST and not re.fullmatch(r'd\d', dst):
                if kind[t] == 0 and plausible(t): work.append((t, pc)); ptr_entries[t] = pc
            if mn in ('lea.l','movea.l') and dst in ('a0','a1'):
                ct = call_target(pc+sz)
                if ct in SPAWN or re.fullmatch(rb'[A-Za-z][A-Za-z0-9_]{3}', d[t:t+4]): add_desc(t, pc)
                elif ct in SPAWN_LIST:
                    q = t
                    while be32(q) and be32(q) < CODE_END: add_desc(be32(q), pc); q += 4
                    for i in range(t, q+4): kind[i] = 3
while True:
    while work:
        t, src = work.pop()
        if t >= N or t & 1: bad.append((t,'bad entry',src)); continue
        trace(t, src)
    discover()
    if work: continue
    if A.orphans:
        # unreferenced code: a gap that starts right after a terminator and traces cleanly
        TERM = lambda a: a in insn and (be16(a) in (0x4E75,0x4E73,0x4E77) or be16(a) & 0xFF00 == 0x6000 or be16(a) & 0xFFC0 == 0x4EC0)
        starts = sorted(insn)
        for a in starts:
            e = a + insn[a][0]
            if e < CODE_END and kind[e] == 0 and TERM(a) and be16(e) != 0 and plausible(e):
                # require the straight-line run from e to reach a terminator before leaving the gap
                q = e; ok = False
                for _ in range(400):
                    it = decode(q)
                    if it is None or kind[q] != 0: break
                    o = be16(q)
                    if o in (0x4E75,0x4E73,0x4E77) or o & 0xFF00 == 0x6000 or o & 0xFFC0 == 0x4EC0: ok = True; break
                    q += it.size
                if ok: orphans.append(e); work.append((e, -2000)); break
    if not work: break

code = sum(1 for k in kind if k in (1,2))
print(f"instructions: {len(insn)}  code bytes: {code} ({100*code/N:.1f}% of ROM)  subroutines: {len(calls)}")
print(f"orphan entries: {len(orphans)}")
print(f"indirect sites: {len(indirect)}  auto tables: {len(auto_tables)}  unresolved: {len(unresolved)}  problems: {len(bad)}")
if A.v:
    for b in bad: print("  BAD %06X %s from %s" % (b[0], b[1], ('%06X'%b[2]) if b[2]>=0 else 'vec%d'%-b[2]))
if A.out:
    json.dump({'kind': bytes(kind).hex(), 'insn': {('%06X'%k): v for k,v in sorted(insn.items())},
               'calls': {('%06X'%k): sorted(v) for k,v in calls.items()},
               'jumps': {('%06X'%k): sorted(v) for k,v in jumps.items()},
               'indirect': indirect, 'orphans': orphans, 'descriptors': {('%06X'%k): v for k,v in descriptors.items()}, 'bra_tables': {('%06X'%k): v for k,v in bra_tables.items()}, 'ptr_entries': {('%06X'%k): v for k,v in ptr_entries.items()}, 'unresolved': unresolved, 'auto_tables': {('%06X'%k): v for k,v in auto_tables.items()}, 'refs': {('%06X'%k): sorted(v) for k,v in refs.items()}, 'bad': bad, 'tables': {('%06X'%k): v for k,v in tables.items()}},
              open(A.out,'w'))
