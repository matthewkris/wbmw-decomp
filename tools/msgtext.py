"""The messages as text to edit (docs/MODDING.md, "Text"): text/messages.txt is written from the
game's messages and read back by the pack, at any length.

The format. Each message starts with a line `--- message N (window W)` (W: the window it opens in,
text/windows.json) and ends with its last {END}; blank lines after it are ignored, and lines
starting with # before the first message are notes. In a message:

* text is text: ASCII, and the font's other glyphs as text.py names them (Ⓐ, Ⅰ, kana; {xNN} for a
  glyph with no name, and for { and });
* a new line is a new line (the code NL); a new line followed by {INDENT} is the code NLINDENT;
* {SPACES n} is the code that moves on n places (not n space characters, which are text);
* {END} ends what the window shows; a message goes on after it where a jump leads (one new line
  after {END} is part of it, so that what follows starts on its own line);
* the other codes and commands are {NAME arguments}, as text.py names them ({MORE}, {SPEED 6},
  {IF_FLAG 54 >2.1}, ...);
* a jump's argument is a label, `>M.K`, and the label is put where it leads, `{@M.K}`: M is the
  message that holds it, K counts the labels in that message. A jump can lead into another
  message. `>$XXXXXX` leads to that address (text outside the messages).

The shared phrases (text/phrases.json) are written out in the text, but for those that jump within
themselves (a shop's whole conversation), which stay {PHRASE T.N}: the phrase, kept where it is. A message whose text is as it
was keeps its bytes exactly; a changed one is encoded again, the phrases put back wherever its
text matches one (a phrase takes 2 bytes).

Laying them out (build): every message in its old order, from where the first one was; if they no
longer fit before the phrases' tables, the message table and all the messages move past the end of
the ROM's data (EXTENSION), and the data directory's pointer to the table with them (the game reads
it there, port/message.c, $276C). A message's place is a signed word from the table: they must
stay within 32 KB of it.
"""
import re, struct
import text, assetfmt

EXTENSION = 0xC0000                                 # past the ROM's 786,432 bytes: the pack's image grows

CODE_NAMES = {name: b for b, (name, n) in text.CODES.items()}
CMD_NAMES = {name: c for c, (name, args) in text.COMMANDS.items()}
GLYPH_BYTES = {g: b for b, g in text.GLYPHS.items()}
MARK_BYTES = {m: b for b, m in text.MARKS.items()}

class TextError(Exception): pass

# ---- tokens ----
# ('char', b) or ('char', mark, b); ('phrase', table, n); ('code', b, [values]); ('cmd', c, [values]);
# ('label', name). In a symbolic token a jump's value is a label name (a str) or an address (an int
# with 'abs').

def jump_fields(tok):
    """the positions in tok's values that are jumps, and each jump word's offset in its bytes"""
    if tok[0] == 'code' and tok[1] == 6: return [(0, 1)]
    if tok[0] != 'cmd': return []
    out, off, i = [], 2, 0
    for f in text.COMMANDS.get(tok[1], text.COMMANDS[50])[1]:
        if f == 'b': off += 1; i += 1
        elif f == 'j': out.append((i, off)); off += 2; i += 1
        elif f == 'Q':
            for _ in range(text.QUIZ_JUMPS): out.append((i, off)); off += 2; i += 1
    return out

def size(tok):
    k = tok[0]
    if k == 'label': return 0
    if k == 'char': return len(tok) - 1
    if k == 'phrase': return 2
    if k == 'code': return 1 + (2 if tok[1] == 6 else len(tok[2]))
    n = 2
    for f in text.COMMANDS.get(tok[1], text.COMMANDS[50])[1]: n += {'b': 1, 'j': 2, 'Q': 2 * text.QUIZ_JUMPS}[f]
    return n

def encode(tok, at, where):
    """tok's bytes at address at; where: label name -> address"""
    vals = list(tok[2]) if tok[0] in ('code', 'cmd') else None
    for i, off in jump_fields(tok):
        v = vals[i]
        if isinstance(v, str) and v not in where: raise TextError('the label %s is not anywhere' % v)
        target = where[v] if isinstance(v, str) else v[1]
        d = target - (at + off)
        if not -32768 <= d < 32768: raise TextError('a jump to %s is too far (%d bytes)' % (v, d))
        vals[i] = d
    return assetfmt.text_encode([(tok[0], tok[1], vals)] if vals is not None else [tok])

