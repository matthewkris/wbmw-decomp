"""Translate 68000 routines into C with exact registers, condition codes and stack effects.

Usage: m68k2c.py ROM work/map.json OUTDIR CONFIG.json

CONFIG (analysis/translate.json) gives the game's facts, as hex address strings:
  roots   routines to translate, each with everything it calls
  yield   the routine that suspends a task: it pops its caller's return address into the task
          slot and returns to the scheduler. The instruction after every call to it (or to a
          routine that jumps to it) is where the task resumes, so it becomes an entry point.
  exits   addresses where generated code stops instead of continuing: code that resets the
          stack and re-enters the frame loop, waits for the next frame, or hangs. Reaching one
          ends the call with c->ret = that address.
  defer   entries deliberately not translated yet (with what only they reach).
  contracts  {"ADDR": ["sp", "d6", ...]}: check that routine only on these registers and flags
          (and memory), watching the rest at run time (port.h, contracts). For translated routines
          that call ordinary C rewrites, whose leftover registers and stack differ.
  restart (native build) exits that are also translated as entries of their own: code reaching
          one unwinds to the host, which then calls it fresh (re-entering the frame loop with a
          reset stack, so the C stack does not grow).
  poll    (native build) instructions in loops that wait for an interrupt: generated code calls
          rt_poll(c, address) there first, and the host runs a frame and the interrupt.
  cycles  (native build) a file of 65,536 bytes: the 68000 cycles of each opcode, as Musashi
          charges them (harness/cycles_dump.c). Each instruction then ends with
          RT_CYC(cycles, next pc), placed where Musashi charges it: after the instruction's
          effects, before any jump, call or return. The dynamic costs follow Musashi's 68000
          rules: Bcc not taken (.b -2, .w +2), DBcc (branch -2, expiry +2), Scc true to a
          register (+2), MOVEM (+4 a word register, +8 a long), shifts and rotates (+2 a bit
          counted), MULU/MULS (+2 per one, or per change, in the source).
  hand_ports  false: translate routines even where port/ has a hand port (the native build's
          timing needs every instruction counted).
  seen    entries found only at run time: targets of computed jumps the tracer could not
          resolve, from the harness's "left generated code" report. Translated like roots.

A call or jump to code that is not translated goes through rt_dispatch at run time; if that
has no translation either, rt_leave (provided by the host) decides: the harness hands control
back to the 68000 there (the original code is always present), a native build stops.

Each root and every routine it calls (that is not already hand-ported in port/registry.c) gets
one C function g_XXXXXX(Cpu *c) in OUTDIR/gen.c, built on port/m68k_rt.h; OUTDIR/gen_registry.c
lists them for the harness (so they can be checked and run in place of the originals) and
implements rt_dispatch for indirect calls. Each instruction becomes one statement with the
original in a comment; local branches become gotos. This is scaffolding (docs/PORT.md): the
output is derived from the ROM's code and is never committed. Game-independent.
"""
import sys, os, re, json

ROM, MAP, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
CONFIG = json.load(open(sys.argv[4]))
ROOTS = [int(a, 16) for a in CONFIG['roots'] + CONFIG.get('seen', [])]
YIELD = int(CONFIG['yield'], 16) if 'yield' in CONFIG else None
EXITS = {int(a, 16) for a in CONFIG.get('exits', [])}
DEFER = {int(a, 16) for a in CONFIG.get('defer', [])}
# contracts (port.h): the registers and flags a translated routine's check compares, by name;
# the rest is left to the harness's dead-register watch. For routines that call ordinary C.
def contract_mask(names):
    m = 1 << 15
    for n in names:
        n = n.lower()
        if n == 'sp': continue
        if n == 'nzvc': m |= 1 << 16
        elif n == 'x': m |= 1 << 17
        else: m |= 1 << (int(n[1]) + (8 if n[0] == 'a' else 0))
    return m
CONTRACTS = {int(a, 16): contract_mask(v) for a, v in CONFIG.get('contracts', {}).items()}
RESTART = {int(a, 16) for a in CONFIG.get('restart', [])}
EXITS |= RESTART
POLL = {int(a, 16) for a in CONFIG.get('poll', [])}
CYC = open(os.path.join(os.path.dirname(MAP), '..', CONFIG['cycles']), 'rb').read() if 'cycles' in CONFIG else None
d = open(ROM, 'rb').read()
m = json.load(open(MAP))
INS = {int(a, 16): v for a, v in m['insn'].items()}
CALLS = {int(a, 16) for a in m['calls']}

# hand ports: {0xADDR, 0xEND, "Name", Fn}
HAND = {}
for line in open(os.path.join(os.path.dirname(__file__), '..', 'port', 'registry.c')):
    mm = re.search(r'\{0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), "(\w+)", (\w+)(?:, [^}]*)?\}', line)
    if mm: HAND[int(mm.group(1), 16)] = mm.group(4)

if CONFIG.get('hand_ports', True) is False: HAND = {}

# what the rewritten C calls in code not rewritten yet (call_68k and its wrappers, or hands over
# to with port_continue_at) must be an entry, even when no generated code calls it any more
C_CALL = re.compile(r'\b(?:call_68k|with_player|call_with_d0|with_entity|call_at|call_d0|call|port_continue_at)\s*\([^;]*?\b0x([0-9A-Fa-f]{3,6})\b')
# the same through a name the file defines (#define SET_ANIM 0x7E8u)
C_DEFINE = re.compile(r'^\s*#define\s+(\w+)\s+0x([0-9A-Fa-f]{3,6})u?\b')
C_CALL_NAMED = re.compile(r'\b(?:call_68k|with_player|call_with_d0|with_entity|call_at|call_d0|call|port_continue_at)\s*\(\s*(?:\w+\s*,\s*)?([A-Z_][A-Z0-9_]*)\b')
for name in sorted(os.listdir(os.path.join(os.path.dirname(__file__), '..', 'port'))):
    if name.endswith('.c'):
        lines = open(os.path.join(os.path.dirname(__file__), '..', 'port', name), encoding='utf-8').read().splitlines()
        named = {mm.group(1): int(mm.group(2), 16) for mm in map(C_DEFINE.match, lines) if mm}
        for line in lines:
            for mm in C_CALL.finditer(line):
                a = int(mm.group(1), 16)
                if a not in ROOTS: ROOTS.append(a)
            for mm in C_CALL_NAMED.finditer(line):
                a = named.get(mm.group(1))
                if a is not None and a in INS and a not in ROOTS: ROOTS.append(a)

