"""Decode the game's messages and phrases to readable text.

Usage: text.py ROM [--message N] [--phrases] [--all]

The tables (the data directory at $1CC00; the interpreter is $13BC, port/message.c):
* messages: the long at $1CC14 points at 3-byte entries, a window number and an unsigned word
  offset from the table to the message's text ($276C);
* windows: the long at $1CC18 points at 4-byte entries: column, row, width, lines;
* phrases: the long at $1CC10 points at four longs, each a table of signed word offsets from
  that table to a phrase ($15A0's codes $0C-$0F, step_into).

A text is bytes: $10 and up are characters (ASCII for the Latin letters, the font's tile
numbers; $FE and $FF put a mark over the next one); $0C-$0F and a byte insert phrase (byte) of
table (code & 3); $00-$0B are codes, $0B followed by a command number and its arguments. The
codes and commands are rendered as {TAGS}; phrases are expanded in the text and kept as numbers
in the parsed form.
"""
import sys, struct, argparse

DIR = 0x1CC00
CODES = {0: ('END', 0), 1: ('ATTR', 1), 2: ('NL', 0), 3: ('SPEED', 1), 4: ('SPACES', 1), 5: ('MORE', 0),
         6: ('JUMP', 'w'), 7: ('NOSKIP', 0), 8: ('SKIP', 0), 9: ('NLINDENT', 0), 10: ('INDENT', 1)}
# command number: (name, argument bytes; 'j' a jump word); from port/message.c's command(). A
# choice's word comes after its two columns: the jump to the second answer's text ($13BC)
COMMANDS = {
    0: ('PART_END', ''), 1: ('THING_PHRASE', 'b'), 2: ('CHOICE', 'bbj'), 3: ('IF_GOLD_SHORT', 'j'),
    4: ('PRICE', 'b'), 5: ('AMOUNT', ''), 6: ('THING_NAME', ''), 7: ('PAY', ''), 8: ('GIVE', 'b'),
    9: ('SPEAKER_GONE', ''), 10: ('THING', 'b'), 11: ('CALL_1C494', ''), 12: ('END_RESTART', ''),
    13: ('END_FADE', ''), 14: ('SET_965E', 'b'), 15: ('AMOUNT_TABLE', 'b'), 16: ('NO_GOLD', ''),
    17: ('IF_FLAG_BIT', 'bj'), 18: ('IF_FLAG', 'bj'), 19: ('SET_FLAG_BIT', 'b'), 20: ('SET_FLAG', 'b'),
    21: ('CALL_1C330', ''), 22: ('SET_8CC3', 'b'), 23: ('IF_OWNS', 'bj'), 24: ('PAUSE', 'b'),
    25: ('SOUND', 'b'), 26: ('OCARINA_TUNE', 'bjj'), 27: ('PHRASE_SET', 'bb'), 28: ('PHRASE_SAVED', ''),
    29: ('CALL_F906', ''), 30: ('CURSOR', 'bb'), 31: ('QUIZ_ANSWER_TEXT', 'b'), 32: ('CLEAR', ''),
    33: ('QUIZ_WAIT', ''), 34: ('QUIZ_QUESTION', ''), 35: ('QUIZ_ORDER', ''), 36: ('QUIZ_JUMPS', 'Q'),
    37: ('AMOUNT_SET', 'bbbb'), 38: ('GAIN', ''), 39: ('BEST_OWNED_JUMPS', 'jjjjj'), 40: ('INN', ''),
    41: ('TAKE', ''), 42: ('IF_959C', 'bj'), 43: ('IF_MAX_HEARTS', 'j'), 44: ('IF_ACTIONS_FULL', 'j'),
    45: ('IF_HEALTH_FULL', 'j'), 46: ('CALL_8E9C', 'bb'), 47: ('IF_9F32', 'j'), 48: ('HEART_UP', ''),
    49: ('CALL_76C4', ''), 50: ('LINES', 'b')}
