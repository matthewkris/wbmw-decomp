import sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
d = open(sys.argv[1],'rb').read()
start=int(sys.argv[2],16); n=int(sys.argv[3]) if len(sys.argv)>3 else 40
md=Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
pc=start
for _ in range(n):
    ins=next(md.disasm(d[pc:pc+16],pc,1),None)
    if ins is None:
        print(f"{pc:06X}: dc.w ${d[pc]<<8|d[pc+1]:04X}"); pc+=2; continue
    print(f"{pc:06X}: {d[pc:pc+ins.size].hex():20s} {ins.mnemonic} {ins.op_str}")
    pc+=ins.size
