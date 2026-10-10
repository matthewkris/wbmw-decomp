"""Everything a routine reaches: instructions by control flow, and the routines it calls.

Usage: reach.py work/map.json ADDR [ADDR ...] [--stop ADDR,ADDR,...]

Follows branches and fall-through from each entry, and calls (jsr/bsr), printing each routine
reached with its instruction count. --stop lists routines not to descend into (already ported,
say). Indirect jumps are reported, not followed. Used to size a piece of the port.
"""
import sys, json, re

def main():
    args = sys.argv[2:]
    stop = set()
    if '--stop' in args:
        i = args.index('--stop'); stop = {int(x, 16) for x in args[i + 1].split(',') if x}; args = args[:i] + args[i + 2:]
    m = json.load(open(sys.argv[1]))
    ins = {int(a, 16): v for a, v in m['insn'].items()}
    calls = {int(a, 16) for a in m['calls']}
    entries = [int(a, 16) for a in args]
    routines, todo, indirect = {}, list(entries), []
    while todo:
        r = todo.pop()
        if r in routines or r in stop: continue
        seen, work = set(), [r]
        while work:
            a = work.pop()
            while a in ins and a not in seen:
                seen.add(a)
                sz, mn, ops = ins[a]
                t = re.match(r'\$([0-9a-f]+)', ops)
                tgt = int(t.group(1), 16) if t and not ops.endswith(')') else None
                if mn in ('rts', 'rte'): break
                if mn.startswith('jmp'):
                    if tgt is not None and tgt not in calls: work.append(tgt)
                    elif tgt is not None: todo.append(tgt)
                    else: indirect.append(a)
                    break
                if mn.startswith('jsr') or mn.startswith('bsr'):
                    if tgt is not None: todo.append(tgt)
                    else: indirect.append(a)
                elif mn.startswith('bra'):
                    work.append(tgt); break
                elif mn[0] == 'b' and mn[:4] not in ('bset', 'bclr', 'bchg', 'btst') or mn.startswith('db'):
                    target = re.search(r'\$([0-9a-f]+)$', ops)
                    if target: work.append(int(target.group(1), 16))
                a += sz
        routines[r] = seen
    total = 0
    for r in sorted(routines):
        n = len(routines[r]); total += n
        print('%06X %4d instructions' % (r, n))
    print('%d routines, %d instructions (shared code counted once per routine)' % (len(routines), total))
    if indirect: print('indirect calls or jumps at', ' '.join('%06X' % a for a in sorted(set(indirect))))

if __name__ == '__main__':
    main()