# ---- the texts as symbolic tokens ----
# A unit is a text with a place of its own: a message, a phrase (one or more phrase numbers whose
# table entries point at it), or a fragment (text kept after the phrases that phrases jump to):
# dict(kind, keys, label, address, end, tokens).

def symbolic(units):
    """every unit's tokens with every jump as a label (`M.K` in message M, `PT.N.K` in phrase T.N,
    `FI.K` in fragment I) and the labels put in"""
    toks, addr = [], {}                             # per unit: [token]; address -> (unit, index)
    for u, unit in enumerate(units):
        a = unit['address']
        lst = [tuple(t) if t[0] not in ('code', 'cmd') else (t[0], t[1], list(t[2])) for t in unit['tokens']]
        for i, t in enumerate(lst):
            addr.setdefault(a, (u, i)); a += size(t)
        toks.append(lst)
    targets = set()
    for unit, lst in zip(units, toks):
        a = unit['address']
        for t in lst:
            for i, off in jump_fields(t): targets.add(a + off + t[2][i])
            a += size(t)
    names, per = {}, {}
    for tgt in sorted(targets):
        if tgt in addr:
            u, i = addr[tgt]; per[u] = per.get(u, 0) + 1
            names[tgt] = ('%s.%d' % (units[u]['label'], per[u]), u, i)
    out = []
    for u, (unit, lst) in enumerate(zip(units, toks)):
        a, new = unit['address'], []
        marks = {i: name for name, uu, i in names.values() if uu == u}
        for i, t in enumerate(lst):
            if i in marks: new.append(('label', marks[i]))
            if t[0] in ('code', 'cmd'):
                vals = list(t[2])
                for k, off in jump_fields(t):
                    tgt = a + off + vals[k]
                    vals[k] = names[tgt][0] if tgt in names else ('abs', tgt)
                t = (t[0], t[1], vals)
            new.append(t); a += size(t)
        out.append(new)
    return out

# ---- text ----

def _glyph(b):
    if b in (0x7B, 0x7D): return '{x%02X}' % b
    return text.char(b)

def render(toks, phrases):
    """symbolic tokens as the text of messages.txt; phrases: (table, n) -> parsed tokens"""
    out = []
    def args(vals):
        return ''.join(' ' + ('>' + v if isinstance(v, str) else '>$%06X' % v[1] if isinstance(v, tuple) else str(v))
                       for v in vals)
    def one(t, depth=0):
        k = t[0]
        if k == 'label': out.append('{@%s}' % t[1])
        elif k == 'char': out.append(_glyph(t[2]) + text.MARKS[t[1]] if len(t) == 3 else _glyph(t[1]))
        elif k == 'phrase':
            p = phrases.get((t[1], t[2]))
            if p is None: raise TextError('phrase %d.%d is not in phrases.json' % (t[1], t[2]))
            body = expand(p, phrases)
            if body is None: out.append('{PHRASE %d.%d}' % (t[1], t[2])); return   # it jumps within itself
            for q in body: one(q, depth + 1)
        elif k == 'code':
            b, vals = t[1], t[2]
            if b == 0: out.append('{END}\n')
            elif b == 2: out.append('\n')
            elif b == 9: out.append('\n{INDENT}')
            else: out.append('{%s%s}' % (text.CODES[b][0], args(vals)))
        else:
            name = text.COMMANDS[t[1]][0] if t[1] in text.COMMANDS else 'CMD %d' % t[1]
            out.append('{%s%s}' % (name, args(t[2])))
    for t in toks: one(t)
    return ''.join(out)

TAG = re.compile(r'\{([^{}]*)\}')

