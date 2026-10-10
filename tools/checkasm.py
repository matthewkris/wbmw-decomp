"""Assemble the generated source, compare it line by line with the ROM, and record every
instruction that does not reassemble identically in raw.json so mkasm.py emits it as dc.w.

Usage: checkasm.py ROM work/src/wbmw.asm work/src/lines.json raw.json VASM
Exit status 0 when the source has no errors and every mapped line matches; it does not check
the final image (the Makefile does that with the SHA-1).
"""
import sys, json, re, subprocess

rom, asm, linesf, rawf, vasm = sys.argv[1:6]
d = open(rom, 'rb').read()
L = json.load(open(linesf))['lines']
lst = asm[:-4] + '.lst'
p = subprocess.run([vasm, '-Fbin', '-m68000', '-no-opt', '-maxerrors=0', '-nowarn=2028',
                    '-L', lst, '-Llo', '-o', asm[:-4] + '.bin', asm], capture_output=True, text=True)
errs = {int(x) for x in re.findall(r'(?:error|warning) \d+ in line (\d+)', p.stdout + p.stderr)}

# listing lines: "00:ADDRESS HEXBYTES <tab> LINE: source"
got = {}
import os
if os.path.exists(lst) and 'aborting' in p.stdout + p.stderr: os.remove(lst)
for ln in (open(lst, encoding='latin1') if os.path.exists(lst) else []):
    mm = re.match(r'00:([0-9A-F]{8}) ([0-9A-F]*)\s*\t\s*(\d+):', ln)
    if mm: got[int(mm.group(3))] = (int(mm.group(1), 16), bytes.fromhex(mm.group(2)))

try: raw = set(json.load(open(rawf)))
except FileNotFoundError: raw = set()
new, bad_data, shifted = [], [], None
for n, (addr, size, isins) in L.items():
    n = int(n)
    if n not in got and n not in errs: continue
    a, b = got.get(n, (None, b''))
    want = d[addr:addr + min(size, len(b))]
    ok = n not in errs and b == want and len(b) > 0
    if a is not None and a != addr and shifted is None: shifted = (n, addr, a)
    if not ok:
        if isins and '%06X' % addr not in raw: new.append('%06X' % addr)
        elif not isins: bad_data.append((n, addr))
raw |= set(new)
json.dump(sorted(raw), open(rawf, 'w'), indent=0)
print('vasm messages on %d lines; %d new raw instructions (%d total); %d data lines differ'
      % (len(errs), len(new), len(raw), len(bad_data)))
if shifted: print('first address shift at line %d: wanted %06X, got %06X' % shifted)
for n, addr in bad_data[:10]: print('  data mismatch line %d at %06X' % (n, addr))
if p.returncode and not errs: print(p.stdout[-2000:], p.stderr[-2000:])
sys.exit(1 if (errs or new or bad_data or shifted) else 0)