# indirect sites with known targets: auto tables, hint tables and bra tables
SITE_TARGETS = {}
for t, (fmt, tg, site) in m['auto_tables'].items():
    SITE_TARGETS.setdefault(site, set()).update(x for x in tg if x is not None)
for t, (n, site) in m['bra_tables'].items():
    T = int(t, 16); q = T
    for _ in range(n):
        SITE_TARGETS.setdefault(site, set()).add(q)
        op = d[q] << 8 | d[q + 1]
        q += 2 if op & 0xFF else 4

# indirect jumps through an address loaded by `lea` shortly before: `lea X,aN; jsr (aN)` goes to
# X, and a jump that indexes from X goes to the entries of the hint table based at X
# (analysis/hints.json "tables", e.g. the two-level behaviour tables at $10A5A)
HINT_BASES = {}
for t in json.load(open(os.path.join(os.path.dirname(MAP), '..', 'analysis', 'hints.json'))).get('tables', []):
    tg = m['tables'].get('%06X' % int(t['addr'], 16))
    if tg: HINT_BASES.setdefault(int(t.get('base', t['addr']), 16), set()).update(tg)
_ORDER = sorted(INS); _AT = {a: i for i, a in enumerate(_ORDER)}
for site in m['unresolved']:
    if site not in _AT or SITE_TARGETS.get(site): continue
    reg = re.fullmatch(r'\((a[0-7])\)', INS[site][2])
    for back in range(1, 12):
        i = _AT[site] - back
        if i < 0: break
        sz, mn, ops = INS[_ORDER[i]]
        mm = re.fullmatch(r'\$([0-9a-f]+)(?:\.[wl]|\(pc\)), (a[0-7])', ops) if mn == 'lea.l' else None
        if not mm: continue
        x = int(mm.group(1), 16)
        if back == 1 and reg and mm.group(2) == reg.group(1) and x in INS: SITE_TARGETS[site] = {x}
        elif x in HINT_BASES: SITE_TARGETS[site] = set(HINT_BASES[x])
        break

# continuations kept in RAM: `move.l #code,VAR` somewhere, `movea.l VAR,aN; jmp (aN)` elsewhere.
# STORES[VAR] = the code addresses ever stored there, the possible targets of such a jump.
STORES = {}
for ptr, site in m['ptr_entries'].items():
    mm = re.fullmatch(r'#\$[0-9a-f]+, (.+)', INS.get(site, (0, '', ''))[2])
    if mm: STORES.setdefault(mm.group(1).replace(' ', ''), set()).add(int(ptr, 16))

# every code address the program stores as a pointer, with the instructions that store it
STORED_BY = {}
for ptr, site in m['ptr_entries'].items():
    if int(ptr, 16) in INS: STORED_BY.setdefault(int(ptr, 16), set()).add(site)
for a, (sz, mn, ops) in INS.items():                   # `move.l #code,X` the tracer did not record
    mm = re.match(r'#\$([0-9a-f]+), ', ops) if mn in ('move.l', 'movea.l') else None
    if mm and int(mm.group(1), 16) in INS and int(mm.group(1), 16) >= 0x200:
        STORED_BY.setdefault(int(mm.group(1), 16), set()).add(a)
# `lea code,aN`: a code pointer, often handed on to be stored (the player's input routine: `lea
# $C1E0(pc),a0; bsr $C0C2`, which stores a0 in $FF9F22)
for a, (sz, mn, ops) in INS.items():
    mm = re.fullmatch(r'\$([0-9a-f]+)(?:\.[wl]|\(pc\)), a[0-7]', ops) if mn == 'lea.l' else None
    if mm and int(mm.group(1), 16) in INS and int(mm.group(1), 16) >= 0x200:
        STORED_BY.setdefault(int(mm.group(1), 16), set()).add(a)

def push_rts(a):
    """`move.l X,-(a7); rts` jumps to X; with `pea R` before it, X returns to R (a computed call,
    as in the animation interpreter at $88E). Returns (is_it, R or None, X's table targets)."""
    prev = [p for p in (a - 2, a - 4, a - 6) if p in INS and p + INS[p][0] == a]
    if not prev or INS[prev[0]][1] != 'move.l' or not INS[prev[0]][2].endswith('-(a7)'): return False, None, set()
    p1 = prev[0]; ret = None
    pp = [p for p in (p1 - 2, p1 - 4, p1 - 6) if p in INS and p + INS[p][0] == p1]
    if pp and INS[pp[0]][1] == 'pea.l':
        mm = re.fullmatch(r'\$([0-9a-f]+)(?:\(pc\)|\.[wl])?', INS[pp[0]][2])
        if mm: ret = int(mm.group(1), 16)
    targets = set()
    mm = re.match(r'\$([0-9a-f]+)\(pc', INS[p1][2])
    if mm: targets = set(m['tables'].get('%06X' % int(mm.group(1), 16), []))
    return True, ret, targets

