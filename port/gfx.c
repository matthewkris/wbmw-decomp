/* DecodeGfxBlock ($6BC4): d0 = graphics block number, a1 = destination. Decodes one entry of
 * the graphics table at $41000 (32 tiles, 1,024 bytes): empty, raw, per-tile packed, or
 * Huffman-coded LZ77 (formats in docs/NOTES.md, Graphics table).
 *
 * This is a line-by-line transcription, not a clean decoder (tools/gfx.py is the clean one): the
 * shadow check compares everything, so it reproduces the Huffman tree the original builds in RAM
 * at $FF8F0A, the bitplane rows at $FF8DE8, the tree-building stack it keeps in the output
 * buffer, the return address its internal bsr leaves below the stack pointer, and every
 * register and condition code at each of its exits. Labels are the original's addresses. */
#include "port.h"

#define GFX_TABLE 0x41000u
#define MB(r, v)  do { uint32_t v_ = (v) & 0xFF; (r) = set_b((r), v_); cc = tst_b(cc, v_); } while (0)
#define MW(r, v)  do { uint32_t v_ = (v) & 0xFFFF; (r) = set_w((r), v_); cc = tst_w(cc, v_); } while (0)
#define MOVEQ(r, v) do { (r) = (uint32_t)(int32_t)(int8_t)(v); cc = tst_l(cc, (r)); } while (0)
#define SEXT(w)   ((uint32_t)(int32_t)(int16_t)((w) & 0xFFFF))
#define DBRA(r)   ((r) = set_w((r), ((r) - 1) & 0xFFFF), ((r) & 0xFFFF) != 0xFFFF)

typedef struct {
    uint32_t d0, d1, d2, d3, d4, d5, d6, d7, a0, a1, a2, a3, sp0;
    uint16_t cc;
} R;

static void wb(uint32_t a, uint32_t v) { wr8(a, (uint8_t)v); }

/* $6E4E: one leaf value from the bit stream: 0 + 8 bits, or 1 + 5 bits as $100 + n (in d7) */
static void leaf(R *r)
{
    uint16_t cc = r->cc;
    if (!DBRA(r->d1)) { MB(r->d0, rd8(r->a0++)); MOVEQ(r->d1, 7); }       /* $6E4E */
    r->d0 = set_b(r->d0, add_b(&cc, r->d0, r->d0));                        /* $6E56 add.b d0,d0 */
    if (cc & CC_C) {                                                         /* bcs $6E76 */
        MW(r->d7, 0x100);
        cc = tst_b(cc, r->d1);
        if (!(r->d1 & 0xFF)) {
            MB(r->d0, rd8(r->a0++));
            MB(r->d7, r->d0);
            r->d7 = set_b(r->d7, lsr_n(&cc, r->d7, 3, 8));
            r->d0 = set_b(r->d0, lsl_n(&cc, r->d0, 5, 8));
            MOVEQ(r->d1, 3);
            r->cc = cc; return;
        }
        MB(r->d7, r->d0);                                                   /* $6E8A */
        r->d7 = set_b(r->d7, lsr_n(&cc, r->d7, 3, 8));
        r->d1 = set_b(r->d1, sub_b(&cc, 5, r->d1));
        if (!(cc & CC_C)) { r->d0 = set_b(r->d0, lsl_n(&cc, r->d0, 5, 8)); r->cc = cc; return; }
        r->d1 = set_b(r->d1, add_b(&cc, 8, r->d1));                         /* $6E96 */
        goto merge;
    }
    MOVEQ(r->d7, 0);
    cc = tst_b(cc, r->d1);
    if (!(r->d1 & 0xFF)) { MB(r->d7, rd8(r->a0++)); r->cc = cc; return; }
    MB(r->d7, r->d0);                                                       /* $6E64 */
merge:                                                                       /* $6E66 / $6E98 */
    MB(r->d0, rd8(r->a0++));
    MB(r->d6, r->d0);
    r->d6 = set_b(r->d6, lsr_n(&cc, r->d6, r->d1 & 63, 8));
    { uint32_t v = (r->d7 | r->d6) & 0xFF; r->d7 = set_b(r->d7, v); cc = tst_b(cc, v); }   /* or.b */
    MOVEQ(r->d6, 8);
    r->d6 = set_b(r->d6, sub_b(&cc, r->d1, r->d6));
    r->d0 = set_b(r->d0, lsl_n(&cc, r->d0, r->d6 & 63, 8));
    r->cc = cc;
}

