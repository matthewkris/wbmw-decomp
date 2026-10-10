import sys, struct
d = open(sys.argv[1],'rb').read()
print("size", len(d), hex(len(d)))
be32 = lambda o: struct.unpack('>I', d[o:o+4])[0]
be16 = lambda o: struct.unpack('>H', d[o:o+2])[0]
names = ["SSP","Reset","BusErr","AddrErr","IllInstr","DivZero","CHK","TRAPV","Priv","Trace","LineA","LineF"]
for i,n in enumerate(names): print(f"  vec {i:2d} {n:9s} {be32(i*4):08X}")
for i in range(12,64):
    v=be32(i*4)
    tag={25:"IRQ1",26:"IRQ2 EXT",27:"IRQ3",28:"IRQ4 HBLANK",29:"IRQ5",30:"IRQ6 VBLANK",31:"IRQ7"}.get(i,"")
    if tag or i>=32: print(f"  vec {i:2d} {tag:12s} {v:08X}")
f = lambda a,b: d[a:b].decode('ascii','replace')
print("console  :", repr(f(0x100,0x110)))
print("copyright:", repr(f(0x110,0x120)))
print("dom title:", repr(f(0x120,0x150)))
print("ovs title:", repr(f(0x150,0x180)))
print("serial   :", repr(f(0x180,0x18E)))
print("checksum : %04X" % be16(0x18E))
print("io       :", repr(f(0x190,0x1A0)))
print("rom range: %08X-%08X" % (be32(0x1A0), be32(0x1A4)))
print("ram range: %08X-%08X" % (be32(0x1A8), be32(0x1AC)))
print("sram     :", repr(d[0x1B0:0x1BC]), "%08X-%08X" % (be32(0x1B4), be32(0x1B8)))
print("modem    :", repr(f(0x1BC,0x1C8)))
print("memo     :", repr(f(0x1C8,0x1F0)))
print("region   :", repr(f(0x1F0,0x200)))
s=0
for o in range(0x200,len(d)&~1,2): s=(s+be16(o))&0xFFFF
print("computed checksum: %04X" % s, "MATCH" if s==be16(0x18E) else "MISMATCH")