def continuation_targets(a):
    """targets of `jmp (aN)` at a when aN was just loaded from a RAM variable"""
    sz, mn, ops = INS[a]
    mm = re.fullmatch(r'\((a[0-7])\)', ops)
    if not mm: return set()
    prev = [p for p in INS if p < a and p + INS[p][0] == a]
    if not prev: return set()
    pm = re.fullmatch(r'(.+), ' + mm.group(1), INS[prev[0]][2])
    if INS[prev[0]][1] != 'movea.l' or not pm: return set()
    return STORES.get(pm.group(1).replace(' ', ''), set())

class Unsupported(Exception): pass

SIZE = {'b': 1, 'w': 2, 'l': 4}
CONDS = {'t': 0, 'ra': 0, 'f': 1, 'hi': 2, 'ls': 3, 'cc': 4, 'hs': 4, 'cs': 5, 'lo': 5, 'ne': 6, 'eq': 7,
         'vc': 8, 'vs': 9, 'pl': 10, 'mi': 11, 'ge': 12, 'lt': 13, 'gt': 14, 'le': 15}

def split_ops(ops):
    out, depth, cur = [], 0, ''
    for ch in ops:
        if ch == '(': depth += 1
        if ch == ')': depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def hexv(s): return int(s.replace('$', '0x'), 16) if '$' in s else int(s)

def reg(name):
    name = name.strip()
    if name == 'sp': name = 'a7'
    return name[0], int(name[1])

def idx_expr(x):
    k, n = reg(x[:2]); size = x[3]
    r = 'c->%s[%d]' % ('d' if k == 'd' else 'a', n)
    return r if size == 'l' else '(uint32_t)(int32_t)(int16_t)%s' % r

class EA:
    """an operand: register, immediate or memory (address expression + side effects)"""
    def __init__(self, op, sz, pc):
        op = op.replace(' ', '')
        self.pre, self.post, self.kind, self.text = [], [], None, op
        mm = re.fullmatch(r'([da])([0-7])|sp', op)
        if mm:
            k, n = reg(op); self.kind = k; self.n = n; return
        mm = re.fullmatch(r'#(-?\$?[0-9a-fA-F]+)', op)
        if mm: self.kind = 'imm'; self.v = hexv(mm.group(1)) & 0xFFFFFFFF; return
        step = lambda n: 2 if n == 7 and sz == 1 else sz
        self.kind = 'mem'
        mm = re.fullmatch(r'\(a([0-7])\)', op) or re.fullmatch(r'\(sp\)', op)
        if mm: n = int(mm.group(1)) if mm.groups() else 7; self.addr = 'c->a[%d]' % n; return
        mm = re.fullmatch(r'\(a([0-7])\)\+', op)
        if mm: n = int(mm.group(1)); self.addr = 'c->a[%d]' % n; self.post = ['c->a[%d] += %d;' % (n, step(n))]; return
        mm = re.fullmatch(r'-\(a([0-7])\)', op)
        if mm: n = int(mm.group(1)); self.pre = ['c->a[%d] -= %d;' % (n, step(n))]; self.addr = 'c->a[%d]' % n; return
        mm = re.fullmatch(r'(-?\$[0-9a-f]+)\(a([0-7])\)', op)
        if mm: self.addr = 'c->a[%d] + (uint32_t)(%d)' % (int(mm.group(2)), hexv(mm.group(1))); return
        mm = re.fullmatch(r'(-?\$[0-9a-f]+)?\(a([0-7]),([da][0-7]\.[wl])\)', op)
        if mm:
            disp = hexv(mm.group(1)) if mm.group(1) else 0
            self.addr = 'c->a[%d] + (uint32_t)(%d) + %s' % (int(mm.group(2)), disp, idx_expr(mm.group(3))); return
        mm = re.fullmatch(r'\$([0-9a-f]+)\(pc\)', op)
        if mm: self.addr = '0x%Xu' % int(mm.group(1), 16); self.const = int(mm.group(1), 16); return
        mm = re.fullmatch(r'\$([0-9a-f]+)\(pc,([da][0-7]\.[wl])\)', op)
        if mm: self.addr = '0x%Xu + %s' % (int(mm.group(1), 16), idx_expr(mm.group(2))); return
        mm = re.fullmatch(r'\$([0-9a-f]+)\.w', op)
        if mm:
            v = int(mm.group(1), 16); v = v - 0x10000 if v & 0x8000 else v
            self.addr = '0x%Xu' % (v & 0xFFFFFFFF); self.const = v & 0xFFFFFFFF; return
        mm = re.fullmatch(r'\$([0-9a-f]+)\.l', op)
        if mm: self.addr = '0x%Xu' % int(mm.group(1), 16); self.const = int(mm.group(1), 16); return
        raise Unsupported('operand ' + op)

    def reg_ref(self): return 'c->%s[%d]' % ('d' if self.kind == 'd' else 'a', self.n)

def read(ea, sz, var, lines):
    """emit code that reads ea into `var`; returns nothing (address kept in var + '_a')"""
    if ea.kind in ('d', 'a'): lines.append('uint32_t %s = rt_rd(%s, %d);' % (var, ea.reg_ref(), sz)); return
    if ea.kind == 'imm': lines.append('uint32_t %s = 0x%Xu;' % (var, ea.v & (0xFF if sz == 1 else 0xFFFF if sz == 2 else 0xFFFFFFFF))); return
    lines += ea.pre
    lines.append('uint32_t %s_a = %s;' % (var, ea.addr))
    lines += ea.post
    lines.append('uint32_t %s = rt_mrd(%s_a, %d);' % (var, var, sz))

def address(ea, var, lines):
    lines += ea.pre
    lines.append('uint32_t %s_a = %s;' % (var, ea.addr))
    lines += ea.post

def write(ea, sz, val, lines, var=None):
    if ea.kind in ('d', 'a'):
        if ea.kind == 'a': lines.append('%s = %s;' % (ea.reg_ref(), val))
        else: lines.append('rt_wr(&%s, %d, %s);' % (ea.reg_ref(), sz, val))
        return
    if var:                                   # read-modify-write: address already computed
        lines.append('rt_mwr(%s_a, %d, %s);' % (var, sz, val)); return
    lines += ea.pre
    lines.append('{ uint32_t w_a = %s;' % ea.addr)
    lines += ea.post
    lines.append('rt_mwr(w_a, %d, %s); }' % (sz, val))