def parse(body, what):
    """a text back to symbolic tokens (characters, codes, commands, {PHRASE}s); what: its name for errors"""
    toks, i, n = [], 0, len(body)
    def fail(why): raise TextError('%s: %s (at "%s")' % (what, why, body[max(0, i - 12):i + 12].replace('\n', '|')))
    while i < n:
        ch = body[i]
        if ch == '\n':
            if body.startswith('{INDENT}', i + 1): toks.append(('code', 9, [])); i += 9
            else: toks.append(('code', 2, [])); i += 1
            continue
        if ch == '{':
            m = TAG.match(body, i)
            if not m: fail('a { with no }')
            parts = m.group(1).split()
            i = m.end()
            if not parts: fail('an empty tag')
            name, rest = parts[0], parts[1:]
            if name.startswith('@'): toks.append(('label', name[1:])); continue
            if re.fullmatch(r'x[0-9A-Fa-f]{2}', name) and not rest: toks.append(('char', int(name[1:], 16))); continue
            if name == 'PHRASE' and len(rest) == 1 and re.fullmatch(r'\d\.\d+', rest[0]):
                k, num = rest[0].split('.'); toks.append(('phrase', int(k), int(num))); continue
            vals = []
            for r in rest:
                if r.startswith('>$'): vals.append(('abs', int(r[2:], 16)))
                elif r.startswith('>'): vals.append(r[1:])
                else:
                    try: vals.append(int(r))
                    except ValueError: fail('"%s" is not a number' % r)
            if name == 'END':
                toks.append(('code', 0, []))
                if body.startswith('\n', i): i += 1
            elif name == 'INDENT' and not vals: fail('{INDENT} without a number is only the code after a new line')
            elif name == 'CMD' and vals: toks.append(('cmd', vals[0], vals[1:]))
            elif name in CODE_NAMES:
                b = CODE_NAMES[name]
                want = 1 if b == 6 else text.CODES[b][1]
                if len(vals) != want: fail('{%s} takes %d values' % (name, want))
                toks.append(('code', b, vals))
            elif name in CMD_NAMES:
                c = CMD_NAMES[name]
                want = sum({'b': 1, 'j': 1, 'Q': text.QUIZ_JUMPS}[f] for f in text.COMMANDS[c][1])
                if len(vals) != want: fail('{%s} takes %d values' % (name, want))
                toks.append(('cmd', c, vals))
            else: fail('no code or command is called %s' % name)
            continue
        if ch == '}': fail('a } with no {')
        if i + 1 < n and body[i + 1] in MARK_BYTES:
            b = _char_byte(ch, fail); toks.append(('char', MARK_BYTES[body[i + 1]], b)); i += 2; continue
        toks.append(('char', _char_byte(ch, fail))); i += 1
    return toks

def _char_byte(ch, fail):
    if 0x20 <= ord(ch) < 0x7F: return ord(ch)
    if ch in GLYPH_BYTES: return GLYPH_BYTES[ch]
    fail('the font has no "%s"' % ch)

def _key(t):
    return (t[0], t[1], tuple(t[2])) if t[0] in ('code', 'cmd') else tuple(t)

def expand(p, phrases, depth=0):
    """a phrase's tokens as they show: up to its END, the phrases in it written out (None if it
    holds a jump, which cannot stand for text elsewhere)"""
    out = []
    for q in p:
        q = tuple(q) if q[0] not in ('code', 'cmd') else (q[0], q[1], list(q[2]))
        if q[0] == 'code' and q[1] == 0: break
        if jump_fields(q): return None
        if q[0] == 'phrase':
            inner = phrases.get((q[1], q[2]))
            sub = expand(inner, phrases, depth + 1) if inner is not None and depth < 4 else None
            if sub is None: return None
            out += sub
        else: out.append(q)
    return out

def with_phrases(toks, phrases):
    """a changed message with the phrases put back where its tokens match one written out, the
    largest first (where a phrase saves bytes: it takes 2)"""
    cands = []
    for (k, n), p in phrases.items():
        body = expand(p, phrases)
        if body and all(q[0] != 'label' for q in body) and sum(size(q) for q in body) > 2: cands.append(([_key(q) for q in body], sum(size(q) for q in body), k, n))
    cands.sort(key=lambda c: -c[1])
    first = {}
    for c in cands: first.setdefault(c[0][0], []).append(c)
    keys = [_key(t) if t[0] != 'label' else None for t in toks]
    out, i = [], 0
    while i < len(toks):
        hit = None
        for body, _, k, n in first.get(keys[i], []):
            if keys[i:i + len(body)] == body: hit = (len(body), k, n); break
        if hit: out.append(('phrase', hit[1], hit[2])); i += hit[0]
        else: out.append(toks[i]); i += 1
    return out

# ---- the units, from the extracted files ----