/* $6E2E: one symbol: walk the tree from the root, a bit per branch; $11F escapes to a leaf value */
static void symbol(R *r)
{
    uint16_t cc = r->cc;
    r->a2 = 0xFFFF8F0A;
    for (;;) {
        if (!DBRA(r->d1)) { MB(r->d0, rd8(r->a0++)); MOVEQ(r->d1, 7); }   /* $6E32 */
        r->d0 = set_b(r->d0, add_b(&cc, r->d0, r->d0));
        if (cc & CC_C) r->a2 += 2;
        MW(r->d7, rd16(r->a2));                                             /* $6E40 */
        if (!(r->d7 & 0x8000)) break;
        r->a2 = SEXT(r->d7);
    }
    cmp_w(&cc, 0x11F, r->d7);
    r->cc = cc;
    if ((r->d7 & 0xFFFF) == 0x11F) leaf(r);
}

static void lz(R *r)                                                         /* $6D9C */
{
    uint16_t cc = r->cc;
    r->a0 = r->d0;
    MB(r->d0, rd8(r->a0++));
    r->d0 = set_b(r->d0, add_b(&cc, r->d0, r->d0));                        /* the root bit, dropped */
    MOVEQ(r->d1, 7);
    MW(r->d3, 0x8F0A);
    r->a2 = SEXT(r->d3);
    r->a3 = r->a1;
    MOVEQ(r->d2, 0); MOVEQ(r->d4, 0);
    /* the tree, preorder: 1 = node (allocated at d3 += 4), 0 = leaf; pending nodes are kept on a
     * word stack that grows up through the output buffer (a3) */
    for (;;) {
        if (!DBRA(r->d1)) { MB(r->d0, rd8(r->a0++)); MOVEQ(r->d1, 7); }   /* $6DB0 */
        r->d0 = set_b(r->d0, add_b(&cc, r->d0, r->d0));                    /* $6DB8 */
        if (cc & CC_C) {
            r->d3 = set_w(r->d3, add_w(&cc, 4, r->d3));
            cc = tst_w(cc, r->d2);
            if (!(r->d2 & 0xFFFF)) {
                wr16(r->a3, (uint16_t)r->a2); r->a3 += 2; cc = tst_w(cc, r->a2);
                r->d4 = set_w(r->d4, add_w(&cc, 1, r->d4));
                wr16(r->a2, (uint16_t)r->d3); cc = tst_w(cc, r->d3);
            } else {
                wr16(r->a2 + 2, (uint16_t)r->d3); cc = tst_w(cc, r->d3);  /* $6DCA */
                MOVEQ(r->d2, 0);
            }
            r->a2 = SEXT(r->d3);                                           /* $6DD0 */
            continue;
        }
        r->cc = cc; wr32(r->sp0 - 4, 0x6DD6); leaf(r); cc = r->cc;        /* $6DD4 bsr */
        cc = tst_w(cc, r->d2);
        if (!(r->d2 & 0xFFFF)) {
            wr16(r->a2, (uint16_t)r->d7); cc = tst_w(cc, r->d7);
            MOVEQ(r->d2, 2);
            continue;
        }
        wr16(r->a2 + 2, (uint16_t)r->d7); cc = tst_w(cc, r->d7);          /* $6DE0 */
        r->a3 -= 2; r->a2 = SEXT(rd16(r->a3));
        if (!DBRA(r->d4)) break;
    }
    MW(r->d2, 0x3FF);
    /* LZ77 over 1,024 bytes: d2 counts down the bytes still to write */
    for (;;) {
        r->cc = cc; wr32(r->sp0 - 4, 0x6DF0); symbol(r); cc = r->cc;      /* $6DEE */
        cmp_w(&cc, 0x100, r->d7);
        if (cc & CC_C) {                                                    /* literal: $6E26 */
            wb(r->a1, r->d7); r->a1++; cc = tst_b(cc, r->d7);
            if (!DBRA(r->d2)) break;
            continue;
        }
        MW(r->d3, r->d7);
        r->d3 = set_w(r->d3, sub_w(&cc, 0xFD, r->d3));                     /* length = symbol - $FD */
        r->d2 = set_w(r->d2, sub_w(&cc, r->d3, r->d2));
        r->cc = cc; wr32(r->sp0 - 4, 0x6E00); symbol(r); cc = r->cc;
        r->d7 = set_w(r->d7, neg_w(&cc, r->d7));
        r->a2 = r->a1 - 1 + SEXT(r->d7);                                   /* distance = symbol + 1 */
        for (;;) {                                                          /* $6E12 */
            r->d3 = set_w(r->d3, sub_w(&cc, 5, r->d3));
            if (cc & CC_C) break;
            for (int k = 0; k < 5; k++) { uint32_t v = rd8(r->a2++); wb(r->a1++, v); cc = tst_b(cc, v); }
        }
        r->d3 = set_w(r->d3, add_w(&cc, 4, r->d3));
        if (!(r->d3 & 0x8000))
            do { uint32_t v = rd8(r->a2++); wb(r->a1++, v); cc = tst_b(cc, v); } while (DBRA(r->d3));
        cc = tst_w(cc, r->d2);                                              /* $6E20 */
        if (r->d2 & 0x8000) break;
    }
    r->cc = cc;
}

