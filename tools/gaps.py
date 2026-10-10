import json,sys,struct
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_000
d=open(sys.argv[1],'rb').read(); m=json.load(open(sys.argv[2])); k=bytes.fromhex(m['kind']); CE=0x1D000
md=Cs(CS_ARCH_M68K, CS_MODE_M68K_000)
be16=lambda o: struct.unpack_from('>H',d,o)[0]
i=0x200; gaps=[]
while i<CE:
    if k[i]==0:
        j=i
        while j<CE and k[j]==0: j+=1
        gaps.append((i,j)); i=j
    else: i+=1
def linear(a,b):
    """linear sweep: fraction of bytes that decode, count of rts"""
    pc=a+(a&1); ok=0; rts=0; n=0
    while pc<b-1:
        it=next(md.disasm(d[pc:pc+12],pc,1),None)
        if it is None or '[' in it.op_str or '*' in it.op_str or pc+it.size>b: pc+=2; continue
        ok+=it.size; n+=1
        if be16(pc)==0x4E75: rts+=1
        pc+=it.size
    return ok/(b-a), rts, n
tot=sum(b-a for a,b in gaps); print('gaps',len(gaps),'bytes',tot)
minsz=int(sys.argv[3]) if len(sys.argv)>3 else 64
for a,b in gaps:
    if b-a<minsz: continue
    f,rts,n=linear(a,b)
    prev=be16(a-2) if a>=2 else 0
    print('%06X-%06X %5d  decodes %.2f  rts %3d  rts/100B %.1f  prevword %04X'%(a,b,b-a,f,rts,100*rts/(b-a),prev))
