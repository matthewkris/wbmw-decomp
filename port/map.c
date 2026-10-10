/* Map lookups ($2542-$25C8, $30D2-$3140, $3F58-$3FC0): the block at a position and its
 * collision type. a3 = layer (plane A $FF9754 or B $FF97D4, see docs/NOTES.md, Rooms):
 *   +$00 flags (bit 1: one row of chunks)  +$10..$1F the four block tables
 *   +$22 chunk map (word address)  +$44/+$46 largest column / row*64 offset in the layer
 * Positions: d6 = block column + $100, d7 = block row * 64 + $4000 (TileAttrAtPixel makes them
 * from pixel X and Y, which carry a $1000 origin). Labels are the original's addresses. */
#include "port.h"

#define SEXT(w) ((uint32_t)(int32_t)(int16_t)((w) & 0xFFFF))
#define CHUNK_OFFSETS 0x25CAu
#define TYPE_TABLE 0x3FC2u

/* MapBlockAtRaw ($2570): d2 = column, d3 = row * 64, both inside the layer. d0 = block word
 * (0 for an empty chunk). Rows come in bands of 12 ($300 = 12 * 64): the band and the column's
 * chunk pick the chunk map entry. */
void MapBlockAtRaw(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d1 = c->d[1], d2 = c->d[2], d3 = c->d[3], d4, d5;
    d5 = 0; cc = tst_l(cc, 0);
    d4 = set_w(c->d[4], 0xC00); cc = tst_w(cc, 0xC00);
    cmp_w(&cc, d4, d3);
    if (!(cc & CC_C)) { d3 = set_w(d3, sub_w(&cc, d4, d3)); d5 = 32; cc = tst_l(cc, 32); }
    d4 = set_w(d4, lsr_n(&cc, d4, 1, 16));
    cmp_w(&cc, d4, d3);
    if (!(cc & CC_C)) { d3 = set_w(d3, sub_w(&cc, d4, d3)); d5 = set_w(d5, add_w(&cc, 0x10, d5)); }
    d4 = set_w(d4, lsr_n(&cc, d4, 1, 16));
    cmp_w(&cc, d4, d3);
    if (!(cc & CC_C)) { d3 = set_w(d3, sub_w(&cc, d4, d3)); d5 = set_b(d5, add_b(&cc, 8, d5)); }
    {                                                                   /* $2594 */
        int one_row = rd8(c->a[3]) & 2;
        cc = (uint16_t)((cc & ~CC_Z) | (one_row ? 0 : CC_Z));
        if (one_row) { d5 = set_w(d5, 0); cc = (uint16_t)((cc & CC_X) | CC_Z); }
    }
    d4 = set_w(d4, d2); cc = tst_w(cc, d2);
    d4 = set_w(d4, lsr_n(&cc, d4, 4, 16));
    d4 = set_w(d4, d4 | d5); cc = tst_w(cc, d4);
    c->a[0] = SEXT(rd16(c->a[3] + 0x22));
    d4 = set_b(d4, rd8(c->a[0] + SEXT(d4))); cc = tst_b(cc, d4);
    if (!(d4 & 0x80)) {
        d4 = set_w(d4, add_w(&cc, d4, d4));
        d3 = set_w(d3, lsr_n(&cc, d3, 1, 16));
        d1 = 15; cc = tst_l(cc, 15);
        d1 = set_w(d1, d1 & d2); cc = tst_w(cc, d1);
        d1 = set_w(d1, add_w(&cc, d1, d1));
        d1 = set_w(d1, add_w(&cc, d3, d1));
        d1 = set_w(d1, add_w(&cc, rd16(CHUNK_OFFSETS + SEXT(d4)), d1));
        c->a[0] = 0x00FF5000;
        c->d[0] = set_w(c->d[0], rd16(c->a[0] + SEXT(d1))); cc = tst_w(cc, c->d[0]);
    }
    c->d[1] = d1; c->d[2] = d2; c->d[3] = d3; c->d[4] = d4; c->d[5] = d5; c->ccr = cc;
}

