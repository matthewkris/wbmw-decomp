"""The game's balance by name (docs/MODDING.md, "Balance"): tables/balance.json, written by
extract.py and read back by the pack over the code tables it comes from (tables/code_tables.json:
`kind_fighting`, `prices`).

* Monsters: each monster type's fighting record (`kind_fighting`, read by $1274A when it appears
  and by $492E for its health bar): the table starts with a word for each category of type (the
  high byte of a type `CCKK`), the offset of its part; how a category's part is read is the
  dispatch at $12762 (port/enemygfx.c, kind_fighting): category 0 has a record of 6 bytes at
  KK x 3 (health / 32, defence, contact damage, a word of flags), categories 2 and 8 a word at KK,
  the contact damage only.
* Defence: the player's hits on it are scaled by the table at $4972 over 64, at this index
  (port/enemylib.c, scale_damage): 0 takes 2x, 1 as it is, 2 3/4, ... 15 6/64.
* Speeds: each type's movement record (`kind_movement`, $126D4): x and y speed limits and
  accelerations (and a monster's two spawn bytes, a townsperson's speed and palette); kinds can
  share a record.
* Drops: what category 0's monsters leave (`defeat_records`, $482C): four each, by chance.
* Prices: each thing's price in gold (`prices`, $1D92), by its number; its name is phrase 1.N.
"""
import struct, re
import codetables

RECORDS, WORDS = 0x1276C, 0x12794                   # $12762's targets: a record of 6 bytes; the damage word
DISPATCH = 0x12762
DEFENCE = [128, 64, 48, 40, 32, 28, 24, 20, 18, 16, 12, 10, 8, 7, 6, 6]   # $4972
# kind_movement ($126D4): a word a category, the offset of its kinds' words, each the offset of its
# record; $126FC says how a category's records are read: a monster's (2 spawn bytes, then speeds and
# accelerations), speeds and accelerations, a townsperson's (a speed, a palette)
MOVE_DISPATCH, MOVE_MONSTER, MOVE_SPEEDS, MOVE_PERSON = 0x126FC, 0x12706, 0x1270E, 0x1273A
# drops ($482C): category 0's kind KK has four at defeat_records + KK x 2, taken with chances
# 3/4, 3/16, 3/64, 1/64; a drop is an item kind (port/items.c), gold paying an amount by the
# masks and bases at $F226 ($F1EC)
GOLD_TABLE = 0xF226
CHANCES = ['3/4', '3/16', '3/64', '1/64']

class BalanceError(Exception): pass

def code_bytes(tables, a, n):
    """n bytes at a, from the code tables that hold them"""
    out = bytearray(n)
    for t in tables:
        ta = int(t['address'], 16); b = codetables.encode(t['rows'], t['element'])
        lo, hi = max(a, ta), min(a + n, ta + len(b))
        if lo < hi: out[lo - a:hi - a] = b[lo - ta:hi - ta]
    return bytes(out)

def drop_names(tables):
    """{code: name} of the drops"""
    g = code_bytes(tables, GOLD_TABLE, 48)
    w = [struct.unpack_from('>H', g, 2 * i)[0] for i in range(24)]
    names = {}
    for c in range(0, 0x18, 2):
        lo, hi = w[c + 1], w[c + 1] + w[c]
        names[c] = 'gold %d-%d%s' % (lo, hi, ', rolling' if c < 0x0C else '')
    names.update({0x18: 'heart', 0x1A: 'full health', 0x1C: 'magic charge', 0xFF: 'nothing'})
    return names

def drop_name(code, names):
    return names.get(code) or ('big item $%02X' % code if code & 0x80 else '$%02X' % code)

def drop_code(name, names, who):
    for c, n in names.items():
        if n == name: return c
    m = re.fullmatch(r'(?:big item )?\$([0-9A-Fa-f]{2})', name or '')
    if m: return int(m.group(1), 16)
    raise BalanceError('%s: no drop is called "%s" (%s, or "$XX")' % (who, name, ', '.join(sorted(set(names.values())))))