class Fn:
    def __init__(self, entry):
        self.entry = entry
        self.body = set(); self.targets = {entry}; self.calls = set()
        self.tails = set()                    # static jumps to other routines (tail calls)
        self.sites = []                       # static calls: (site, target, return address)
        work = [entry]
        while work:
            a = work.pop()
            while a in INS and a not in self.body:
                self.body.add(a)
                if a in EXITS and a != entry: break      # stops here; the 68000 carries on
                sz, mn, ops = INS[a]
                base = mn.split('.')[0]
                t = re.search(r'\$([0-9a-f]+)$', ops)
                if base == 'rts':
                    pr, ret, tg = push_rts(a)
                    if pr:
                        self.calls |= tg
                        if ret is not None: work.append(ret); self.targets.add(ret)
                    break
                if base == 'rte': break
                if base == 'jmp':
                    tgt = self.static_target(ops)
                    if tgt in EXITS: break
                    if tgt is not None and tgt not in CALLS: work.append(tgt); self.targets.add(tgt)
                    elif tgt is not None: self.calls.add(tgt); self.tails.add(tgt)
                    for x in SITE_TARGETS.get(a, ()):
                        if x in CALLS: self.calls.add(x)
                        else: work.append(x); self.targets.add(x)
                    self.calls |= continuation_targets(a)          # reached through rt_dispatch
                    break
                if base in ('jsr', 'bsr'):
                    tgt = self.static_target(ops) if base == 'jsr' else int(t.group(1), 16)
                    if tgt is not None: self.calls.add(tgt); self.sites.append((a, tgt, a + sz))
                    for x in SITE_TARGETS.get(a, ()): self.calls.add(x); self.sites.append((a, x, a + sz))
                elif base.startswith('db') or (base[0] == 'b' and base[1:] in CONDS):
                    tgt = int(t.group(1), 16); work.append(tgt); self.targets.add(tgt)
                    if base in ('bra',): break
                a += sz

    @staticmethod
    def static_target(ops):
        mm = re.fullmatch(r'\$([0-9a-f]+)(\.w|\.l)?', ops.strip())
        if not mm: return None
        v = int(mm.group(1), 16)
        if mm.group(2) == '.w' and v & 0x8000: return None      # into RAM
        return v

def opword(a): return d[a] << 8 | d[a + 1]

def cyc(a, nxt, extra=None):
    """the cycle charge for the instruction at a (empty without a cycle table)"""
    if CYC is None: return ''
    n = CYC[opword(a)]
    return 'RT_CYC(%d%s, %s);' % (n, (' + (%s)' % extra) if extra else '', nxt)

def hx(v): return '0x%Xu' % v

def call_stmt(tgt, ret, cy=''):
    """a call to a known routine: push, call, continue only if it came back here"""
    if tgt in EXITS: return ['rt_push(c, 0x%Xu); %s c->ret = 0x%Xu; return;' % (ret, cy, tgt)]
    if tgt not in HAND and tgt not in fns:                   # not translated: dispatch at run time
        return ['rt_push(c, 0x%Xu); %s if (!rt_dispatch(c, 0x%Xu)) rt_leave(c, 0x%Xu, 0x%Xu); if (c->ret != 0x%Xu) return;'
                % (ret, cy, tgt, ret, tgt, ret)]
    if tgt in HAND:
        return ['rt_push(c, 0x%Xu); %s c->ret = 0; %s(c); rt_after_hand(c); if (c->ret != 0x%Xu) return;'
                % (ret, cy, HAND[tgt], ret)]
    return ['rt_push(c, 0x%Xu); %s g_%06X(c); if (c->ret != 0x%Xu) return;' % (ret, cy, tgt, ret)]

def tail_stmt(tgt, cy=''):
    if tgt in EXITS: return ['%s c->ret = 0x%Xu; return;' % (cy, tgt)]
    if tgt not in HAND and tgt not in fns:
        return ['%s if (!rt_dispatch(c, 0x%Xu)) rt_leave(c, 0, 0x%Xu); return;' % (cy, tgt, tgt)]
    if tgt in HAND: return ['%s c->ret = 0; %s(c); rt_after_hand(c); return;' % (cy, HAND[tgt])]
    return ['%s g_%06X(c); return;' % (cy, tgt)]

def translate(fn):
    out = []
    labels = fn.targets
    if fn.entry != min(fn.body):              # code laid out by address: start at the entry
        out.append('    goto L_%06X;' % fn.entry)
    for a in sorted(fn.body):
        sz_b, mn, ops = INS[a]
        nxt = a + sz_b
        if a in labels: out.append('L_%06X: RT_AT(0x%06X);' % (a, a))
        try:
            # an exit inside a routine (a loop that waits for an interrupt): hand it to the 68000
            if a in EXITS and a != fn.entry: body = ['c->ret = 0x%Xu; return; /* exit */' % a]
            else:
                body = insn(fn, a, nxt, mn, ops)
                if CYC is not None and not controls(mn): body = plain_cycles(a, nxt, mn, ops, body)
        except Unsupported as e:
            body = ['rt_unknown(0x%Xu, 0); /* unsupported: %s */' % (a, e)]
            UNSUPPORTED.append((a, mn, ops, str(e)))
        if a in POLL: body = ['rt_poll(c, 0x%Xu);' % a] + body
        out.append('    { %s }  /* %06X %s %s */' % (' '.join(body), a, mn, ops))
    out.append('    rt_unknown(0x%Xu, 0xFFFFFFFFu);  /* never reached: every path ends in rts or a jump */' % fn.entry)
    return out