QUIZ_JUMPS = 6      # command 36: a jump for each number of right answers, 0 to 5

def be16(d, a): return d[a] << 8 | d[a + 1]
def be32(d, a): return struct.unpack_from('>I', d, a)[0]
def sw(d, a): return struct.unpack_from('>h', d, a)[0]

def messages_table(d):
    """[(window, address)] of every message: the count is where the first text begins"""
    base = be32(d, DIR + 0x14) & 0xFFFFFF
    out, first = [], None
    i = 0
    while first is None or base + 3 * i < first:
        w, off = d[base + 3 * i], be16(d, base + 3 * i + 1)
        out.append((w, base + off))
        first = base + off if first is None else min(first, base + off)
        i += 1
    return out

def window(d, n):
    a = (be32(d, DIR + 0x18) & 0xFFFFFF) + 4 * n
    return dict(col=d[a], row=d[a + 1], width=d[a + 2], lines=d[a + 3])

def phrase_tables(d):
    """four lists of phrase addresses (each table's count: where its first phrase begins)"""
    tb = be32(d, DIR + 0x10) & 0xFFFFFF
    out = []
    for k in range(4):
        t = be32(d, tb + 4 * k) & 0xFFFFFF
        lst, first, j = [], None, 0
        while first is None or t + 2 * j < first:
            a = t + sw(d, t + 2 * j)
            lst.append(a)
            if a > t: first = a if first is None else min(first, a)
            j += 1
            if j > 512: break
        out.append(lst)
    return out

# The font's glyphs outside ASCII (tiles $10-$1F and $A0-$FD, read from the font through the
# graphics slots at $96BC): the quiz's letters, marks, Roman numerals, then kana from the
# Japanese release. $7F-$9F are pieces of pictures and $C0-$C5, $D0 icons: left as {xNN}.
GLYPHS = dict(zip(range(0x10, 0x20), 'ⒶⒷⒸ▲◪▔▏✕✦ⅠⅡⅢⅤⅩⅬⅯ'))
GLYPHS.update(zip(range(0xA0, 0xC0), 'たちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわん゛゜'))
GLYPHS.update(zip(range(0xC6, 0xD0), 'ヲァィゥェォャュョッ'))
GLYPHS.update(zip(range(0xD1, 0xFE), 'アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン'))
MARKS = {0xFE: '゛', 0xFF: '゜'}                   # $FE, $FF: the mark over the next character

def char(b):
    return chr(b) if 0x20 <= b < 0x7F else GLYPHS.get(b, '{x%02X}' % b)

def decode(d, a, end=None, phrases=None, depth=0, stop_at_end=True, jumps=None):
    """the text at a as (rendered string, parsed list, end address); up to the END code (or end).
    jumps, if given, gets every jump's target (its word's place plus its value)"""
    out, parsed = [], []
    while end is None or a < end:
        b = d[a]; a += 1
        if b >= 0x10:
            if b >= 0xFE:
                mark, b = b, d[a]; a += 1
                out.append(char(b) + MARKS[mark]); parsed.append(('char', mark, b))
            else:
                out.append(char(b)); parsed.append(('char', b))
        elif b >= 0x0C:
            n = d[a]; a += 1
            parsed.append(('phrase', b & 3, n))
            if phrases and depth < 4 and n < len(phrases[b & 3]):
                out.append(decode(d, phrases[b & 3][n], phrases=phrases, depth=depth + 1)[0])
            else:
                out.append('{PHRASE%d.%d}' % (b & 3, n))
        elif b == 11:
            c = d[a]; a += 1
            name, args = COMMANDS.get(c, COMMANDS[50])
            vals = []
            for f in args:
                if f == 'b': vals.append(d[a]); a += 1
                elif f == 'j':
                    vals.append(sw(d, a))
                    if jumps is not None: jumps.append(a + vals[-1])
                    a += 2
                elif f == 'Q':
                    for _ in range(QUIZ_JUMPS):
                        vals.append(sw(d, a))
                        if jumps is not None: jumps.append(a + vals[-1])
                        a += 2
            out.append('{%s%s}' % (name, ' ' + ' '.join(str(v) for v in vals) if vals else ''))
            parsed.append(('cmd', c, vals))
        else:
            name, n = CODES[b]
            vals = []
            if n == 'w':
                vals.append(sw(d, a))
                if jumps is not None: jumps.append(a + vals[-1])
                a += 2
            else:
                for _ in range(n): vals.append(d[a]); a += 1
            if b == 2: out.append('\n')
            elif b == 9: out.append('\n{INDENT}')
            elif b == 4: out.append(' ' * vals[0])
            elif b == 0:
                parsed.append(('code', 0, []))
                if stop_at_end: return ''.join(out), parsed, a
                out.append('{END}\n'); continue
            else: out.append('{%s%s}' % (name, ' ' + ' '.join(str(v) for v in vals) if vals else ''))
            parsed.append(('code', b, vals))
    return ''.join(out), parsed, a

