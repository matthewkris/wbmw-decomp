"""Emit a labelled listing from the tracer's map: listing.py ROM map.json out.lst"""
import sys, json, struct
d = open(sys.argv[1],'rb').read(); m = json.load(open(sys.argv[2])); N = len(d)
k = bytes.fromhex(m['kind']); ins = m['insn']
subs = {int(a,16) for a in m['calls']}; labs = {int(a,16) for a in m['jumps']}
desc = {int(a,16): t for a,t in m['descriptors'].items()}
orph = set(m['orphans']); ptr = {int(a,16) for a in m['ptr_entries']}
END = 0x1D000
with open(sys.argv[3],'w') as f:
    pc = 0x200
    while pc < END:
        if k[pc] == 1:
            sz, mn, ops = ins['%06X'%pc]
            if pc in subs: f.write('\nsub_%06X:%s\n' % (pc, '    ; unreferenced' if pc in orph else ''))
            elif pc in ptr: f.write('\nptr_%06X:    ; entered through a stored pointer\n' % pc)
            elif pc in orph: f.write('\norphan_%06X:    ; unreferenced\n' % pc)
            elif pc in labs: f.write('loc_%06X:\n' % pc)
            f.write('    %-10s %-36s ; %06X  %s\n' % (mn, ops, pc, d[pc:pc+sz].hex()))
            pc += sz
        else:
            j = pc; kk = k[pc]
            while j < END and k[j] == kk and (j == pc or j not in desc) and j - pc < 16: j += 1
            if pc in desc: f.write('\ntask_%s_%06X:\n' % (desc[pc].replace('_','x'), pc))
            f.write('    dc.b %-41s ; %06X  %s\n' % (','.join('$%02X'%b for b in d[pc:j]), pc, 'table' if kk == 3 else 'data'))
            pc = j
print('wrote', sys.argv[3])