static const uint32_t TAB[4] = {0x6D1C, 0x6D3C, 0x6D5C, 0x6D7C};    /* nibble -> 4 pixels' bit */

static void tiles(R *r)                                                      /* $6BFC */
{
    uint16_t cc = r->cc;
    r->a0 = r->d0;
    MOVEQ(r->d7, 31);
    do {
        MB(r->d0, rd8(r->a0++));                                            /* $6C00 */
        if (!(r->d0 & 0xFF)) {                                              /* raw tile */
            MOVEQ(r->d0, 31);
            do { uint32_t v = rd8(r->a0++); wb(r->a1++, v); cc = tst_b(cc, v); } while (DBRA(r->d0));
            continue;
        }
        if (!(r->d0 & 0x80)) {                                              /* value + mask fills */
            r->d0 = ext_w(r->d0); cc = tst_w(cc, r->d0);
            r->d0 = set_b(r->d0, sub_b(&cc, 1, r->d0));
            MOVEQ(r->d3, 0);
            do {
                MB(r->d1, rd8(r->a0++));                                    /* $6C1A */
                MB(r->d2, rd8(r->a0++));
                r->d2 = set_w(r->d2, lsl_n(&cc, r->d2, 8, 16));
                MB(r->d2, rd8(r->a0++));
                r->d2 = r->d2 << 16 | r->d2 >> 16; cc = tst_l(cc, r->d2);   /* swap */
                MB(r->d2, rd8(r->a0++));
                r->d2 = set_w(r->d2, lsl_n(&cc, r->d2, 8, 16));
                MB(r->d2, rd8(r->a0++));
                r->d3 |= r->d2; cc = tst_l(cc, r->d3);
                r->a2 = r->a1;
                do {
                    r->d2 = add_l(&cc, r->d2, r->d2);                       /* $6C2E */
                    if (cc & CC_C) { wb(r->a2, r->d1); cc = tst_b(cc, r->d1); }
                    r->a2++;
                    cc = tst_l(cc, r->d2);
                } while (r->d2);
            } while (DBRA(r->d0));
            r->d3 = ~r->d3; cc = tst_l(cc, r->d3);
            r->a2 = r->a1;
            do {                                                            /* the uncovered bytes */
                r->d3 = add_l(&cc, r->d3, r->d3);                           /* $6C42 */
                if (cc & CC_C) { uint32_t v = rd8(r->a0++); wb(r->a2, v); cc = tst_b(cc, v); }
                r->a2++;
                cc = tst_l(cc, r->d3);
            } while (r->d3);
            r->a1 += 0x20;
            continue;
        }
        /* bitplanes: $6C56 */
        MB(r->d1, rd8(r->a0++));
        MOVEQ(r->d6, 3);
        r->a2 = 0xFFFF8DE8; r->a3 = r->a2;
        do {
            r->d1 = set_b(r->d1, lsr_n(&cc, r->d1, 1, 8));                 /* $6C60 */
            r->d2 = set_b(r->d2, cc & CC_C ? 0xFF : 0);                    /* scs */
            r->d1 = set_b(r->d1, lsr_n(&cc, r->d1, 1, 8));
            if (cc & CC_C) {                                                /* solid plane */
                r->d2 = ext_w(r->d2); cc = tst_w(cc, r->d2);
                r->d2 = SEXT(r->d2); cc = tst_l(cc, r->d2);
                wr32(r->a3, r->d2); r->a3 += 4; cc = tst_l(cc, r->d2);
                wr32(r->a3, r->d2); r->a3 += 4; cc = tst_l(cc, r->d2);
            } else {
                MB(r->d3, rd8(r->a0++));                                    /* $6C72 */
                MOVEQ(r->d4, 7);
                do {
                    MB(r->d5, r->d2);                                       /* $6C76 */
                    r->d3 = set_b(r->d3, lsr_n(&cc, r->d3, 1, 8));
                    if (cc & CC_C) MB(r->d5, rd8(r->a0++));
                    wb(r->a3++, r->d5); cc = tst_b(cc, r->d5);
                } while (DBRA(r->d4));
            }
            r->d0 = set_b(r->d0, lsr_n(&cc, r->d0, 1, 8));                 /* $6C84 */
            if (cc & CC_C) {                                                /* XOR delta down the rows */
                r->a3 -= 8;
                MB(r->d4, rd8(r->a3)); r->a3++;
                MOVEQ(r->d2, 6);
                do {
                    MB(r->d3, rd8(r->a3));
                    { uint32_t v = (r->d4 ^ r->d3) & 0xFF; r->d4 = set_b(r->d4, v); cc = tst_b(cc, v); }
                    wb(r->a3++, r->d4); cc = tst_b(cc, r->d4);
                } while (DBRA(r->d2));
            }
        } while (DBRA(r->d6));
        MOVEQ(r->d0, 7);
        do {                                                                /* planar to 4bpp: $6C9E */
            for (int p = 0; p < 4; p++) {
                MB(r->d2, rd8(r->a2 + (p ? 7 + 8 * (p - 1) : 0)));
                if (!p) r->a2++;
                MB(r->d3, r->d2);
                r->d3 = set_w(r->d3, lsr_n(&cc, r->d3, 3, 16));
                { uint32_t v = r->d3 & 0x1E; r->d3 = set_w(r->d3, v); cc = tst_w(cc, v); }
                uint32_t hi = rd16(TAB[p] + (r->d3 & 0xFFFF));
                if (!p) { MW(r->d1, hi); } else { uint32_t v = (r->d1 | hi) & 0xFFFF; r->d1 = set_w(r->d1, v); cc = tst_w(cc, v); }
                r->d1 = r->d1 << 16 | r->d1 >> 16; cc = tst_l(cc, r->d1);
                { uint32_t v = r->d2 & 0xF; r->d2 = set_w(r->d2, v); cc = tst_w(cc, v); }
                r->d2 = set_w(r->d2, add_w(&cc, r->d2, r->d2));
                uint32_t lo = rd16(TAB[p] + (r->d2 & 0xFFFF));
                if (!p) { MW(r->d1, lo); } else { uint32_t v = (r->d1 | lo) & 0xFFFF; r->d1 = set_w(r->d1, v); cc = tst_w(cc, v); }
                if (p < 3) { r->d1 = r->d1 << 16 | r->d1 >> 16; cc = tst_l(cc, r->d1); }
            }
            wr32(r->a1, r->d1); r->a1 += 4; cc = tst_l(cc, r->d1);
        } while (DBRA(r->d0));
    } while (DBRA(r->d7));                                                  /* $6C0C */
    r->cc = cc;
}

