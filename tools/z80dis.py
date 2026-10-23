"""A Z80 disassembler and code/data tracer, for the sound driver (docs/SOUND.md).

Usage: z80dis.py BIN [--base ADDR] [--entry ADDR ...] [--out FILE]

decode(mem, pc) decodes one instruction from the documented Z80 set and the common undocumented
ones (IXH/IXL, SLL), by the opcode's fields (x, y, z, p, q, as in the usual decoding tables):
its length, its text and its flow (fall-through, jump and call targets, returns). trace() follows
the flow from the entry points (recursive descent, as tools/trace.py does for the 68000) and
marks code; a listing marks the rest as data.
"""
import sys, argparse

R = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add a,', 'adc a,', 'sub ', 'sbc a,', 'and ', 'xor ', 'or ', 'cp ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']
IM = ['0', '0', '1', '2', '0', '0', '1', '2']
BLI = {(4, 0): 'ldi', (4, 1): 'cpi', (4, 2): 'ini', (4, 3): 'outi',
       (5, 0): 'ldd', (5, 1): 'cpd', (5, 2): 'ind', (5, 3): 'outd',
       (6, 0): 'ldir', (6, 1): 'cpir', (6, 2): 'inir', (6, 3): 'otir',
       (7, 0): 'lddr', (7, 1): 'cpdr', (7, 2): 'indr', (7, 3): 'otdr'}

class Insn:
    __slots__ = ('pc', 'size', 'text', 'jumps', 'calls', 'stop', 'cond')
    def __init__(self, pc):
        self.pc, self.size, self.text, self.jumps, self.calls, self.stop, self.cond = pc, 0, '', [], [], False, False

def decode(mem, pc, base=0):
    """the instruction at pc (an address; mem[pc - base] its first byte)"""
    i = Insn(pc)
    p = pc - base
    b = lambda k: mem[p + k] if 0 <= p + k < len(mem) else 0
    w = lambda k: b(k) | b(k + 1) << 8
    s8 = lambda v: v - 256 if v & 0x80 else v
    n = 0
    idx = None                                       # 'ix' / 'iy' after DD / FD
    while b(n) in (0xDD, 0xFD) and n < 4:
        idx = 'ix' if b(n) == 0xDD else 'iy'; n += 1
    op = b(n); n += 1
    x, y, z = op >> 6, op >> 3 & 7, op & 7
    pp, q = y >> 1, y & 1
    disp = None

    def reg(k, hl_ok=True):
        """register k, with (hl)/h/l replaced by (ix+d)/ixh/ixl after a prefix"""
        nonlocal n, disp
        if idx is None: return R[k]
        if k == 6:
            if disp is None: disp = s8(b(n)); n += 1
            return '(%s%+d)' % (idx, disp)
        if hl_ok and k in (4, 5): return idx + ('h' if k == 4 else 'l')
        return R[k]
    def rp(k, tab=RP): return idx if (idx and tab[k] == 'hl') else tab[k]

    if op == 0xCB:                                   # CB: rotations and bits
        if idx is not None:
            disp = s8(b(n)); n += 1
            op2 = b(n); n += 1
            m = '(%s%+d)' % (idx, disp)
        else:
            op2 = b(n); n += 1
            m = None
        x2, y2, z2 = op2 >> 6, op2 >> 3 & 7, op2 & 7
        t = m if m else R[z2]
        if x2 == 0: i.text = '%s %s' % (ROT[y2], t)
        elif x2 == 1: i.text = 'bit %d,%s' % (y2, t)
        elif x2 == 2: i.text = 'res %d,%s' % (y2, t)
        else: i.text = 'set %d,%s' % (y2, t)
        if m and z2 != 6 and x2 != 1: i.text += ',' + R[z2]
    elif op == 0xED:                                 # ED: the extended set
        op2 = b(n); n += 1
        x2, y2, z2 = op2 >> 6, op2 >> 3 & 7, op2 & 7
        p2, q2 = y2 >> 1, y2 & 1
        if x2 == 1:
            if z2 == 0: i.text = 'in (c)' if y2 == 6 else 'in %s,(c)' % R[y2]
            elif z2 == 1: i.text = 'out (c),0' if y2 == 6 else 'out (c),%s' % R[y2]
            elif z2 == 2: i.text = '%s hl,%s' % ('adc' if q2 else 'sbc', RP[p2])
            elif z2 == 3:
                a = w(n); n += 2
                i.text = 'ld %s,($%04x)' % (RP[p2], a) if q2 else 'ld ($%04x),%s' % (a, RP[p2])
            elif z2 == 4: i.text = 'neg'
            elif z2 == 5: i.text = 'reti' if y2 == 1 else 'retn'; i.stop = True
            elif z2 == 6: i.text = 'im %s' % IM[y2]
            else: i.text = ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld', 'nop', 'nop'][y2]
        elif x2 == 2 and (y2, z2) in BLI: i.text = BLI[(y2, z2)]
        else: i.text = 'db $ed,$%02x' % op2
    elif x == 0:
        if z == 0:
            if y == 0: i.text = 'nop'
            elif y == 1: i.text = "ex af,af'"
            else:
                d = s8(b(n)); n += 1
                t = (pc + n + d) & 0xFFFF
                if y == 2: i.text = 'djnz $%04x' % t; i.jumps = [t]; i.cond = True
                elif y == 3: i.text = 'jr $%04x' % t; i.jumps = [t]; i.stop = True
                else: i.text = 'jr %s,$%04x' % (CC[y - 4], t); i.jumps = [t]; i.cond = True
        elif z == 1:
            if q == 0: v = w(n); n += 2; i.text = 'ld %s,$%04x' % (rp(pp), v)
            else: i.text = 'add %s,%s' % (rp(2), rp(pp))
        elif z == 2:
            if q == 0:
                if pp == 0: i.text = 'ld (bc),a'
                elif pp == 1: i.text = 'ld (de),a'
                elif pp == 2: a = w(n); n += 2; i.text = 'ld ($%04x),%s' % (a, rp(2))
                else: a = w(n); n += 2; i.text = 'ld ($%04x),a' % a
            else:
                if pp == 0: i.text = 'ld a,(bc)'
                elif pp == 1: i.text = 'ld a,(de)'
                elif pp == 2: a = w(n); n += 2; i.text = 'ld %s,($%04x)' % (rp(2), a)
                else: a = w(n); n += 2; i.text = 'ld a,($%04x)' % a
        elif z == 3: i.text = '%s %s' % ('dec' if q else 'inc', rp(pp))
        elif z == 4: i.text = 'inc %s' % reg(y)
        elif z == 5: i.text = 'dec %s' % reg(y)
        elif z == 6:
            t = reg(y); v = b(n); n += 1
            i.text = 'ld %s,$%02x' % (t, v)
        else: i.text = ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf', 'ccf'][y]
    elif x == 1:
        if z == 6 and y == 6: i.text = 'halt'
        else:
            # ld r,r': with an index prefix, (ix+d) replaces (hl) and the other side keeps h/l
            if y == 6: i.text = 'ld %s,%s' % (reg(6), R[z])
            elif z == 6: i.text = 'ld %s,%s' % (R[y], reg(6))
            else: i.text = 'ld %s,%s' % (reg(y), reg(z))
    elif x == 2: i.text = ALU[y] + reg(z)
    else:
        if z == 0: i.text = 'ret %s' % CC[y]; i.cond = True
        elif z == 1:
            if q == 0: i.text = 'pop %s' % rp(pp, RP2)
            elif pp == 0: i.text = 'ret'; i.stop = True
            elif pp == 1: i.text = 'exx'
            elif pp == 2: i.text = 'jp (%s)' % rp(2); i.stop = True
            else: i.text = 'ld sp,%s' % rp(2)
        elif z == 2: t = w(n); n += 2; i.text = 'jp %s,$%04x' % (CC[y], t); i.jumps = [t]; i.cond = True
        elif z == 3:
            if y == 0: t = w(n); n += 2; i.text = 'jp $%04x' % t; i.jumps = [t]; i.stop = True
            elif y == 2: v = b(n); n += 1; i.text = 'out ($%02x),a' % v
            elif y == 3: v = b(n); n += 1; i.text = 'in a,($%02x)' % v
            elif y == 4: i.text = 'ex (sp),%s' % rp(2)
            elif y == 5: i.text = 'ex de,hl'
            elif y == 6: i.text = 'di'
            else: i.text = 'ei'
        elif z == 4: t = w(n); n += 2; i.text = 'call %s,$%04x' % (CC[y], t); i.calls = [t]
        elif z == 5:
            if q == 0: i.text = 'push %s' % rp(pp, RP2)
            elif pp == 0: t = w(n); n += 2; i.text = 'call $%04x' % t; i.calls = [t]
            else: i.text = 'db $%02x' % op                 # (prefixes, handled above)
        elif z == 6: v = b(n); n += 1; i.text = ALU[y] + '$%02x' % v
        else: i.text = 'rst $%02x' % (y * 8); i.calls = [y * 8]
    i.size = n
    return i