def movement(tables):
    """[(type, record address, how)] of kind_movement, read from the code tables"""
    kt = _table(tables, 'kind_movement')
    base, size = int(kt['address'], 16), int(kt['length'])
    m = codetables.encode(kt['rows'], kt['element'])
    sw = lambda a: struct.unpack_from('>h', m, a - base)[0]
    disp = code_bytes(tables, MOVE_DISPATCH, 10)
    n = sw(base) // 2
    offs = [sw(base + 2 * k) for k in range(n)]
    starts = sorted(o for o in offs if o > 0)
    # the kinds' words run from each category's offset to the next's; the last's to the first record
    recs = [sw(base + i) for a, b in zip(starts, starts[1:]) for i in range(a, b, 2)]
    ends = dict(zip(starts, starts[1:] + [min(recs)]))
    out = []
    for k, o in enumerate(offs):
        if o <= 0: continue
        how = MOVE_DISPATCH + struct.unpack_from('>h', disp, 2 * k)[0]
        for i in range((ends[o] - o) // 2):
            out.append(('%02X%02X' % (2 * k, 2 * i), base + sw(base + o + 2 * i), how))
    return out

def _table(tables, name):
    t = next((t for t in tables if t['name'] == name), None)
    if t is None: raise BalanceError('code_tables.json has no table %s' % name)
    return t

def _parts(d, base, size):
    """the categories' parts of kind_fighting: [(category, kind ('records'/'words'), start, end)]"""
    sw = lambda a: struct.unpack_from('>h', d, a)[0]
    n = sw(base) // 2                               # the first part starts after the offsets
    offs = [sw(base + 2 * k) for k in range(n)]
    out = []
    for k, o in enumerate(offs):
        if not o: continue
        how = DISPATCH + sw(DISPATCH + 2 * k)
        end = min([x for x in offs if x > o] + [size])
        if how == RECORDS: out.append((2 * k, 'records', o, end))
        elif how == WORDS: out.append((2 * k, 'words', o, end))
    return out

def extract(d, tables, enemies, names):
    """balance.json's content from the ROM d; enemies: enemies.json; names: {n: thing n's name}"""
    kf, pr = _table(tables, 'kind_fighting'), _table(tables, 'prices')
    base, size = int(kf['address'], 16), int(kf['length'])
    monsters = []
    for cat, kind, o, end in _parts(d, base, size):
        step = 6 if kind == 'records' else 2
        for i in range((end - o) // step):
            kk = 2 * i
            a = base + o + (kk * 3 if kind == 'records' else kk)
            t = '%02X%02X' % (cat, kk)
            m = dict(type=t)
            if kind == 'records':
                m.update(health=d[a] * 32, defence=d[a + 1], damage=struct.unpack_from('>H', d, a + 2)[0],
                         flags=struct.unpack_from('>H', d, a + 4)[0])
            else: m.update(damage=struct.unpack_from('>H', d, a)[0])
            monsters.append(m)
    by = {m['type']: m for m in monsters}
    s8 = lambda a: d[a] - 256 if d[a] & 0x80 else d[a]
    moves = movement(tables)
    share = {}
    for t, rec, how in moves: share.setdefault(rec, []).append(t)
    for t, rec, how in moves:                        # their speeds (kind_movement)
        m = by.setdefault(t, dict(type=t))
        if how == MOVE_MONSTER:
            m.update(speed=[d[rec + 2], d[rec + 3]], acceleration=[s8(rec + 4), s8(rec + 5)], spawn=[d[rec], d[rec + 1]])
        elif how == MOVE_SPEEDS: m.update(speed=[d[rec], d[rec + 1]], acceleration=[s8(rec + 2), s8(rec + 3)])
        elif how == MOVE_PERSON: m.update(speed=d[rec], palette=d[rec + 1])
        else: continue
        if len(share[rec]) > 1: m['moves_like'] = [x for x in share[rec] if x != t]
    dr = _table(tables, 'defeat_records')
    d0, dn = int(dr['address'], 16), int(dr['length'])
    dnames = drop_names(tables)
    for t, m in by.items():                          # their drops (defeat_records): category 0's
        kk = int(t[2:], 16)
        if t[:2] == '00' and 2 * kk + 4 <= dn: m['drops'] = [drop_name(d[d0 + 2 * kk + i], dnames) for i in range(4)]
    for t, m in by.items():
        e = enemies.get(t)
        if e: m.update(first_seen='area %d room %d' % (e['area'], e['room']), picture='sprites/enemies/enemy_%s.png' % t)
    monsters = [by[t] for t in sorted(by)]
    p0, n = int(pr['address'], 16), int(pr['length']) // 4
    prices = [dict(thing=k, name=names.get(k, ''), price=struct.unpack_from('>I', d, p0 + 4 * k)[0]) for k in range(n)]
    return dict(about='The game\'s balance by name (docs/MODDING.md, "Balance"): the pack writes these over '
                      'tables/code_tables.json. health: a multiple of 32; defence: 0-15 (0 takes double damage, '
                      '1 normal, higher less); damage: what touching it costs you; speed, acceleration: x and y; '
                      'drops: four, taken with chances %s; moves_like: kinds sharing the movement record, '
                      'which must keep the same values.' % ', '.join(CHANCES),
                drops=sorted(set(dnames.values())), monsters=monsters, prices=prices)

def apply(B, tables, pieces):
    """balance.json B written over the code tables' pieces [(address, bytes)] (a new list)"""
    out = {a: bytearray(b) for a, b in pieces}
    def table_bytes(name):
        a = int(_table(tables, name)['address'], 16)
        if a not in out: raise BalanceError('the code tables lack %s' % name)
        return a, out[a]
    fbase, fight = table_bytes('kind_fighting')
    mbase, move = table_bytes('kind_movement')
    dbase, drops = table_bytes('defeat_records')
    p0, price = table_bytes('prices')
    sw = lambda a: struct.unpack_from('>h', fight, a)[0]
    records = {t: (rec, how) for t, rec, how in movement(tables)}
    dnames = drop_names(tables)
    written = {}                                    # a movement record's byte -> (the type that set it, its value)
    for m in B.get('monsters', []):
        t = m.get('type', '')
        if not re.fullmatch(r'[0-9A-Fa-f]{4}', t): raise BalanceError('a monster without a type CCKK: %s' % m)
        t = t.upper(); cat, kk = int(t[:2], 16), int(t[2:], 16)
        def num(k, lo, hi, v=None):
            v = m[k] if v is None else v
            if not isinstance(v, int) or not lo <= v <= hi: raise BalanceError('monster %s: %s must be %d to %d' % (t, k, lo, hi))
            return v
        if 'health' in m or 'damage' in m:          # its fighting record (kind_fighting)
            o = sw(cat) if 2 * cat < len(fight) else 0
            if o <= 0: raise BalanceError('monster %s: its category has no fighting records' % t)
            if 'health' in m:
                a = o + kk * 3
                if a + 6 > len(fight): raise BalanceError('monster %s is past the fighting records' % t)
                h = num('health', 0, 255 * 32)
                if h % 32: raise BalanceError('monster %s: health %d is not a multiple of 32 (%d or %d?)' % (t, h, h // 32 * 32, h // 32 * 32 + 32))
                fight[a] = h // 32
                fight[a + 1] = num('defence', 0, 15)
                fight[a + 2:a + 4] = struct.pack('>H', num('damage', 0, 65535))
                fight[a + 4:a + 6] = struct.pack('>H', num('flags', 0, 65535))
            else:
                a = o + kk
                if a + 2 > len(fight): raise BalanceError('monster %s is past the fighting records' % t)
                fight[a:a + 2] = struct.pack('>H', num('damage', 0, 65535))
        if 'speed' in m:                            # its movement record (kind_movement)
            if t not in records: raise BalanceError('monster %s has no movement record' % t)
            rec, how = records[t]
            if how == MOVE_PERSON: b = bytes([num('speed', 0, 255), num('palette', 0, 255)])
            else:
                sp, ac = m['speed'], m['acceleration']
                if not (isinstance(sp, list) and isinstance(ac, list) and len(sp) == 2 and len(ac) == 2):
                    raise BalanceError('monster %s: speed and acceleration are [x, y]' % t)
                b = bytes([num('speed', 0, 255, sp[0]), num('speed', 0, 255, sp[1]),
                           num('acceleration', -128, 127, ac[0]) & 255, num('acceleration', -128, 127, ac[1]) & 255])
                if how == MOVE_MONSTER:
                    s = m.get('spawn', [move[rec - mbase], move[rec - mbase + 1]])
                    b = bytes([num('spawn', 0, 255, s[0]), num('spawn', 0, 255, s[1])]) + b
            for i, v in enumerate(b):               # kinds can share a record (of other categories, read otherwise)
                w = written.get(rec + i)
                if w and w[1] != v:
                    raise BalanceError('monsters %s and %s share their movement record (moves_like): it cannot hold '
                                       'both their values' % (w[0], t))
                written[rec + i] = (t, v)
            move[rec - mbase:rec - mbase + len(b)] = b
        if 'drops' in m:                            # its drops (defeat_records)
            ds = m['drops']
            if not isinstance(ds, list) or len(ds) != 4: raise BalanceError('monster %s: drops are four (chances %s)' % (t, ', '.join(CHANCES)))
            a = 2 * kk
            if cat != 0 or a + 4 > len(drops): raise BalanceError('monster %s has no drops (category 0 only)' % t)
            drops[a:a + 4] = bytes(drop_code(x, dnames, 'monster %s' % t) for x in ds)
    for p in B.get('prices', []):
        k, v = p.get('thing'), p.get('price')
        if not isinstance(k, int) or not 0 <= k < len(price) // 4: raise BalanceError('a price for thing %r: no such thing' % k)
        if not isinstance(v, int) or not 0 <= v < 1 << 32: raise BalanceError('the price of %s must be 0 or more' % p.get('name', k))
        price[4 * k:4 * k + 4] = struct.pack('>I', v)
    return [(a, bytes(b)) for a, b in out.items()]