void DecodeGfxBlock(Cpu *c);
void SetVRAMWrite(Cpu *c);

/* rotate a word right by n (ror.w): C = the last bit rotated out (now bit 15), X kept, V clear */
static uint32_t ror_w(uint16_t *ccr, uint32_t v, unsigned n)
{
    uint32_t x = v & 0xFFFF;
    n &= 15;
    uint32_t r = n ? ((x >> n) | (x << (16 - n))) & 0xFFFF : x;
    *ccr = (uint16_t)((*ccr & CC_X) | cc_nz_w(r) | (n && (r & 0x8000) ? CC_C : 0));
    return r;
}

/* StreamGfxBlocks ($22F0): for each of the 64 VRAM slots whose wanted block ($FF8E08 + 2n)
 * differs from the loaded one (128 bytes on), decode it into $FF2D00 and copy the 1 KB to VRAM
 * at n * $400. A stack frame (link a6,#-2) holds the slot counter; a0 is saved around the
 * decode with move.w a0,-(a7). The stack writes are made in the original's order so the words
 * it leaves below SP match. */
void StreamGfxBlocks(Cpu *c)
{
    uint32_t sp0 = c->a[7], sp = sp0;
    uint16_t cc = c->ccr;
    sp -= 4; wr32(sp, c->a[6]); c->a[6] = sp; sp -= 2;                      /* link a6,#-2 */
    c->a[0] = 0xFFFF8E08;
    wr16(c->a[6] - 2, 0); cc = tst_w(cc, 0);
    do {
        c->d[0] = set_w(c->d[0], rd16(c->a[0])); c->a[0] += 2; cc = tst_w(cc, c->d[0]);   /* $22FE */
        if (c->d[0] & 0x8000) goto next;
        cmp_w(&cc, rd16(c->a[0] + 0x7E), c->d[0]);
        if (cc & CC_Z) goto next;
        wr16(c->a[0] + 0x7E, (uint16_t)c->d[0]); cc = tst_w(cc, c->d[0]);
        sp -= 2; wr16(sp, (uint16_t)c->a[0]); cc = tst_w(cc, c->a[0]);     /* move.w a0,-(a7) */
        c->a[1] = 0x00FF2D00;
        sp -= 4; wr32(sp, 0x231A); c->a[7] = sp; c->ccr = cc;               /* jsr DecodeGfxBlock */
        DecodeGfxBlock(c); cc = c->ccr; sp += 4;
        c->a[0] = SEXT(rd16(sp)); sp += 2;                                  /* movea.w (a7)+,a0 */
        c->d[0] = set_w(c->d[0], rd16(c->a[6] - 2)); cc = tst_w(cc, c->d[0]);
        c->d[0] = set_w(c->d[0], ror_w(&cc, c->d[0], 6));                  /* slot * $400 */
        sp -= 4; wr32(sp, 0x2326); c->a[7] = sp; c->ccr = cc;               /* jsr SetVRAMWrite */
        SetVRAMWrite(c); cc = c->ccr; sp += 4;
        c->a[1] = 0x00FF2D00; c->a[2] = 0x00C00000;
        c->d[0] = set_w(c->d[0], 0xFF); cc = tst_w(cc, 0xFF);
        do { uint32_t v = rd32(c->a[1]); c->a[1] += 4; wr32(c->a[2], v); cc = tst_l(cc, v); } while (DBRA(c->d[0]));
next:
        wr16(c->a[6] - 2, (uint16_t)add_w(&cc, 1, rd16(c->a[6] - 2)));     /* $233C */
        cmp_w(&cc, 0x40, rd16(c->a[6] - 2));
    } while (cc & CC_C);
    sp = c->a[6]; c->a[6] = rd32(sp); sp += 4;                              /* unlk a6 */
    c->a[7] = sp0; c->ccr = cc;
}