def all_messages(d):
    """every message decoded, each up to the next message's text (jumps can reach past an END);
    the last one up to the next table (the phrases' tables follow the messages)"""
    tab = messages_table(d)
    phrases = phrase_tables(d)
    tb = be32(d, DIR + 0x10) & 0xFFFFFF
    tables = [tb] + [be32(d, tb + 4 * k) & 0xFFFFFF for k in range(4)] + [be32(d, DIR + 0x18) & 0xFFFFFF]
    starts = sorted(set(a for _, a in tab))
    nxt = dict(zip(starts, starts[1:] + [min([t for t in tables if t > starts[-1]] or [None])]))
    out, jumps = [], []
    for n, (w, a) in enumerate(tab):
        text, parsed, stop = decode(d, a, nxt[a], phrases, stop_at_end=nxt[a] is None, jumps=jumps)
        out.append(dict(number=n, window=w, address=a, end=stop, text=text, parsed=parsed))
    return out, phrases

def fragments(d, msgs, phrases):
    """the texts jumps lead to outside every message's and phrase's bytes (some messages jump to
    text kept after the phrases), each to its END, and those their own jumps lead to"""
    spans = [(m['address'], m['end']) for m in msgs] + [(a, decode(d, a)[2]) for t in phrases for a in t]
    inside = lambda t: any(a <= t < e for a, e in spans)
    todo, out = [], {}
    for m in msgs: decode(d, m['address'], m['end'], phrases, stop_at_end=False, jumps=todo)
    for t in phrases:
        for a in t: decode(d, a, jumps=todo)
    while todo:
        t = todo.pop()
        if t in out or inside(t) or not 0 < t < len(d): continue
        found = []
        text, parsed, end = decode(d, t, phrases=phrases, jumps=found)
        out[t] = dict(address=t, end=end, text=text, parsed=parsed)
        spans.append((t, end)); todo += found
    return [out[k] for k in sorted(out)]

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('--message', type=int)
    ap.add_argument('--phrases', action='store_true'); ap.add_argument('--all', action='store_true')
    A = ap.parse_args()
    d = open(A.rom, 'rb').read()
    msgs, phrases = all_messages(d)
    if A.phrases:
        for k, lst in enumerate(phrases):
            for j, a in enumerate(lst): print('%d.%d %06X %r' % (k, j, a, decode(d, a)[0]))
    if A.message is not None:
        m = msgs[A.message]; print('message %d, window %d %s, at %06X' % (m['number'], m['window'], window(d, m['window']), m['address'])); print(m['text'])
    if A.all:
        for m in msgs: print('--- message %d (window %d, %06X)\n%s' % (m['number'], m['window'], m['address'], m['text']))
    if not (A.phrases or A.message is not None or A.all):
        print('%d messages, phrase tables of %s' % (len(msgs), [len(t) for t in phrases]))