/* MapBlockAt ($2542): clamp d6/d7 into the layer, then MapBlockAtRaw */
void MapBlockAt(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[0] = 0; cc = tst_l(cc, 0);
    c->d[2] = set_w(c->d[2], c->d[6]); cc = tst_w(cc, c->d[6]);
    c->d[2] = set_w(c->d[2], sub_w(&cc, 0x100, c->d[2]));
    if (cc & CC_C) { c->d[2] = 0; cc = tst_l(cc, 0); }
    else {
        cmp_w(&cc, rd16(c->a[3] + 0x44), c->d[2]);
        if (!(cc & (CC_C | CC_Z))) { c->d[2] = set_w(c->d[2], rd16(c->a[3] + 0x44)); cc = tst_w(cc, c->d[2]); }
    }
    c->d[3] = set_w(c->d[3], c->d[7]); cc = tst_w(cc, c->d[7]);             /* $255A */
    c->d[3] = set_w(c->d[3], sub_w(&cc, 0x4000, c->d[3]));
    if (cc & CC_C) { c->d[3] = 0; cc = tst_l(cc, 0); }
    else {
        cmp_w(&cc, rd16(c->a[3] + 0x46), c->d[3]);
        if (!(cc & (CC_C | CC_Z))) { c->d[3] = set_w(c->d[3], rd16(c->a[3] + 0x46)); cc = tst_w(cc, c->d[3]); }
    }
    c->ccr = cc;
    MapBlockAtRaw(c);
}

/* TileTypeOf ($30F4): attribute byte d1 -> collision type word d2, through the two-level table
 * at $3FC2 (high nibble picks a sub-table, low nibble the entry). For the player only, types
 * $5xxx become 8 unless bit 5 of $FF9EF1 is set, and $6xxx become 15 unless bit 4 is. */
void TileTypeOf(Cpu *c)
{
    uint16_t cc = c->ccr;
    uint32_t d1 = c->d[1], d2 = c->d[2];
    d2 = set_b(d2, d1); cc = tst_b(cc, d1);
    d2 = set_w(d2, d2 & 0xF0); cc = tst_w(cc, d2);
    d2 = set_w(d2, lsr_n(&cc, d2, 3, 16));
    c->a[0] = TYPE_TABLE;
    d2 = set_w(d2, rd16(c->a[0] + SEXT(d2))); cc = tst_w(cc, d2);
    c->a[0] += SEXT(d2);
    d2 = set_b(d2, d1); cc = tst_b(cc, d1);
    d2 = set_w(d2, d2 & 0xF); cc = tst_w(cc, d2);
    d2 = set_w(d2, add_w(&cc, d2, d2));
    d2 = set_w(d2, rd16(c->a[0] + SEXT(d2))); cc = tst_w(cc, d2);
    cmp_l(&cc, SEXT(rd16(0xFF9EEE)), c->a[4]);                              /* cmpa.w: the player? */
    if (cc & CC_Z) {
        c->d[0] = set_w(c->d[0], d2); cc = tst_w(cc, d2);
        c->d[0] = set_w(c->d[0], c->d[0] & 0xF000); cc = tst_w(cc, c->d[0]);
        cmp_w(&cc, 0x5000, c->d[0]);
        if (cc & CC_Z) {
            int keep = rd8(0xFF9EF1) & 0x20;
            cc = (uint16_t)((cc & ~CC_Z) | (keep ? 0 : CC_Z));
            if (!keep) { d2 = 8; cc = tst_l(cc, 8); }
        } else {
            cmp_w(&cc, 0x6000, c->d[0]);
            if (cc & CC_Z) {
                int keep = rd8(0xFF9EF1) & 0x10;
                cc = (uint16_t)((cc & ~CC_Z) | (keep ? 0 : CC_Z));
                if (!keep) { d2 = 15; cc = tst_l(cc, 15); }
            }
        }
    }
    c->d[2] = d2; c->ccr = cc;
}

/* TileAttrAt ($30DA): collision type d2 of the block at d6/d7 (also d0 = block, d1 = attribute) */
void TileAttrAt(Cpu *c)
{
    uint32_t sp = c->a[7];
    wr32(sp - 4, 0x30DE); c->a[7] = sp - 4; MapBlockAt(c); c->a[7] = sp;
    uint16_t cc = c->ccr;
    c->d[1] = set_w(c->d[1], c->d[0]); cc = tst_w(cc, c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[1] & 0x300); cc = tst_w(cc, c->d[1]);
    c->d[1] = set_w(c->d[1], lsr_n(&cc, c->d[1], 6, 16));
    c->a[0] = rd32(c->a[3] + 0x10 + SEXT(c->d[1]));                          /* the block table */
    c->d[1] = set_w(c->d[1], 0x800); cc = tst_w(cc, 0x800);
    c->d[1] = set_b(c->d[1], c->d[0]); cc = tst_b(cc, c->d[0]);
    c->d[1] = set_b(c->d[1], rd8(c->a[0] + SEXT(c->d[1]))); cc = tst_b(cc, c->d[1]);   /* attribute */
    c->ccr = cc;
    TileTypeOf(c);
}