CONTROL = {'rts', 'rte', 'bra', 'bsr', 'jmp', 'jsr'}
def controls(mn):
    base = mn.split('.')[0]
    return base in CONTROL or base.startswith('db') or (base[0] == 'b' and base[1:] in CONDS
                                                         and base not in ('bset', 'bclr', 'bchg', 'btst'))

def plain_cycles(a, nxt, mn, ops, body):
    """the charge for an instruction without control flow, with Musashi's dynamic extras"""
    base = mn.split('.')[0]; parts = split_ops(ops) if ops else []
    pre, extra = [], None
    if base in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr') and len(parts) == 2:
        if parts[0].startswith('#'): extra = '%d' % (2 * hexv(parts[0][1:]))
        else:                                          # the count, read before the shift changes it
            k, n = reg(parts[0]); pre = ['unsigned cnt_ = c->d[%d] & 63;' % n]; extra = '2 * cnt_'
    elif base == 'movem':
        regs = [p for p in (parts[0] if re.fullmatch(r'[da][0-7]([-/][da][0-7])*', parts[0].replace(' ', '')) else parts[1]).replace(' ', '').split('/')]
        count = 0
        for r in regs:
            if '-' in r: x, y = r.split('-'); count += reg(y)[1] - reg(x)[1] + 1
            else: count += 1
        extra = '%d' % (count * (8 if mn.endswith('.l') else 4))
    elif base in ('mulu', 'muls'):
        extra = 'rt_%s_cyc(s)' % base
    elif base[0] == 's' and base[1:] in CONDS and re.fullmatch(r'd[0-7]', parts[0]):
        extra = '(rt_cond(c, %d) ? 2 : 0)' % CONDS[base[1:]]
    return pre + body + [cyc(a, hx(nxt), extra)]