def units_of(msgs, phrase_list, frags, phrase_tables):
    """the messages, the phrases (one unit per text: numbers whose entries point at the same place
    share it; an entry pointing at its own table is an unused one, kept as it is) and the fragments"""
    units = [dict(kind='message', keys=[m['number']], label='%d' % m['number'], address=int(m['address'], 16),
                  end=int(m['end'], 16), tokens=m['parsed'], window=m['window']) for m in msgs]
    by_addr = {}
    for p in sorted(phrase_list, key=lambda p: (p['table'], p['number'])):
        a = int(p['address'], 16)
        if a == phrase_tables[p['table']]: continue
        if a in by_addr: by_addr[a]['keys'].append((p['table'], p['number'])); continue
        by_addr[a] = dict(kind='phrase', keys=[(p['table'], p['number'])], label='P%d.%d' % (p['table'], p['number']),
                          address=a, end=int(p['end'], 16), tokens=p['parsed'])
    units += sorted(by_addr.values(), key=lambda u: u['address'])
    units += [dict(kind='fragment', keys=[i], label='F%d' % i, address=int(f['address'], 16), end=int(f['end'], 16),
                   tokens=f['parsed']) for i, f in enumerate(frags)]
    return units

def unit_title(u):
    if u['kind'] == 'message': return 'message %d (window %d)' % (u['keys'][0], u['window'])
    if u['kind'] == 'phrase': return 'phrase ' + ' '.join('%d.%d' % k for k in u['keys'])
    return 'fragment %d' % u['keys'][0]

# ---- the files ----

HEADER = re.compile(r'^--- (message (\d+) \(window (\d+)[^)]*\)|phrase ((?:\d\.\d+ ?)+)|fragment (\d+))\s*$')

NOTES = {'messages': """# The game's messages, to edit (docs/MODDING.md, "Text"). The build reads this file back.
# {NAME ...} is a code or command; {@M.K} marks where a jump >M.K leads; {SPACES n} moves on n places;
# the shared phrases are written out, but {PHRASE T.N} for those with jumps of their own (phrases.txt).
# Keep every message number; a message can be any length.
""", 'phrases': """# The shared phrases (messages and names use them by number: a phrase changed here changes them
# all) and the fragments their jumps lead to, to edit (docs/MODDING.md, "Text"). The build reads this
# file back. Things' names are phrases of table 1. Same format as messages.txt; any length.
"""}

def write(path, which, units, phrases):
    """messages.txt (which 'messages') or phrases.txt ('phrases'); phrases: (table, n) -> parsed"""
    sym = symbolic(units)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(NOTES[which])
        for u, toks in zip(units, sym):
            if (u['kind'] == 'message') == (which == 'messages'):
                f.write('\n--- %s\n%s\n' % (unit_title(u), render(toks, phrases)))

def read(path):
    """{('message', n) | ('phrase', (t, n)) | ('fragment', i): (window or None, body)}"""
    out, cur, order = {}, None, []
    for line in open(path, encoding='utf-8').read().split('\n'):
        h = HEADER.match(line)
        if h:
            if h.group(2) is not None: key, w = ('message', int(h.group(2))), int(h.group(3))
            elif h.group(4) is not None:
                k = h.group(4).split()[0].split('.'); key, w = ('phrase', (int(k[0]), int(k[1]))), None
            else: key, w = ('fragment', int(h.group(5))), None
            if key in out: raise TextError('%s is in %s twice' % (line[4:], path))
            cur = [w, []]; out[key] = cur; continue
        if cur is None:
            if line.strip() and not line.startswith('#'): raise TextError('text before the first header in %s: "%s"' % (path, line[:40]))
            continue
        cur[1].append(line)
    for key, (w, lines) in out.items():
        body = '\n'.join(lines)
        k = body.rfind('{END}')
        if k >= 0 and not body[k + 5:].strip('\n'): body = body[:k + 5] + '\n'
        else: body = body.rstrip('\n')
        out[key] = (w, body)
    return out

# ---- laying them out ----

