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
* Prices: each thing's price in gold (`prices`, $1D92), by its number; its name is phrase 1.N.
"""
import struct, re

RECORDS, WORDS = 0x1276C, 0x12794                   # $12762's targets: a record of 6 bytes; the damage word
DISPATCH = 0x12762
DEFENCE = [128, 64, 48, 40, 32, 28, 24, 20, 18, 16, 12, 10, 8, 7, 6, 6]   # $4972

class BalanceError(Exception): pass

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
            e = enemies.get(t)
            if e: m.update(first_seen='area %d room %d' % (e['area'], e['room']), picture='sprites/enemies/enemy_%s.png' % t)
            monsters.append(m)
    p0, n = int(pr['address'], 16), int(pr['length']) // 4
    prices = [dict(thing=k, name=names.get(k, ''), price=struct.unpack_from('>I', d, p0 + 4 * k)[0]) for k in range(n)]
    return dict(about='The game\'s balance by name (docs/MODDING.md, "Balance"): the pack writes these over '
                      'tables/code_tables.json. health: a multiple of 32; defence: 0-15 (0 takes double damage, '
                      '1 normal, higher less); damage: what touching it costs you.',
                monsters=monsters, prices=prices)

def apply(B, tables, pieces):
    """balance.json B written over the code tables' pieces [(address, bytes)] (a new list)"""
    kf, pr = _table(tables, 'kind_fighting'), _table(tables, 'prices')
    out = {a: bytearray(b) for a, b in pieces}
    base, p0 = int(kf['address'], 16), int(pr['address'], 16)
    if base not in out or p0 not in out: raise BalanceError('the code tables lack kind_fighting or prices')
    fight, price = out[base], out[p0]
    sw = lambda a: struct.unpack_from('>h', fight, a - base)[0]
    for m in B.get('monsters', []):
        if not re.fullmatch(r'[0-9A-Fa-f]{4}', m.get('type', '')): raise BalanceError('a monster without a type CCKK: %s' % m)
        cat, kk = int(m['type'][:2], 16), int(m['type'][2:], 16)
        o = sw(base + cat)
        if o <= 0: raise BalanceError('monster %s: its category has no fighting records' % m['type'])
        def num(k, lo, hi):
            v = m[k]
            if not isinstance(v, int) or not lo <= v <= hi: raise BalanceError('monster %s: %s must be %d to %d' % (m['type'], k, lo, hi))
            return v
        if 'health' in m:
            a = o + kk * 3
            h = num('health', 0, 255 * 32)
            if h % 32: raise BalanceError('monster %s: health %d is not a multiple of 32 (%d or %d?)' % (m['type'], h, h // 32 * 32, h // 32 * 32 + 32))
            fight[a] = h // 32
            fight[a + 1] = num('defence', 0, 15)
            fight[a + 2:a + 4] = struct.pack('>H', num('damage', 0, 65535))
            fight[a + 4:a + 6] = struct.pack('>H', num('flags', 0, 65535))
        else:
            a = o + kk
            fight[a:a + 2] = struct.pack('>H', num('damage', 0, 65535))
        if a + 2 > len(fight): raise BalanceError('monster %s is past the table' % m['type'])
    for p in B.get('prices', []):
        k, v = p.get('thing'), p.get('price')
        if not isinstance(k, int) or not 0 <= k < len(price) // 4: raise BalanceError('a price for thing %r: no such thing' % k)
        if not isinstance(v, int) or not 0 <= v < 1 << 32: raise BalanceError('the price of %s must be 0 or more' % p.get('name', k))
        price[4 * k:4 * k + 4] = struct.pack('>I', v)
    return [(a, bytes(b)) for a, b in out.items()]