def insn(fn, a, nxt, mn, ops):
    parts = split_ops(ops) if ops else []
    base, _, suf = mn.partition('.')
    sz = SIZE.get(suf, 4)
    L = []
    if base in ('nop',): return ['/* nop */']
    if base == 'rts':
        pr, ret, tg = push_rts(a)
        if pr:                                     # a computed call (or jump) through the stack
            L = ['uint32_t t_a = rd32(c->a[7]); c->a[7] += 4;', cyc(a, 't_a'),
                 'if (!rt_dispatch(c, t_a)) rt_leave(c, 0x%Xu, t_a);' % a]
            L.append('if (c->ret != 0x%Xu) return; goto L_%06X;' % (ret, ret) if ret is not None else 'return;')
            return L
        return ['rt_rts(c); %s return;' % cyc(a, 'c->ret')]
    if base == 'rte':
        if CYC is not None:                           # SR (with the interrupt mask) and PC; the host checks interrupts
            return ['{ uint16_t sr_ = rd16(c->a[7]); c->ret = rd32(c->a[7] + 2); c->a[7] += 6; rt_set_sr(c, sr_); } %s return;'
                    % cyc(a, 'c->ret')]
        return ['c->ccr = (uint16_t)(rd16(c->a[7]) & 0x1F); c->ret = rd32(c->a[7] + 2); c->a[7] += 6; return;']
    if base == 'bra': t = int(ops[1:], 16); return ['%s goto L_%06X;' % (cyc(a, hx(t)), t)]
    if base == 'bsr': t = int(ops[1:], 16); return call_stmt(t, nxt, cyc(a, hx(t)))
    if base[0] == 'b' and base[1:] in CONDS and base not in ('bset', 'bclr', 'bchg', 'btst'):
        t = int(ops[1:], 16)
        if CYC is None: return ['if (rt_cond(c, %d)) goto L_%06X;' % (CONDS[base[1:]], t)]
        return ['if (rt_cond(c, %d)) { %s goto L_%06X; } %s'
                % (CONDS[base[1:]], cyc(a, hx(t)), t, cyc(a, hx(nxt), '-2' if suf == 'b' else '2'))]
    if base.startswith('db'):
        cond = CONDS['f' if base == 'dbra' else base[2:]]
        k, n = reg(parts[0]); t = int(parts[1][1:], 16)
        if CYC is None:
            return ['if (!rt_cond(c, %d)) { uint32_t w = (c->d[%d] - 1) & 0xFFFF; rt_wr(&c->d[%d], 2, w); if (w != 0xFFFF) goto L_%06X; }'
                    % (cond, n, n, t)]
        return ['if (!rt_cond(c, %d)) { uint32_t w = (c->d[%d] - 1) & 0xFFFF; rt_wr(&c->d[%d], 2, w); '
                'if (w != 0xFFFF) { %s goto L_%06X; } %s } else { %s }'
                % (cond, n, n, cyc(a, hx(t), '-2'), t, cyc(a, hx(nxt), '2'), cyc(a, hx(nxt)))]
    if base in ('jmp', 'jsr'):
        tgt = Fn.static_target(ops)
        if tgt is not None:
            if base == 'jsr': return call_stmt(tgt, nxt, cyc(a, hx(tgt)))
            if tgt in fn.targets and tgt not in CALLS: return ['%s goto L_%06X;' % (cyc(a, hx(tgt)), tgt)]
            return tail_stmt(tgt, cyc(a, hx(tgt)))
        ea = EA(parts[0], 4, a)
        if ea.kind != 'mem': raise Unsupported('jump operand')
        address(ea, 't', L)
        if base == 'jsr':
            L.append('rt_push(c, 0x%Xu); %s if (!rt_dispatch(c, t_a)) rt_leave(c, 0x%Xu, t_a); if (c->ret != 0x%Xu) return;'
                     % (nxt, cyc(a, 't_a'), a, nxt))
            return L
        if CYC is not None: L.append(cyc(a, 't_a'))
        local = sorted(x for x in SITE_TARGETS.get(a, ()) if x in fn.body)
        L.append('switch (t_a) {' + ''.join(' case 0x%Xu: goto L_%06X;' % (x, x) for x in local) + ' }')
        L.append('if (!rt_dispatch(c, t_a)) rt_leave(c, 0x%Xu, t_a); return;' % a)
        return L
    if base == 'moveq':
        v = hexv(parts[0][1:]) & 0xFF
        k, n = reg(parts[1])
        return ['c->d[%d] = 0x%Xu; rt_logic(c, 4, c->d[%d]);' % (n, (v - 256 if v & 0x80 else v) & 0xFFFFFFFF, n)]
    if base == 'lea':
        ea = EA(parts[0], 4, a); k, n = reg(parts[1])
        address(ea, 'e', L); L.append('c->a[%d] = e_a;' % n); return L
    if base == 'pea':
        ea = EA(parts[0], 4, a); address(ea, 'e', L); L.append('rt_push(c, e_a);'); return L
    if base == 'link':
        k, n = reg(parts[0]); disp = hexv(parts[1][1:])
        disp = disp - 0x10000 if disp & 0x8000 else disp
        return ['rt_push(c, c->a[%d]); c->a[%d] = c->a[7]; c->a[7] += (uint32_t)(%d);' % (n, n, disp)]
    if base == 'unlk':
        k, n = reg(parts[0])
        return ['c->a[7] = c->a[%d]; c->a[%d] = rd32(c->a[7]); c->a[7] += 4;' % (n, n)]
    if base == 'swap':
        k, n = reg(parts[0])
        return ['c->d[%d] = c->d[%d] << 16 | c->d[%d] >> 16; rt_logic(c, 4, c->d[%d]);' % (n, n, n, n)]
    if base == 'exg':
        x, y = (EA(p, 4, a) for p in parts)
        return ['{ uint32_t t = %s; %s = %s; %s = t; }' % (x.reg_ref(), x.reg_ref(), y.reg_ref(), y.reg_ref())]
    if base == 'ext':
        k, n = reg(parts[0])
        return ['c->d[%d] = rt_ext(c, %d, c->d[%d]);' % (n, sz, n)]
    if base in ('st', 'sf') or (base[0] == 's' and base[1:] in CONDS):
        cond = CONDS[base[1:]]
        ea = EA(parts[0], 1, a)
        write(ea, 1, '(rt_cond(c, %d) ? 0xFFu : 0)' % cond, L); return L
    if base in ('move', 'movea'):
        if base == 'move' and suf == '' and parts[1] == 'usp':
            return ['/* move to usp: no user mode in the port */']
        if base == 'move' and suf == '':
            raise Unsupported('move to/from usp, sr or ccr')
        if parts[1] == 'sr' and parts[0].startswith('#'):          # the interrupt mask is the host's business
            if CYC is not None: return ['rt_set_sr(c, 0x%Xu);' % (hexv(parts[0][1:]) & 0xFFFF)]
            return ['c->ccr = 0x%Xu;' % (hexv(parts[0][1:]) & 0x1F)]
        if any(p in ('sr', 'ccr', 'usp') for p in parts): raise Unsupported('special register')
        src, dst = EA(parts[0], sz, a), EA(parts[1], sz, a)
        read(src, sz, 's', L)
        if dst.kind == 'a': L.append('%s = rt_sext(s, %d);' % (dst.reg_ref(), sz)); return L
        write(dst, sz, 's', L); L.append('rt_logic(c, %d, s);' % sz); return L
    if base == 'movem':
        return movem(parts, sz, a)
    if base in ('clr', 'tst', 'not', 'neg', 'negx'):
        ea = EA(parts[0], sz, a)
        if base == 'clr':
            if ea.kind == 'mem': read(ea, sz, 'v', L); write(ea, sz, '0', L, 'v')     # the 68000 reads first
            else: write(ea, sz, '0', L)
            L.append('rt_logic(c, %d, 0);' % sz); return L
        read(ea, sz, 'v', L)
        if base == 'tst': L.append('rt_logic(c, %d, v);' % sz); return L
        if base == 'not': L.append('uint32_t r = ~v; rt_logic(c, %d, r);' % sz)
        elif base == 'neg': L.append('uint32_t r = rt_neg(c, %d, v);' % sz)
        else: L.append('uint32_t r = rt_negx(c, %d, v);' % sz)
        write(ea, sz, 'r', L, 'v' if ea.kind == 'mem' else None); return L
    if base in ('add', 'adda', 'addi', 'addq', 'sub', 'suba', 'subi', 'subq', 'and', 'andi', 'or', 'ori', 'eor', 'eori',
                'addx', 'subx'):
        op = base.rstrip('aiq') if base not in ('addx', 'subx') else base
        if parts[1] in ('ccr', 'sr'):
            if parts[1] == 'sr': raise Unsupported('logic to sr')
            v = hexv(parts[0][1:]) & 0x1F
            return ['c->ccr = (uint16_t)(c->ccr %s 0x%X);' % ({'and': '&', 'or': '|', 'eor': '^'}[op], v)]
        src, dst = EA(parts[0], sz, a), EA(parts[1], sz, a)
        read(src, sz, 's', L)
        if dst.kind == 'a':                                   # adda/suba/addq/subq to An: long, no flags
            ext = 's' if sz == 4 else 'rt_sext(s, %d)' % sz
            L.append('%s %s= %s;' % (dst.reg_ref(), '+' if op == 'add' else '-', ext)); return L
        read(dst, sz, 'd', L)
        if op in ('add', 'sub'): L.append('uint32_t r = rt_%s(c, %d, s, d);' % (op, sz))
        elif op in ('addx', 'subx'): L.append('uint32_t r = rt_%s(c, %d, s, d);' % (op, sz))
        else: L.append('uint32_t r = d %s s; rt_logic(c, %d, r);' % ({'and': '&', 'or': '|', 'eor': '^'}[op], sz))
        write(dst, sz, 'r', L, 'd' if dst.kind == 'mem' else None); return L
    if base in ('cmp', 'cmpi', 'cmpa', 'cmpm'):
        src, dst = EA(parts[0], sz, a), EA(parts[1], sz, a)
        read(src, sz, 's', L); read(dst, 4 if dst.kind == 'a' else sz, 'd', L)
        if dst.kind == 'a': L.append('rt_cmp(c, 4, rt_sext(s, %d), d);' % sz)
        else: L.append('rt_cmp(c, %d, s, d);' % sz)
        return L
    if base in ('asl', 'asr', 'lsl', 'lsr', 'rol', 'ror', 'roxl', 'roxr'):
        kind = 'RT_' + base.upper()
        if len(parts) == 1:                                   # memory: by one, word
            ea = EA(parts[0], 2, a); read(ea, 2, 'v', L)
            L.append('uint32_t r = rt_shift(c, %s, 2, v, 1);' % kind); write(ea, 2, 'r', L, 'v'); return L
        k, n = reg(parts[1])
        if parts[0].startswith('#'): cnt = '%du' % hexv(parts[0][1:])
        else: kk, nn = reg(parts[0]); cnt = '(c->d[%d] & 63)' % nn
        return ['rt_wr(&c->d[%d], %d, rt_shift(c, %s, %d, c->d[%d], %s));' % (n, sz, kind, sz, n, cnt)]
    if base in ('btst', 'bset', 'bclr', 'bchg'):
        dst = EA(parts[1], 1, a)
        bit = ('(%du)' % hexv(parts[0][1:])) if parts[0].startswith('#') else '(c->d[%d])' % reg(parts[0])[1]
        if dst.kind == 'd':
            L.append('unsigned b = %s & 31; uint32_t v = %s;' % (bit, dst.reg_ref()))
            L.append('rt_btst(c, v, b);')
            if base != 'btst':
                op = {'bset': 'v | (1u << b)', 'bclr': 'v & ~(1u << b)', 'bchg': 'v ^ (1u << b)'}[base]
                L.append('%s = %s;' % (dst.reg_ref(), op))
            return L
        read(dst, 1, 'v', L)
        L.append('unsigned b = %s & 7; rt_btst(c, v, b);' % bit)
        if base != 'btst':
            op = {'bset': 'v | (1u << b)', 'bclr': 'v & ~(1u << b)', 'bchg': 'v ^ (1u << b)'}[base]
            write(dst, 1, op, L, 'v')
        return L
    if base in ('mulu', 'muls', 'divu', 'divs'):
        src = EA(parts[0], 2, a); k, n = reg(parts[1])
        read(src, 2, 's', L)
        L.append('c->d[%d] = rt_%s(c, s, c->d[%d]);' % (n, base, n)); return L
    raise Unsupported(mn)