def build(msgs, phrase_list, frags, msg_table, phrase_tables, files):
    """every text, from messages.txt and phrases.txt (files: their paths, None for one there is
    not: then as the .json files have them). Returns (pieces by name, the message table's address,
    the four phrase tables' addresses, the units changed)."""
    units = units_of(msgs, phrase_list, frags, phrase_tables)
    orig = {(p['table'], p['number']): p['parsed'] for p in phrase_list}
    sym = symbolic(units)
    changed = []
    texts = {}
    for f in files:
        if f: texts.update(read(f))
    def key(u): return (u['kind'], u['keys'][0])
    if texts:                                       # each file whole: messages.txt every message, phrases.txt the rest
        has = {'message': files[0] is not None, 'phrase': files[1] is not None, 'fragment': files[1] is not None}
        missing = [unit_title(u) for u in units if has[u['kind']] and key(u) not in texts]
        if missing: raise TextError('missing from the text files: %s' % ', '.join(missing[:8]))
        known = {key(u) for u in units}
        extra = [k for k in texts if k not in known]
        if extra: raise TextError('no such text in the game: %s' % ', '.join('%s %s' % k for k in extra[:8]))
    # the phrases first: a changed message puts back the phrases as they are now
    current = dict(orig)
    for kind in ('phrase', 'fragment', 'message'):
        for u, unit in enumerate(units):
            if unit['kind'] != kind or key(unit) not in texts: continue
            w, body = texts[key(unit)]
            if kind == 'message': unit['window'] = w
            if body == render(sym[u], orig): continue
            toks = parse(body, unit_title(unit))
            sym[u] = with_phrases(toks, current) if kind == 'message' else toks
            changed.append(unit_title(unit))
            if kind == 'phrase':
                for k in unit['keys']: current[k] = sym[u]

    def lay(us, first, where):
        """units us one after the other from first: their places; their labels into where"""
        at, places = first, {}
        for u in us:
            places[u] = at
            for t in sym[u]:
                if t[0] == 'label':
                    if t[1] in where: raise TextError('the label %s is put twice' % t[1])
                    where[t[1]] = at
                at += size(t)
        return places, at

    msg_us = sorted((u for u in range(len(units)) if units[u]['kind'] == 'message'), key=lambda u: units[u]['address'])
    phr_us = [u for u in range(len(units)) if units[u]['kind'] != 'message']
    phrases_changed = any(c.startswith(('phrase', 'fragment')) for c in changed)
    start, end = units[msg_us[0]]['address'], max(units[u]['end'] for u in msg_us)
    total = sum(size(t) for u in msg_us for t in sym[u])
    where = {}
    if total <= end - start: mtable, (mplaces, mend) = msg_table, lay(msg_us, start, where)
    else: mtable = EXTENSION; mplaces, mend = lay(msg_us, EXTENSION + 3 * len(msg_us), where)
    counts = [1 + max(p['number'] for p in phrase_list if p['table'] == k) for k in range(4)]
    if not phrases_changed:                         # as they were: each where it was
        tables, pplaces = list(phrase_tables), {}
        for u in phr_us: pplaces.update(lay([u], units[u]['address'], where)[0])
    else:                                           # all of them, and their tables, past the messages
        at = max(EXTENSION, mend if mtable == EXTENSION else 0)
        at += at & 1
        tables = []
        for k in range(4): tables.append(at); at += 2 * counts[k]
        pplaces, _ = lay(phr_us, at, where)
    for u, a in list(mplaces.items()):
        if not -32768 <= a - mtable < 32768: raise TextError('the messages must stay within 32 KB of their table')
    pieces = {'messages': [], 'message table': [], 'phrases': [], 'text fragments': [], 'phrase tables': []}
    for u, a in list(mplaces.items()) + list(pplaces.items()):
        b = bytearray()
        for t in sym[u]:
            if t[0] != 'label': b += encode(t, a + len(b), where)
        pieces['messages' if units[u]['kind'] == 'message' else 'phrases' if units[u]['kind'] == 'phrase'
               else 'text fragments'].append((a, bytes(b)))
    windows = {units[u]['keys'][0]: units[u]['window'] for u in msg_us}
    pieces['message table'].append((mtable, b''.join(bytes([windows[n]]) + struct.pack('>h', mplaces[u] - mtable)
                                                      for n, u in sorted((units[u]['keys'][0], u) for u in msg_us))))
    entry = {}
    for u in phr_us:
        if units[u]['kind'] == 'phrase':
            for k in units[u]['keys']: entry[k] = pplaces[u]
    for k in range(4):
        offs = []
        for n in range(counts[k]):
            o = entry[(k, n)] - tables[k] if (k, n) in entry else 0
            if not -32768 <= o < 32768: raise TextError('phrase %d.%d is too far from its table' % (k, n))
            offs.append(o)
        pieces['phrase tables'].append((tables[k], b''.join(struct.pack('>h', o) for o in offs)))
    return pieces, mtable, tables, changed