/* CopyRawTiles ($22A8): d0 = VRAM address, d1 = global tile number (0 = clear), d2 = longs.
 * Copies d2 longs of raw tile data straight from a raw block of the graphics table, starting
 * at tile d1, linearly through the ROM (so it may run into the 6 extra tiles after the block).
 * d1 = 0 writes d2 + 1 zero longs instead. The original hangs on an entry that is empty or not
 * raw ($22D0, $22D8); so does this. */
void CopyRawTiles(Cpu *c)
{
    wr32(c->a[7] - 4, 0x22AC);                                             /* jsr SetVRAMWrite */
    c->a[7] -= 4; SetVRAMWrite(c); c->a[7] += 4;
    uint16_t cc = c->ccr;
    c->a[1] = 0x00C00000;
    c->d[0] = set_w(c->d[0], c->d[1]); cc = tst_w(cc, c->d[1]);
    if (!(c->d[1] & 0xFFFF)) {
        MOVEQ(c->d[0], 0);
        do { wr32(c->a[1], 0); cc = tst_l(cc, 0); } while (DBRA(c->d[2]));
        c->ccr = cc; return;
    }
    { uint32_t v = c->d[1] & 0xFFE0; c->d[1] = set_w(c->d[1], v); cc = tst_w(cc, v); }
    c->d[1] = set_w(c->d[1], lsr_n(&cc, c->d[1], 3, 16));
    c->a[0] = SEXT(c->d[1]) + GFX_TABLE;
    c->d[1] = rd32(c->a[0]); cc = tst_l(cc, c->d[1]);
    if (c->d[1] & 0x80000000u) for (;;) {}                                 /* bmi.w * */
    if (!(c->d[1] & 0x01000000u)) for (;;) {}                              /* bclr #24; beq.w * */
    c->d[1] &= ~0x01000000u; cc = (uint16_t)(cc & ~CC_Z);
    c->a[0] = c->d[1];
    { uint32_t v = c->d[0] & 0x1F; c->d[0] = set_w(c->d[0], v); cc = tst_w(cc, v); }
    c->d[0] = set_w(c->d[0], lsl_n(&cc, c->d[0], 5, 16));
    c->a[0] += SEXT(c->d[0]);
    c->d[2] = set_w(c->d[2], sub_w(&cc, 1, c->d[2]));
    do { uint32_t v = rd32(c->a[0]); c->a[0] += 4; wr32(c->a[1], v); cc = tst_l(cc, v); } while (DBRA(c->d[2]));
    c->ccr = cc;
}