/* TileAttrAtPixel ($30D2): as TileAttrAt, from pixel coordinates d6 = X, d7 = Y */
void TileAttrAtPixel(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[6] = set_w(c->d[6], lsr_n(&cc, c->d[6], 4, 16));
    c->d[7] = set_w(c->d[7], c->d[7] & 0xFFF0); cc = tst_w(cc, c->d[7]);
    c->d[7] = set_w(c->d[7], asl_n(&cc, c->d[7], 2, 16));
    c->ccr = cc;
    TileAttrAt(c);
}

/* the probes: the collision type at a point of entity a4, stored for the movement code */
static void probe(Cpu *c, uint32_t ret, uint32_t store)
{
    uint32_t sp = c->a[7];
    wr32(sp - 4, ret); c->a[7] = sp - 4; TileAttrAtPixel(c); c->a[7] = sp;
    wr16(store, (uint16_t)c->d[2]); c->ccr = tst_w(c->ccr, c->d[2]);
}

/* ProbeTileBelow ($3F58): at the feet (Y + height) -> $FF9EDE */
void ProbeTileBelow(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[6] = set_w(c->d[6], rd16(ea(c->a[4], -0x3800))); cc = tst_w(cc, c->d[6]);
    c->d[7] = 0; cc = tst_l(cc, 0);
    c->d[7] = set_b(c->d[7], rd8(ea(c->a[4], -0x34FD))); cc = tst_b(cc, c->d[7]);
    c->d[7] = set_w(c->d[7], add_w(&cc, rd16(ea(c->a[4], -0x3700)), c->d[7]));
    c->ccr = cc;
    probe(c, 0x3F6A, 0xFF9EDE);
}

/* ProbeTileAbove ($3F74): at the head (Y - height) -> $FF9EDC */
void ProbeTileAbove(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[6] = set_w(c->d[6], rd16(ea(c->a[4], -0x3800))); cc = tst_w(cc, c->d[6]);
    c->d[0] = 0; cc = tst_l(cc, 0);
    c->d[0] = set_b(c->d[0], rd8(ea(c->a[4], -0x34FD))); cc = tst_b(cc, c->d[0]);
    c->d[7] = set_w(c->d[7], rd16(ea(c->a[4], -0x3700))); cc = tst_w(cc, c->d[7]);
    c->d[7] = set_w(c->d[7], sub_w(&cc, c->d[0], c->d[7]));
    c->ccr = cc;
    probe(c, 0x3F88, 0xFF9EDC);
}

/* ProbeTileFeet ($3F92): just above the feet (Y + height - 1) -> $FF9EE0 */
void ProbeTileFeet(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[6] = set_w(c->d[6], rd16(ea(c->a[4], -0x3800))); cc = tst_w(cc, c->d[6]);
    c->d[7] = 0; cc = tst_l(cc, 0);
    c->d[7] = set_b(c->d[7], rd8(ea(c->a[4], -0x34FD))); cc = tst_b(cc, c->d[7]);
    c->d[7] = set_w(c->d[7], add_w(&cc, rd16(ea(c->a[4], -0x3700)), c->d[7]));
    c->d[7] = set_w(c->d[7], sub_w(&cc, 1, c->d[7]));
    c->ccr = cc;
    probe(c, 0x3FA6, 0xFF9EE0);
}

/* ProbeTileAtA3 ($3FB0) and ProbeTileAt ($3FAC, plane A): at the entity's position -> $FF9EE2 */
void ProbeTileAtA3(Cpu *c)
{
    uint16_t cc = c->ccr;
    c->d[6] = set_w(c->d[6], rd16(ea(c->a[4], -0x3800))); cc = tst_w(cc, c->d[6]);
    c->d[7] = set_w(c->d[7], rd16(ea(c->a[4], -0x3700))); cc = tst_w(cc, c->d[7]);
    c->ccr = cc;
    probe(c, 0x3FBC, 0xFF9EE2);
}

void ProbeTileAt(Cpu *c) { c->a[3] = 0xFFFF9754; ProbeTileAtA3(c); }