def movem(parts, sz, a):
    L = []
    def reglist(s):
        regs = []
        for part in s.split('/'):
            if '-' in part:
                x, y = part.split('-'); kx, nx = reg(x); ky, ny = reg(y)
                for i in range(nx, ny + 1): regs.append((kx, i))
            else: regs.append(reg(part))
        return sorted(regs, key=lambda r: (r[0] != 'd', r[1]))     # d0..d7, a0..a7
    to_mem = not re.fullmatch(r'[da0-7/\-]+', parts[1].replace(' ', '').replace('sp', 'a7')) is None
    if re.fullmatch(r'[da][0-7]([-/][da][0-7])*', parts[0].replace(' ', '')):
        regs, ea = reglist(parts[0].replace(' ', '')), EA(parts[1], sz, a); store = True
    else:
        regs, ea = reglist(parts[1].replace(' ', '')), EA(parts[0], sz, a); store = False
    ref = lambda r: 'c->%s[%d]' % (r[0], r[1])
    n = len(regs)
    if store and ea.pre:                                       # -(An): lowest address holds the first register
        an = re.search(r'c->a\[(\d)\]', ea.addr).group(1)
        L.append('uint32_t base = c->a[%s] - %d;' % (an, n * sz))
        for i, r in enumerate(regs): L.append('rt_mwr(base + %d, %d, %s);' % (i * sz, sz, ref(r)))
        L.append('c->a[%s] = base;' % an); return L
    L.append('uint32_t base = %s;' % ea.addr)
    for i, r in enumerate(regs):
        if store: L.append('rt_mwr(base + %d, %d, %s);' % (i * sz, sz, ref(r)))
        else: L.append('%s = rt_sext(rt_mrd(base + %d, %d), %d);' % (ref(r), i * sz, sz, sz))
    if ea.post:
        an = re.search(r'c->a\[(\d)\]', ea.addr).group(1)
        L.append('c->a[%s] = base + %d;' % (an, n * sz))
    return L

# ---- routines that suspend the task that called them ----
_fn_cache, _yields = {}, {}
def fn_of(e):
    if e not in _fn_cache: _fn_cache[e] = Fn(e)
    return _fn_cache[e]

def yields(e, seen=()):
    """e is the yield routine, or reaches it by a jump or branch (not a call)"""
    if YIELD is None or e not in INS or e in EXITS: return False
    if e in _yields: return _yields[e]
    if e in seen: return False
    f = fn_of(e)
    r = e == YIELD or YIELD in f.body or any(yields(t, seen + (e,)) for t in f.tails)
    _yields[e] = r
    return r

_saves = {}
def saves(e, seen=()):
    """e pops its caller's return address into memory (`move.l (a7)+,<memory>`, directly or by a
    jump to such code): a continuation the game jumps to later, so the instruction after every
    call to e is an entry. This is how `$9C14`, `$9630`-`$9654`, `$9F1E` and `$A3BE` are filled."""
    if e not in INS or e in EXITS: return False
    if e in _saves: return _saves[e]
    if e in seen: return False
    f = fn_of(e)
    r = any(INS[a][1] == 'move.l' and re.fullmatch(r'\(a7\)\+, (?![da][0-7]$).+', INS[a][2]) for a in f.body) \
        or any(saves(t, seen + (e,)) for t in f.tails)
    _saves[e] = r
    return r