void DecodeGfxBlock(Cpu *c)
{
    R r = { c->d[0], c->d[1], c->d[2], c->d[3], c->d[4], c->d[5], c->d[6], c->d[7],
            c->a[0], c->a[1], c->a[2], c->a[3], c->a[7], c->ccr };
    uint16_t cc = r.cc;
    r.d0 = set_w(r.d0, lsl_n(&cc, r.d0, 2, 16));
    r.a0 = SEXT(r.d0) + GFX_TABLE;
    r.d0 = rd32(r.a0); cc = tst_l(cc, r.d0);
    if (r.d0 & 0x80000000u) {                                               /* empty slot */
        MW(r.d1, 0xFF); MOVEQ(r.d0, 0);
        do { wr32(r.a1, 0); r.a1 += 4; cc = tst_l(cc, 0); } while (DBRA(r.d1));
    } else if (r.d0 & 0x01000000u) {                                        /* raw */
        r.d0 &= ~0x01000000u; cc = (uint16_t)(cc & ~CC_Z);                  /* bclr: bit was set */
        r.a0 = r.d0;
        MW(r.d1, 0xFF);
        do { uint32_t v = rd32(r.a0); r.a0 += 4; wr32(r.a1, v); r.a1 += 4; cc = tst_l(cc, v); } while (DBRA(r.d1));
    } else {
        cc = (uint16_t)(cc | CC_Z);                                         /* bclr #24: bit was clear */
        int lzf = (r.d0 >> 25) & 1;
        r.d0 &= ~0x02000000u;
        cc = (uint16_t)((cc & ~CC_Z) | (lzf ? 0 : CC_Z));                   /* bclr #25 */
        r.cc = cc;
        if (lzf) lz(&r); else tiles(&r);
        cc = r.cc;
    }
    c->d[0] = r.d0; c->d[1] = r.d1; c->d[2] = r.d2; c->d[3] = r.d3; c->d[4] = r.d4;
    c->d[5] = r.d5; c->d[6] = r.d6; c->d[7] = r.d7;
    c->a[0] = r.a0; c->a[1] = r.a1; c->a[2] = r.a2; c->a[3] = r.a3;
    c->ccr = cc;
}