def trace(mem, base, entries, stops=()):
    """code reached from entries: {pc: Insn}. stops: addresses of routines that do not return
    to their caller (the caller's fall-through is not followed)"""
    code, todo = {}, list(entries)
    while todo:
        pc = todo.pop()
        while base <= pc < base + len(mem) and pc not in code:
            i = decode(mem, pc, base)
            code[pc] = i
            todo += i.jumps + i.calls
            if i.stop or any(t in stops for t in i.calls): break
            pc += i.size
    return code

def listing(mem, base, code, labels=None):
    labels = labels or {}
    targets = set()
    for i in code.values(): targets.update(i.jumps + i.calls)
    out, pc, end = [], base, base + len(mem)
    while pc < end:
        if pc in labels or pc in targets: out.append('%s:' % labels.get(pc, 'l_%04x' % pc))
        if pc in code:
            i = code[pc]
            raw = ' '.join('%02x' % mem[pc - base + k] for k in range(i.size))
            out.append('    %-28s ; %04x  %s' % (i.text, pc, raw))
            pc += i.size
        else:
            q = pc
            while q < end and q not in code and q - pc < 16 and not (q > pc and (q in targets or q in labels)): q += 1
            out.append('    db %-25s ; %04x  data' % (','.join('$%02x' % v for v in mem[pc - base:q - base]), pc))
            pc = q
    return '\n'.join(out)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('bin')
    ap.add_argument('--base', default='0'); ap.add_argument('--entry', nargs='*', default=['0'])
    ap.add_argument('--out')
    A = ap.parse_args()
    mem = open(A.bin, 'rb').read()
    base = int(A.base, 16)
    code = trace(mem, base, [int(e, 16) for e in A.entry])
    text = listing(mem, base, code)
    n = sum(i.size for i in code.values())
    print('%d instructions, %d bytes of code of %d' % (len(code), n, len(mem)), file=sys.stderr)
    if A.out: open(A.out, 'w').write(text + '\n')
    else: print(text)

if __name__ == '__main__':
    main()