# ---- the closure of the roots, minus hand ports, plus resume points ----
UNSUPPORTED = []
# the tracer's hints name code reached only through computed jumps (analysis/hints.json "entries"
# and "tables"): all of it may be jumped to, so all of it is translated
HINTS = json.load(open(os.path.join(os.path.dirname(MAP), '..', 'analysis', 'hints.json')))
HINT_ROOTS = [int(e, 16) for e in HINTS.get('entries', [])] + sorted(set().union(*HINT_BASES.values()) if HINT_BASES else set())
HINT_ROOTS += [x for t in HINTS.get('tables', []) for x in m['tables'].get('%06X' % int(t['addr'], 16), [])]
fns, todo = {}, list(ROOTS) + sorted(RESTART) + [x for x in HINT_ROOTS if x in INS]
RESUME = set()
CONTINUE = set()
while todo:
    e = todo.pop()
    if e in fns or e in HAND or (e in EXITS and e not in RESTART) or e in DEFER: continue
    if e not in INS: print('warning: %06X is not traced code' % e); continue
    f = fn_of(e); fns[e] = f
    todo += [x for x in f.calls if x not in fns and x not in HAND and x not in EXITS]
    for site, tgt, ret in f.sites:
        if yields(tgt): RESUME.add(ret); todo.append(ret)
        elif saves(tgt): CONTINUE.add(ret); todo.append(ret)
    # code addresses it stores (a task's exit handler, a continuation): called through a pointer later
    todo += [p for p, s in STORED_BY.items() if s & f.body]

import contextlib, io

@contextlib.contextmanager
def changed_only(path):
    """Write the file only if its contents change, so that make does not recompile the (large)
    generated code when a change to port/*.c leaves the translation as it was."""
    buf = io.StringIO()
    yield buf
    text = buf.getvalue()
    try:
        with open(path, newline='') as old:
            if old.read() == text: return
    except FileNotFoundError:
        pass
    with open(path, 'w', newline='\n') as f: f.write(text)

os.makedirs(OUT, exist_ok=True)
with changed_only(os.path.join(OUT, 'gen.c')) as f:
    f.write('/* Generated by tools/m68k2c.py from the ROM: scaffolding, do not edit or commit. */\n')
    f.write('#include "m68k_rt.h"\n\n')
    for e in sorted(HAND): f.write('void %s(Cpu *c);\n' % HAND[e])
    for e in sorted(fns): f.write('void g_%06X(Cpu *c);\n' % e)
    for e in sorted(fns):
        f.write('\nvoid g_%06X(Cpu *c)\n{\n' % e)
        f.write('\n'.join(translate(fns[e])))
        f.write('\n}\n')
with changed_only(os.path.join(OUT, 'gen_registry.c')) as f:
    f.write('/* Generated by tools/m68k2c.py: the translated routines. Do not edit or commit. */\n')
    f.write('#include <stdio.h>\n#include <stdlib.h>\n#include "m68k_rt.h"\n\n')
    for e in sorted(HAND): f.write('void %s(Cpu *c);\n' % HAND[e])
    for e in sorted(fns): f.write('void g_%06X(Cpu *c);\n' % e)
    f.write('\nconst PortRoutine gen_routines[] = {\n')
    for e in sorted(fns):
        end = max(fns[e].body) + INS[max(fns[e].body)][0]
        f.write('    {0x%06X, 0x%06X, "g_%06X", g_%06X, 0x%X},\n' % (e, end, e, e, CONTRACTS.get(e, 0)))
    f.write('};\nconst int gen_routine_count = %d;\n\n' % len(fns))
    f.write('/* where generated code stops (translate.json "exits"): a check also ends there */\n')
    f.write('const uint32_t gen_exits[] = {%s0};\nconst int gen_exit_count = %d;\n\n'
            % (''.join('0x%06X, ' % e for e in sorted(EXITS)), len(EXITS)))
    f.write('int rt_dispatch(Cpu *c, uint32_t t)\n{\n    switch (t & 0xFFFFFF) {\n')
    for e in sorted(fns):
        if e not in RESTART: f.write('    case 0x%06X: g_%06X(c); return 1;\n' % (e, e))
    for e in sorted(EXITS): f.write('    case 0x%06X: c->ret = 0x%06X; return 1;   /* exit */\n' % (e, e))
    for e in sorted(HAND): f.write('    case 0x%06X: c->ret = 0; %s(c); rt_after_hand(c); return 1;\n' % (e, HAND[e]))
    f.write('    }\n    return 0;\n}\n\n')
    f.write('void rt_unknown(uint32_t from, uint32_t target)\n{\n'
            '    fprintf(stderr, "generated code at %06X: no translation for %06X\\n", (unsigned)from, (unsigned)target);\n'
            '    abort();\n}\n')
# each routine's own instructions, for tools/coverage.py: a routine's address range can span
# other routines (a tail jump back past a neighbour), which are not part of it
with changed_only(os.path.join(OUT, 'bodies.json')) as f:
    json.dump({'g_%06X' % e: ['%06X' % a for a in sorted(fns[e].body)] for e in sorted(fns)}, f, indent=0)
total = sum(len(f.body) for f in fns.values())
print('translated %d routines, %d instructions (shared code counted per routine) into %s' % (len(fns), total, OUT))
if RESUME: print('  %d task resume points' % len(RESUME))
if CONTINUE: print('  %d continuation points (returns saved in RAM)' % len(CONTINUE))
for u in UNSUPPORTED: print('  unsupported at %06X: %s %s (%s)' % u)
